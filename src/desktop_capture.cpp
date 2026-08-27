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
static bool g_captureExclusionEnabled = false;

static bool supportsTransparentCaptureExclusion()
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll)
        return false;
    typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOW *);
    RtlGetVersionFn rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
        GetProcAddress(ntdll, "RtlGetVersion"));
    if (!rtlGetVersion)
        return false;

    OSVERSIONINFOW version;
    ZeroMemory(&version, sizeof(version));
    version.dwOSVersionInfoSize = sizeof(version);
    if (rtlGetVersion(&version) != 0)
        return false;
    return version.dwMajorVersion > 10 ||
           (version.dwMajorVersion == 10 && version.dwBuildNumber >= 19041);
}

static bool setWindowCaptureExclusion(HWND hwnd, bool enabled)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32 || !hwnd)
        return false;
    typedef BOOL (WINAPI *SetWindowDisplayAffinityFn)(HWND, DWORD);
    SetWindowDisplayAffinityFn fn = reinterpret_cast<SetWindowDisplayAffinityFn>(
        GetProcAddress(user32, "SetWindowDisplayAffinity"));
    if (!fn)
        return false;

    if (enabled && !supportsTransparentCaptureExclusion())
        return false;

    const DWORD affinity = enabled ? 0x00000011u : 0x00000000u;
    const bool ok = fn(hwnd, affinity) != FALSE;
    if (ok)
        g_captureExclusionEnabled = enabled;
    return ok;
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

static BOOL CALLBACK wallpaperWindowCandidate(HWND hwnd, LPARAM param)
{
    WindowSearch *search = reinterpret_cast<WindowSearch *>(param);
    if (!search || !IsWindow(hwnd))
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

struct ShellWindowSearch
{
    RECT target;
    HWND best;
    qint64 bestArea;
    ShellWindowSearch() : best(0), bestArea(0) { ZeroMemory(&target, sizeof(target)); }
};

static BOOL CALLBACK shellWallpaperCandidate(HWND hwnd, LPARAM param)
{
    ShellWindowSearch *search = reinterpret_cast<ShellWindowSearch *>(param);
    if (!search || !IsWindow(hwnd))
        return TRUE;

    wchar_t className[128] = {0};
    GetClassNameW(hwnd, className, 127);
    if (_wcsicmp(className, L"WorkerW") != 0 && _wcsicmp(className, L"Progman") != 0)
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

static HWND findShellWallpaperWindow(const QRect &screenRect)
{
    ShellWindowSearch search;
    search.target.left = screenRect.left();
    search.target.top = screenRect.top();
    search.target.right = screenRect.right() + 1;
    search.target.bottom = screenRect.bottom() + 1;

    HWND progman = FindWindowW(L"Progman", 0);
    if (progman)
        shellWallpaperCandidate(progman, reinterpret_cast<LPARAM>(&search));
    EnumWindows(shellWallpaperCandidate, reinterpret_cast<LPARAM>(&search));
    return search.best;
}

static HWND wallpaperWindowForRect(const QRect &screenRect)
{
    bool cachedUsable = false;
    if (g_cachedWallpaperWindow && IsWindow(g_cachedWallpaperWindow)) {
        RECT rect;
        if (GetWindowRect(g_cachedWallpaperWindow, &rect)) {
            RECT target = { screenRect.left(), screenRect.top(),
                            screenRect.right() + 1, screenRect.bottom() + 1 };
            cachedUsable = intersectionArea(rect, target) > 0;
        }
    }

    // Re-probe occasionally even while the shell fallback is valid so a
    // Wallpaper Engine instance started after Kadia is picked up live. Doing
    // the Toolhelp process walk every frame would defeat the performance goal.
    const DWORD now = GetTickCount();
    if (!cachedUsable || static_cast<DWORD>(now - g_lastWallpaperProbe) >= 2000u) {
        g_lastWallpaperProbe = now;
        HWND animated = findWallpaperEngineWindow(screenRect);
        if (animated) {
            g_cachedWallpaperWindow = animated;
            return g_cachedWallpaperWindow;
        }
        if (!cachedUsable)
            g_cachedWallpaperWindow = findShellWallpaperWindow(screenRect);
    }
    return g_cachedWallpaperWindow;
}

static QImage captureScreenRegion(const QRect &screenRect, const QSize &outputSize)
{
    if (screenRect.isEmpty() || outputSize.isEmpty())
        return QImage();

    HDC sourceDc = GetDC(0);
    if (!sourceDc)
        return QImage();
    HDC memoryDc = CreateCompatibleDC(sourceDc);
    if (!memoryDc) {
        ReleaseDC(0, sourceDc);
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
        ReleaseDC(0, sourceDc);
        return QImage();
    }

    HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);
    SetStretchBltMode(memoryDc, HALFTONE);
    SetBrushOrgEx(memoryDc, 0, 0, 0);
    const BOOL ok = StretchBlt(memoryDc,
                               0, 0, outputSize.width(), outputSize.height(),
                               sourceDc, screenRect.left(), screenRect.top(),
                               screenRect.width(), screenRect.height(), SRCCOPY | CAPTUREBLT);

    QImage result;
    if (ok) {
        QImage wrapped(static_cast<uchar *>(bits), outputSize.width(), outputSize.height(),
                       outputSize.width() * 4, QImage::Format_RGB32);
        result = wrapped.copy();
    }

    SelectObject(memoryDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(0, sourceDc);
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

}
#endif

namespace DesktopCapture {

bool setCaptureExclusion(WId windowId, bool enabled)
{
#ifdef Q_OS_WIN
    return setWindowCaptureExclusion(reinterpret_cast<HWND>(windowId), enabled);
#else
    Q_UNUSED(windowId);
    Q_UNUSED(enabled);
    return false;
#endif
}

QImage capture(const QRect &screenRect, const QSize &outputSize)
{
#ifdef Q_OS_WIN
    // This is the most literal transparency mode: capture the composited
    // desktop directly while Kadia is excluded from capture. It preserves
    // Wallpaper Engine, desktop icons and any animation exactly as Windows is
    // displaying them.
    if (g_captureExclusionEnabled) {
        const QImage screen = captureScreenRegion(screenRect, outputSize);
        if (!screen.isNull())
            return screen;
    }

    // XP / older Windows fallback: capture the wallpaper host itself so Kadia
    // never recursively records its own window.
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

}
