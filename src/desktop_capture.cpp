#include "desktop_capture.h"

#include <QImage>
#include <QHash>
#include <QList>
#include <QString>
#include <QtGlobal>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <tlhelp32.h>
#endif

#ifdef Q_OS_WIN
namespace {

static HWND g_cachedWallpaperWindow = 0;
static DWORD g_lastWallpaperProbe = 0;
static QImage g_lastGoodWallpaperFrame;

struct PrintCaptureState
{
    int method;       // -1 unknown, -2 all print methods failed, 0..3 flags, 4 WM_PRINT
    DWORD lastProbe;
    PrintCaptureState() : method(-1), lastProbe(0) {}
};
static QHash<quintptr, PrintCaptureState> g_printCaptureStates;

static qint64 intersectionArea(const RECT &a, const RECT &b)
{
    const LONG left = qMax(a.left, b.left);
    const LONG top = qMax(a.top, b.top);
    const LONG right = qMin(a.right, b.right);
    const LONG bottom = qMin(a.bottom, b.bottom);
    if (right <= left || bottom <= top)
        return 0;
    return static_cast<qint64>(right - left) * static_cast<qint64>(bottom - top);
}

static bool processNameIsWallpaperEngine(const wchar_t *name)
{
    if (!name || !*name)
        return false;
    return _wcsicmp(name, L"wallpaper32.exe") == 0 ||
           _wcsicmp(name, L"wallpaper64.exe") == 0 ||
           _wcsicmp(name, L"wallpaper_engine.exe") == 0;
}

static QList<DWORD> wallpaperEnginePids()
{
    QList<DWORD> result;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return result;

    PROCESSENTRY32W entry;
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (processNameIsWallpaperEngine(entry.szExeFile))
                result.append(entry.th32ProcessID);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

struct WindowSearch
{
    QList<DWORD> pids;
    RECT target;
    HWND best;
    qint64 bestArea;
    WindowSearch() : best(0), bestArea(0) { ZeroMemory(&target, sizeof(target)); }
};

static bool isDesktopUiClass(HWND hwnd)
{
    wchar_t className[128] = {0};
    GetClassNameW(hwnd, className, 127);
    return _wcsicmp(className, L"SHELLDLL_DefView") == 0 ||
           _wcsicmp(className, L"SysListView32") == 0 ||
           _wcsicmp(className, L"Shell_TrayWnd") == 0 ||
           _wcsicmp(className, L"Shell_SecondaryTrayWnd") == 0;
}

static bool hasWallpaperDesktopAncestor(HWND hwnd)
{
    HWND cursor = GetParent(hwnd);
    for (int depth = 0; cursor && depth < 8; ++depth) {
        wchar_t className[128] = {0};
        GetClassNameW(cursor, className, 127);
        if (_wcsicmp(className, L"WorkerW") == 0 || _wcsicmp(className, L"Progman") == 0)
            return true;
        cursor = GetParent(cursor);
    }
    return false;
}

static BOOL CALLBACK wallpaperWindowCandidate(HWND hwnd, LPARAM param)
{
    WindowSearch *search = reinterpret_cast<WindowSearch *>(param);
    if (!search || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd) || isDesktopUiClass(hwnd))
        return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!search->pids.contains(pid))
        return TRUE;

    RECT rect;
    if (!GetWindowRect(hwnd, &rect))
        return TRUE;
    const qint64 area = intersectionArea(rect, search->target);
    if (area <= 0)
        return TRUE;

    // Wallpaper Engine normally exposes one visible renderer per display with
    // a title such as "Wallpaper Engine 1". Prefer that renderer, then visible
    // Wallpaper-Engine-owned children actually parented into WorkerW/Progman.
    // This prevents a same-sized settings/preview window from winning merely
    // because EnumWindows happened to visit it first.
    qint64 score = area;
    wchar_t title[256] = {0};
    GetWindowTextW(hwnd, title, 255);
    if (wcsstr(title, L"Wallpaper Engine") != 0)
        score += area * 8;
    if (hasWallpaperDesktopAncestor(hwnd))
        score += area * 4;
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_TOOLWINDOW) != 0)
        score /= 4;

    if (score > search->bestArea) {
        search->bestArea = score;
        search->best = hwnd;
    }
    return TRUE;
}

static BOOL CALLBACK scanTopLevelForWallpaperEngine(HWND hwnd, LPARAM param)
{
    wallpaperWindowCandidate(hwnd, param);
    EnumChildWindows(hwnd, wallpaperWindowCandidate, param);
    return TRUE;
}

static HWND findWallpaperEngineWindow(const QRect &screenRect)
{
    const QList<DWORD> pids = wallpaperEnginePids();
    if (pids.isEmpty())
        return 0;

    WindowSearch search;
    search.pids = pids;
    search.target.left = screenRect.left();
    search.target.top = screenRect.top();
    search.target.right = screenRect.right() + 1;
    search.target.bottom = screenRect.bottom() + 1;
    EnumWindows(scanTopLevelForWallpaperEngine, reinterpret_cast<LPARAM>(&search));
    return search.best;
}

struct DefViewSearch
{
    HWND host;
    DefViewSearch() : host(0) {}
};

static BOOL CALLBACK findDefViewHostCallback(HWND hwnd, LPARAM param)
{
    DefViewSearch *search = reinterpret_cast<DefViewSearch *>(param);
    if (!search)
        return TRUE;
    if (FindWindowExW(hwnd, 0, L"SHELLDLL_DefView", 0)) {
        search->host = hwnd;
        return FALSE;
    }
    return TRUE;
}

static void ensureWallpaperWorkerExists()
{
    static DWORD lastAttempt = 0;
    const DWORD now = GetTickCount();
    if (lastAttempt != 0 && static_cast<DWORD>(now - lastAttempt) < 5000u)
        return;
    lastAttempt = now;

    HWND progman = FindWindowW(L"Progman", 0);
    if (!progman)
        return;

    // Windows 7-10 traditionally accept 0/0. Newer Windows 11 raised-desktop
    // builds create the wallpaper WorkerW with 0xD/0x1. Both messages are
    // undocumented Explorer implementation details, so send them infrequently
    // with a short timeout and tolerate either being ignored. On XP they are
    // harmless.
    DWORD_PTR ignored = 0;
    SendMessageTimeoutW(progman, 0x052C, 0x000D, 0x0001,
                        SMTO_ABORTIFHUNG | SMTO_NORMAL, 150, &ignored);
    SendMessageTimeoutW(progman, 0x052C, 0, 0,
                        SMTO_ABORTIFHUNG | SMTO_NORMAL, 150, &ignored);
}

static HWND findWallpaperWorker()
{
    ensureWallpaperWorkerExists();

    HWND progman = FindWindowW(L"Progman", 0);

    // Windows 11 24H2+ can use a raised desktop: Progman is the top-level
    // no-redirection host, SHELLDLL_DefView is a layered child, and the actual
    // wallpaper is another WorkerW *child* of Progman below it. Prefer that
    // child when present.
    if (progman) {
        HWND child = 0;
        while ((child = FindWindowExW(progman, child, L"WorkerW", 0)) != 0) {
            if (!FindWindowExW(child, 0, L"SHELLDLL_DefView", 0))
                return child;
        }
    }

    // Classic shell layout: the host containing SHELLDLL_DefView has a top-level
    // WorkerW sibling immediately behind it.
    DefViewSearch search;
    EnumWindows(findDefViewHostCallback, reinterpret_cast<LPARAM>(&search));
    if (search.host) {
        HWND worker = FindWindowExW(0, search.host, L"WorkerW", 0);
        if (worker)
            return worker;
    }

    // Do not capture Progman as a fallback: on classic shells it can also own
    // SHELLDLL_DefView/SysListView32 and therefore desktop icons. If there is no
    // dedicated wallpaper WorkerW, return null and let Kadia use the already
    // loaded static Windows wallpaper image instead.
    return 0;
}

struct ChildLayerSearch
{
    RECT target;
    HWND best;
    qint64 bestArea;
    ChildLayerSearch() : best(0), bestArea(0) { ZeroMemory(&target, sizeof(target)); }
};

static BOOL CALLBACK childWallpaperCandidate(HWND hwnd, LPARAM param)
{
    ChildLayerSearch *search = reinterpret_cast<ChildLayerSearch *>(param);
    if (!search || !IsWindow(hwnd) || isDesktopUiClass(hwnd))
        return TRUE;

    RECT rect;
    if (!GetWindowRect(hwnd, &rect))
        return TRUE;
    const qint64 area = intersectionArea(rect, search->target);
    if (area > search->bestArea) {
        search->bestArea = area;
        search->best = hwnd;
    }
    return TRUE;
}

static HWND findGenericWallpaperChild(const QRect &screenRect)
{
    HWND worker = findWallpaperWorker();
    if (!worker)
        return 0;

    ChildLayerSearch search;
    search.target.left = screenRect.left();
    search.target.top = screenRect.top();
    search.target.right = screenRect.right() + 1;
    search.target.bottom = screenRect.bottom() + 1;
    EnumChildWindows(worker, childWallpaperCandidate, reinterpret_cast<LPARAM>(&search));
    return search.best;
}

static bool cachedWindowCovers(const QRect &screenRect)
{
    if (!g_cachedWallpaperWindow || !IsWindow(g_cachedWallpaperWindow))
        return false;
    RECT rect;
    if (!GetWindowRect(g_cachedWallpaperWindow, &rect))
        return false;
    RECT target = { screenRect.left(), screenRect.top(),
                    screenRect.right() + 1, screenRect.bottom() + 1 };
    return intersectionArea(rect, target) > 0;
}

static HWND wallpaperWindowForRect(const QRect &screenRect)
{
    const bool cachedUsable = cachedWindowCovers(screenRect);
    const DWORD now = GetTickCount();

    // Re-probe occasionally so starting/stopping Wallpaper Engine while Kadia
    // is open is reflected without walking the process list every frame.
    if (!cachedUsable || static_cast<DWORD>(now - g_lastWallpaperProbe) >= 1500u) {
        g_lastWallpaperProbe = now;

        // Prefer Wallpaper Engine's renderer itself. This is the animated
        // surface behind Explorer's icon layer, not a screenshot of the
        // composited desktop.
        HWND animated = findWallpaperEngineWindow(screenRect);
        if (animated) {
            g_cachedWallpaperWindow = animated;
            return g_cachedWallpaperWindow;
        }

        // Other wallpaper applications commonly parent their renderer to the
        // wallpaper WorkerW. Capture the largest child of that worker only.
        HWND generic = findGenericWallpaperChild(screenRect);
        if (generic) {
            g_cachedWallpaperWindow = generic;
            return g_cachedWallpaperWindow;
        }

        g_cachedWallpaperWindow = 0;
    }
    return g_cachedWallpaperWindow;
}

static bool imageHasUsefulPixels(const QImage &image)
{
    if (image.isNull() || image.width() < 2 || image.height() < 2)
        return false;

    // GPU-backed wallpaper windows can report a successful GDI operation while
    // returning a completely black surface. Sample a bounded grid so we can
    // reject that false-success cheaply without scanning every pixel.
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    const int stepX = qMax(1, rgb.width() / 48);
    const int stepY = qMax(1, rgb.height() / 30);
    int sampled = 0;
    int nonBlack = 0;
    int minLum = 255;
    int maxLum = 0;
    for (int y = 0; y < rgb.height(); y += stepY) {
        const QRgb *row = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); x += stepX) {
            const QRgb px = row[x];
            const int lum = (qRed(px) * 54 + qGreen(px) * 183 + qBlue(px) * 19) >> 8;
            minLum = qMin(minLum, lum);
            maxLum = qMax(maxLum, lum);
            if (qRed(px) > 5 || qGreen(px) > 5 || qBlue(px) > 5)
                ++nonBlack;
            ++sampled;
        }
    }
    return sampled > 0 && (nonBlack > sampled / 100 || (maxLum - minLum) > 4);
}

static QImage captureWindowWithPrint(HWND hwnd, const QRect &screenRect, const QSize &outputSize)
{
    if (!hwnd || !IsWindow(hwnd) || screenRect.isEmpty() || outputSize.isEmpty())
        return QImage();

    RECT client;
    ZeroMemory(&client, sizeof(client));
    if (!GetClientRect(hwnd, &client))
        return QImage();
    const int clientWidth = client.right - client.left;
    const int clientHeight = client.bottom - client.top;
    if (clientWidth <= 0 || clientHeight <= 0 || clientWidth > 16384 || clientHeight > 16384)
        return QImage();

    POINT origin = {0, 0};
    if (!ClientToScreen(hwnd, &origin))
        return QImage();

    HWND referenceWindow = hwnd;
    HDC referenceDc = GetDC(referenceWindow);
    if (!referenceDc) {
        referenceWindow = 0;
        referenceDc = GetDC(0);
    }
    if (!referenceDc)
        return QImage();

    HDC memoryDc = CreateCompatibleDC(referenceDc);
    if (!memoryDc) {
        ReleaseDC(referenceWindow, referenceDc);
        return QImage();
    }

    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = clientWidth;
    bmi.bmiHeader.biHeight = -clientHeight;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = 0;
    HBITMAP bitmap = CreateDIBSection(memoryDc, &bmi, DIB_RGB_COLORS, &bits, 0, 0);
    if (!bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        DeleteDC(memoryDc);
        ReleaseDC(referenceWindow, referenceDc);
        return QImage();
    }

    HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);

#ifndef PW_RENDERFULLCONTENT
#  define PW_RENDERFULLCONTENT 0x00000002
#endif

    const QRect wanted(screenRect.left() - origin.x, screenRect.top() - origin.y,
                       screenRect.width(), screenRect.height());
    const QRect clipped = wanted.intersected(QRect(0, 0, clientWidth, clientHeight));
    QImage result;

    auto readResult = [&]() -> QImage {
        if (clipped.isEmpty())
            return QImage();
        QImage wrapped(static_cast<uchar *>(bits), clientWidth, clientHeight,
                       clientWidth * 4, QImage::Format_RGB32);
        QImage region = wrapped.copy(clipped);
        if (region.size() != outputSize)
            region = region.scaled(outputSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        return imageHasUsefulPixels(region) ? region : QImage();
    };

    // Different Wallpaper Engine renderers behave differently here. Some D3D
    // builds only respond to the ordinary PrintWindow path, while Chromium/
    // DirectComposition-backed builds can require PW_RENDERFULLCONTENT.
    const UINT printFlags[] = {
        static_cast<UINT>(PW_CLIENTONLY | PW_RENDERFULLCONTENT),
        static_cast<UINT>(PW_RENDERFULLCONTENT),
        static_cast<UINT>(PW_CLIENTONLY),
        0u
    };

    PrintCaptureState &state = g_printCaptureStates[reinterpret_cast<quintptr>(hwnd)];
    const DWORD now = GetTickCount();

    auto tryPrintFlag = [&](int method) -> QImage {
        PatBlt(memoryDc, 0, 0, clientWidth, clientHeight, BLACKNESS);
        if (!PrintWindow(hwnd, memoryDc, printFlags[method]))
            return QImage();
        return readResult();
    };
    auto tryWmPrint = [&]() -> QImage {
        // Never use a blocking SendMessage here: a hung renderer must not freeze
        // Kadia's UI. The pixels themselves are the success criterion.
        PatBlt(memoryDc, 0, 0, clientWidth, clientHeight, BLACKNESS);
        DWORD_PTR ignored = 0;
        SendMessageTimeoutW(hwnd, WM_PRINT, reinterpret_cast<WPARAM>(memoryDc),
                            PRF_CLIENT | PRF_ERASEBKGND | PRF_NONCLIENT | PRF_CHILDREN | PRF_OWNED,
                            SMTO_ABORTIFHUNG | SMTO_NORMAL, 90, &ignored);
        return readResult();
    };

    // Once a renderer has demonstrated which path works, use only that path on
    // the 60 Hz hot loop. If all print paths are black, back off for 750 ms
    // before probing again instead of issuing four expensive cross-process paint
    // requests every frame.
    if (state.method >= 0 && state.method <= 3) {
        result = tryPrintFlag(state.method);
        if (result.isNull())
            state.method = -1;
    } else if (state.method == 4) {
        result = tryWmPrint();
        if (result.isNull())
            state.method = -1;
    } else if (state.method == -2 && state.lastProbe != 0 &&
               static_cast<DWORD>(now - state.lastProbe) < 750u) {
        // Leave result null and let the cheap GDI/static fallback handle this frame.
    }

    if (result.isNull() && state.method == -1) {
        for (int i = 0; i < 4 && result.isNull(); ++i) {
            result = tryPrintFlag(i);
            if (!result.isNull())
                state.method = i;
        }
        if (result.isNull()) {
            result = tryWmPrint();
            if (!result.isNull())
                state.method = 4;
        }
        if (result.isNull()) {
            state.method = -2;
            state.lastProbe = now;
        }
    }

    SelectObject(memoryDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(referenceWindow, referenceDc);
    return result;
}

static QImage captureWindowRegion(HWND hwnd, const QRect &screenRect, const QSize &outputSize)
{
    if (!hwnd || !IsWindow(hwnd) || screenRect.isEmpty() || outputSize.isEmpty())
        return QImage();

    HDC sourceDc = GetDC(hwnd);
    if (!sourceDc)
        return QImage();

    POINT clientOrigin = {0, 0};
    ClientToScreen(hwnd, &clientOrigin);
    const int sourceX = screenRect.left() - clientOrigin.x;
    const int sourceY = screenRect.top() - clientOrigin.y;

    HDC memoryDc = CreateCompatibleDC(sourceDc);
    if (!memoryDc) {
        ReleaseDC(hwnd, sourceDc);
        return QImage();
    }

    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = outputSize.width();
    bmi.bmiHeader.biHeight = -outputSize.height();
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = 0;
    HBITMAP bitmap = CreateDIBSection(memoryDc, &bmi, DIB_RGB_COLORS, &bits, 0, 0);
    if (!bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        DeleteDC(memoryDc);
        ReleaseDC(hwnd, sourceDc);
        return QImage();
    }

    HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);
    SetStretchBltMode(memoryDc, HALFTONE);
    SetBrushOrgEx(memoryDc, 0, 0, 0);
    PatBlt(memoryDc, 0, 0, outputSize.width(), outputSize.height(), BLACKNESS);
    const BOOL ok = StretchBlt(memoryDc,
                               0, 0, outputSize.width(), outputSize.height(),
                               sourceDc, sourceX, sourceY,
                               screenRect.width(), screenRect.height(), SRCCOPY);

    QImage result;
    if (ok) {
        QImage wrapped(static_cast<uchar *>(bits), outputSize.width(), outputSize.height(),
                       outputSize.width() * 4, QImage::Format_RGB32);
        result = wrapped.copy();
    }

    SelectObject(memoryDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(hwnd, sourceDc);
    return result;
}

} // namespace
#endif

namespace DesktopCapture {

bool setCaptureExclusion(WId windowId, bool enabled)
{
#ifdef Q_OS_WIN
    Q_UNUSED(enabled);

    // The old implementation excluded Kadia from screen capture and then
    // captured the *composited screen*. That necessarily included windows,
    // desktop icons and the taskbar. The wallpaper-only implementation never
    // captures the screen, so explicitly clear any affinity that an older run
    // may have left on this HWND and keep Kadia visible to OBS/capture tools.
    HWND hwnd = reinterpret_cast<HWND>(windowId);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32 || !hwnd)
        return false;
    typedef BOOL (WINAPI *SetWindowDisplayAffinityFn)(HWND, DWORD);
    SetWindowDisplayAffinityFn fn = reinterpret_cast<SetWindowDisplayAffinityFn>(
        GetProcAddress(user32, "SetWindowDisplayAffinity"));
    if (!fn)
        return false;
    return fn(hwnd, 0x00000000u) != FALSE; // WDA_NONE
#else
    Q_UNUSED(windowId);
    Q_UNUSED(enabled);
    return false;
#endif
}

QImage capture(const QRect &screenRect, const QSize &outputSize)
{
#ifdef Q_OS_WIN
    if (screenRect.isEmpty() || outputSize.isEmpty())
        return QImage();

    // wallpaperWindowForRect() is deliberately cached. Walking every process,
    // top-level window and child window at 60 Hz caused more work than the UI
    // renderer itself on large libraries. Re-probe only on its 1.5 s cadence.
    const HWND primary = wallpaperWindowForRect(screenRect);
    if (primary) {
        QImage frame = captureWindowWithPrint(primary, screenRect, outputSize);
        if (frame.isNull()) {
            frame = captureWindowRegion(primary, screenRect, outputSize);
            if (!imageHasUsefulPixels(frame))
                frame = QImage();
        }
        if (!frame.isNull()) {
            g_lastGoodWallpaperFrame = frame;
            return frame;
        }
    }

    // If Wallpaper Engine recreated its renderer between cache probes, retry the
    // shell wallpaper layer only after the primary path failed. This keeps the
    // normal hot path cheap while still recovering quickly from renderer swaps.
    QList<HWND> fallbacks;
    const HWND worker = findWallpaperWorker();
    if (worker && worker != primary)
        fallbacks.append(worker);
    const HWND generic = findGenericWallpaperChild(screenRect);
    if (generic && generic != primary && !fallbacks.contains(generic))
        fallbacks.append(generic);

    for (int i = 0; i < fallbacks.size(); ++i) {
        QImage frame = captureWindowWithPrint(fallbacks.at(i), screenRect, outputSize);
        if (frame.isNull()) {
            frame = captureWindowRegion(fallbacks.at(i), screenRect, outputSize);
            if (!imageHasUsefulPixels(frame))
                frame = QImage();
        }
        if (!frame.isNull()) {
            g_lastGoodWallpaperFrame = frame;
            g_cachedWallpaperWindow = fallbacks.at(i);
            return frame;
        }
    }

    // Do not flash to black when Wallpaper Engine swaps renderer windows or
    // Explorer recreates WorkerW. Keep the most recent valid wallpaper frame
    // until the next probe succeeds. The scene still owns the static Windows
    // wallpaper underneath this live layer, so the very first failed capture
    // also remains visually usable instead of becoming a black background.
    if (!g_lastGoodWallpaperFrame.isNull()) {
        if (g_lastGoodWallpaperFrame.size() == outputSize)
            return g_lastGoodWallpaperFrame;
        return g_lastGoodWallpaperFrame.scaled(outputSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return QImage();
#else
    Q_UNUSED(screenRect);
    Q_UNUSED(outputSize);
    return QImage();
#endif
}

} // namespace DesktopCapture
