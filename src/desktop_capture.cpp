#include "desktop_capture.h"

#include <QImage>
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

static BOOL CALLBACK wallpaperWindowCandidate(HWND hwnd, LPARAM param)
{
    WindowSearch *search = reinterpret_cast<WindowSearch *>(param);
    if (!search || !IsWindow(hwnd) || isDesktopUiClass(hwnd))
        return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!search->pids.contains(pid))
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

static HWND findWallpaperWorker()
{
    // Explorer normally keeps SHELLDLL_DefView (desktop icons) in Progman or
    // one WorkerW.  The WorkerW immediately behind that host is the wallpaper
    // layer.  Selecting this layer is crucial: screen capture or the DefView
    // host would also include icons, taskbars and ordinary application windows.
    DefViewSearch search;
    EnumWindows(findDefViewHostCallback, reinterpret_cast<LPARAM>(&search));
    if (search.host) {
        HWND worker = FindWindowExW(0, search.host, L"WorkerW", 0);
        if (worker)
            return worker;
    }

    HWND progman = FindWindowW(L"Progman", 0);
    return progman;
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
    // Capture only the wallpaper renderer. Never call GetDC(NULL) / BitBlt on
    // the desktop here: that is the composited desktop and includes icons,
    // taskbars and ordinary windows, which is explicitly not the requested
    // transparency effect.
    HWND source = wallpaperWindowForRect(screenRect);
    if (!source)
        return QImage();
    return captureWindowRegion(source, screenRect, outputSize);
#else
    Q_UNUSED(screenRect);
    Q_UNUSED(outputSize);
    return QImage();
#endif
}

} // namespace DesktopCapture
