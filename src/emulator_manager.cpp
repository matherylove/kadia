#include "emulator_manager.h"
#include "game_stats.h"
#include "windspro_bootstrap.h"

#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QMessageBox>
#include <QProcess>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <shellapi.h>
#  include <tlhelp32.h>
#endif

namespace {
static QString settingsPath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) base = QDir::homePath() + QStringLiteral("/.mathery-kadia");
    QDir().mkpath(base);
    return QDir(base).filePath(QStringLiteral("settings.ini"));
}

static QString systemKey(const QString &system)
{
    QString key = system.toLower().simplified();
    for (int i = 0; i < key.size(); ++i)
        if (!key.at(i).isLetterOrNumber()) key[i] = QLatin1Char('_');
    return key;
}

static QStringList executableHints(const QString &system)
{
    const QString s = system.toLower();

    // Most-specific systems must come first. In particular PlayStation Vita
    // used to fall through to the generic "playstation" branch and could be
    // incorrectly associated with DuckStation.
    if (s.contains(QStringLiteral("playstation vita")))
        return QStringList() << QStringLiteral("vita3k.exe");
    if (s.contains(QStringLiteral("3ds")))
        return QStringList() << QStringLiteral("lime3ds.exe") << QStringLiteral("citra-qt.exe") << QStringLiteral("citra.exe");
    if (s.contains(QStringLiteral("nintendo ds")))
        return QStringList() << QStringLiteral("melonds.exe") << QStringLiteral("desmume.exe");
    if (s.contains(QStringLiteral("game boy")))
        return QStringList() << QStringLiteral("mgba.exe") << QStringLiteral("visualboyadvance-m.exe") << QStringLiteral("vba-m.exe");
    if (s.contains(QStringLiteral("psp")) || s.contains(QStringLiteral("portable")))
        return QStringList() << QStringLiteral("ppssppwindows64.exe") << QStringLiteral("ppssppwindows.exe") << QStringLiteral("ppsspp.exe");
    if (s.contains(QStringLiteral("playstation 3")))
        return QStringList() << QStringLiteral("rpcs3.exe");
    if (s.contains(QStringLiteral("playstation 2")))
        return QStringList() << QStringLiteral("pcsx2-qt.exe") << QStringLiteral("pcsx2-qtx64-avx2.exe") << QStringLiteral("pcsx2-qtx64-sse4.exe") << QStringLiteral("pcsx2.exe");
    if (s == QStringLiteral("playstation"))
        return QStringList() << QStringLiteral("duckstation-qt-x64-release.exe") << QStringLiteral("duckstation-qt-x64-releaseltcg.exe") << QStringLiteral("duckstation.exe") << QStringLiteral("epsxe.exe");
    if (s.contains(QStringLiteral("wii u")))
        return QStringList() << QStringLiteral("cemu.exe");
    if (s.contains(QStringLiteral("switch")))
        return QStringList() << QStringLiteral("ryujinx.exe") << QStringLiteral("yuzu.exe");
    if (s.contains(QStringLiteral("gamecube")) || s == QStringLiteral("nintendo wii"))
        return QStringList() << QStringLiteral("dolphin.exe") << QStringLiteral("dolphinqt2.exe") << QStringLiteral("dolphinwx.exe");
    if (s == QStringLiteral("xbox"))
        return QStringList() << QStringLiteral("xemu.exe");
    if (s.contains(QStringLiteral("xbox 360")))
        return QStringList() << QStringLiteral("xenia_canary.exe") << QStringLiteral("xenia.exe");
    if (s.contains(QStringLiteral("super nintendo")))
        return QStringList() << QStringLiteral("snes9x-x64.exe") << QStringLiteral("snes9x.exe") << QStringLiteral("mesen2.exe") << QStringLiteral("mesen.exe");
    if (s.contains(QStringLiteral("nintendo 64")))
        return QStringList() << QStringLiteral("project64.exe") << QStringLiteral("mupen64plus-ui-console.exe");
    if (s.contains(QStringLiteral("nintendo entertainment")))
        return QStringList() << QStringLiteral("mesen2.exe") << QStringLiteral("mesen.exe") << QStringLiteral("nestopia.exe");
    if (s.contains(QStringLiteral("dreamcast")))
        return QStringList() << QStringLiteral("redream.exe") << QStringLiteral("flycast.exe");
    if (s.contains(QStringLiteral("saturn")))
        return QStringList() << QStringLiteral("mednafen.exe") << QStringLiteral("yabasanshiro.exe") << QStringLiteral("kega-fusion.exe") << QStringLiteral("fusion.exe");
    if (s.contains(QStringLiteral("sega")))
        return QStringList() << QStringLiteral("kega-fusion.exe") << QStringLiteral("fusion.exe") << QStringLiteral("mednafen.exe");
    if (s.contains(QStringLiteral("atari 2600")))
        return QStringList() << QStringLiteral("stella.exe");
    if (s.contains(QStringLiteral("atari 5200")) || s.contains(QStringLiteral("atari 7800")))
        return QStringList() << QStringLiteral("a7800.exe") << QStringLiteral("altirra64.exe") << QStringLiteral("altirra.exe") << QStringLiteral("mame.exe");
    if (s.contains(QStringLiteral("atari lynx")))
        return QStringList() << QStringLiteral("mednafen.exe");
    if (s.contains(QStringLiteral("pc engine")) || s.contains(QStringLiteral("turbografx")))
        return QStringList() << QStringLiteral("mednafen.exe");
    if (s.contains(QStringLiteral("neo geo")))
        return QStringList() << QStringLiteral("fbneo.exe") << QStringLiteral("mame.exe") << QStringLiteral("mame64.exe");
    if (s.contains(QStringLiteral("wonderswan")))
        return QStringList() << QStringLiteral("mednafen.exe");
    if (s == QStringLiteral("msx"))
        return QStringList() << QStringLiteral("openmsx.exe");
    if (s.contains(QStringLiteral("commodore 64")))
        return QStringList() << QStringLiteral("x64sc.exe") << QStringLiteral("x64.exe");
    if (s == QStringLiteral("amiga"))
        return QStringList() << QStringLiteral("winuae64.exe") << QStringLiteral("winuae.exe");
    if (s.contains(QStringLiteral("dos")))
        return QStringList() << QStringLiteral("dosbox-staging.exe") << QStringLiteral("dosbox.exe");
    if (s.contains(QStringLiteral("arcade")))
        return QStringList() << QStringLiteral("mame.exe") << QStringLiteral("mame64.exe") << QStringLiteral("fbneo.exe");
    return QStringList();
}

static QStringList launchArguments(const QString &emulator, const QString &system,
                                   const QString &absoluteRomPath)
{
    Q_UNUSED(system);
    const QString exe = QFileInfo(emulator).fileName().toLower();
    QStringList args;

    // Keep documented native fullscreen switches where they are stable. Kadia
    // also applies a short-lived Win32 borderless-fullscreen enforcement pass
    // after launch, so an emulator/version that ignores its CLI switch still
    // fills the monitor instead of silently falling back to a window.
    if (exe.startsWith(QStringLiteral("pcsx2-qt")) || exe.startsWith(QStringLiteral("pcsx2-qtx")))
        return args << QStringLiteral("-fullscreen") << QStringLiteral("-batch")
                    << QStringLiteral("--") << absoluteRomPath;
    if (exe == QStringLiteral("pcsx2.exe"))
        return args << absoluteRomPath << QStringLiteral("--fullscreen") << QStringLiteral("--nogui");

    if (exe == QStringLiteral("vita3k.exe"))
        return args << QStringLiteral("-F") << absoluteRomPath;
    if (exe.contains(QStringLiteral("duckstation")))
        return args << QStringLiteral("-fullscreen") << QStringLiteral("-batch")
                    << QStringLiteral("--") << absoluteRomPath;
    if (exe.startsWith(QStringLiteral("ppsspp")))
        return args << QStringLiteral("--fullscreen") << QStringLiteral("--pause-menu-exit")
                    << absoluteRomPath;
    if (exe == QStringLiteral("melonds.exe"))
        return args << QStringLiteral("-f") << absoluteRomPath;
    if (exe == QStringLiteral("mgba.exe"))
        return args << QStringLiteral("-f") << absoluteRomPath;
    if (exe.startsWith(QStringLiteral("snes9x")))
        return args << QStringLiteral("-fullscreen") << absoluteRomPath;
    if (exe.contains(QStringLiteral("citra")) || exe.contains(QStringLiteral("lime3ds")))
        return args << QStringLiteral("-f") << absoluteRomPath;
    if (exe.startsWith(QStringLiteral("dolphin")))
        return args << QStringLiteral("-b")
                    << QStringLiteral("-C") << QStringLiteral("Dolphin.Display.Fullscreen=True")
                    << QStringLiteral("-e") << absoluteRomPath;
    if (exe == QStringLiteral("cemu.exe"))
        return args << QStringLiteral("-g") << absoluteRomPath << QStringLiteral("-f");
    if (exe == QStringLiteral("yuzu.exe"))
        return args << QStringLiteral("-f") << QStringLiteral("-g") << absoluteRomPath;
    if (exe == QStringLiteral("ryujinx.exe"))
        return args << QStringLiteral("--fullscreen") << absoluteRomPath;
    if (exe == QStringLiteral("rpcs3.exe"))
        return args << QStringLiteral("--fullscreen") << absoluteRomPath;
    if (exe.startsWith(QStringLiteral("xenia")))
        return args << absoluteRomPath << QStringLiteral("--fullscreen=true");
    if (exe == QStringLiteral("mame.exe") || exe == QStringLiteral("mame64.exe"))
        return args << QStringLiteral("-nowindow") << absoluteRomPath;
    if (exe == QStringLiteral("project64.exe"))
        return args << QStringLiteral("/fullscreen") << absoluteRomPath;
    if (exe == QStringLiteral("redream.exe"))
        return args << QStringLiteral("--fullscreen") << absoluteRomPath;

    return args << absoluteRomPath;
}

static QString findRecursive(const QString &root, const QStringList &names, int maxDepth)
{
    if (root.isEmpty() || names.isEmpty() || maxDepth < 0) return QString();
    QDir d(root);
    if (!d.exists()) return QString();
    const QFileInfoList files = d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (int i = 0; i < files.size(); ++i)
        for (int n = 0; n < names.size(); ++n)
            if (files.at(i).fileName().compare(names.at(n), Qt::CaseInsensitive) == 0)
                return files.at(i).absoluteFilePath();
    if (maxDepth == 0) return QString();
    const QFileInfoList dirs = d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (int i = 0; i < dirs.size(); ++i) {
        const QString hit = findRecursive(dirs.at(i).absoluteFilePath(), names, maxDepth - 1);
        if (!hit.isEmpty()) return hit;
    }
    return QString();
}

#ifdef Q_OS_WIN
static QList<DWORD> processTree(DWORD rootPid)
{
    QList<DWORD> ordered;
    QSet<DWORD> known;
    if (!rootPid)
        return ordered;
    known.insert(rootPid);
    ordered.append(rootPid);

    // A few frontends spawn a renderer/game child and leave their launcher
    // process alive. Include descendants so the fullscreen pass follows the
    // actual game window rather than only the initial process.
    for (int pass = 0; pass < 5; ++pass) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            break;
        QList<DWORD> additions;
        PROCESSENTRY32W entry;
        ZeroMemory(&entry, sizeof(entry));
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (known.contains(entry.th32ParentProcessID) && !known.contains(entry.th32ProcessID))
                    additions.append(entry.th32ProcessID);
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        if (additions.isEmpty())
            break;
        for (int i = 0; i < additions.size(); ++i) {
            known.insert(additions.at(i));
            ordered.append(additions.at(i));
        }
    }
    return ordered;
}

struct EmulatorWindowSearch
{
    QList<DWORD> pids;
    HWND best;
    qint64 bestArea;
    EmulatorWindowSearch() : best(0), bestArea(0) {}
};

static BOOL CALLBACK emulatorWindowCandidate(HWND hwnd, LPARAM param)
{
    EmulatorWindowSearch *search = reinterpret_cast<EmulatorWindowSearch *>(param);
    if (!search || !IsWindow(hwnd) || !IsWindowVisible(hwnd))
        return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!search->pids.contains(pid))
        return TRUE;

    wchar_t className[128] = {0};
    GetClassNameW(hwnd, className, 127);
    if (_wcsicmp(className, L"ConsoleWindowClass") == 0 ||
        _wcsicmp(className, L"tooltips_class32") == 0)
        return TRUE;

    RECT r;
    if (!GetWindowRect(hwnd, &r))
        return TRUE;
    const qint64 w = qMax<LONG>(0, r.right - r.left);
    const qint64 h = qMax<LONG>(0, r.bottom - r.top);
    const qint64 area = w * h;
    if (area < 160LL * 120LL)
        return TRUE;

    qint64 score = area;
    if (_wcsicmp(className, L"#32770") == 0) {
        // Prefer a real renderer/main window over standard modal dialogs, but
        // keep large dialog-class windows eligible for older emulators whose
        // main render window is implemented as #32770.
        if (w < 800 || h < 600)
            return TRUE;
        score /= 4;
    }

    if (score > search->bestArea) {
        search->bestArea = score;
        search->best = hwnd;
    }
    return TRUE;
}

static HWND bestEmulatorWindow(DWORD rootPid)
{
    EmulatorWindowSearch search;
    search.pids = processTree(rootPid);
    if (search.pids.isEmpty())
        return 0;
    EnumWindows(emulatorWindowCandidate, reinterpret_cast<LPARAM>(&search));
    return search.best;
}

static bool makeWindowBorderlessFullscreen(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
        return false;

    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!monitor || !GetMonitorInfoW(monitor, &mi))
        return false;

    RECT currentRect;
    ZeroMemory(&currentRect, sizeof(currentRect));
    GetWindowRect(hwnd, &currentRect);

    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);

    // If the emulator already honored its native fullscreen mode, leave its
    // window and graphics device alone. Re-styling an exclusive-fullscreen
    // window can force an unnecessary D3D/Vulkan swapchain reset.
    const bool alreadyCoversMonitor = qAbs(currentRect.left - mi.rcMonitor.left) <= 2 &&
                                      qAbs(currentRect.top - mi.rcMonitor.top) <= 2 &&
                                      qAbs(currentRect.right - mi.rcMonitor.right) <= 2 &&
                                      qAbs(currentRect.bottom - mi.rcMonitor.bottom) <= 2;
    const bool alreadyBorderless = (style & (WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME)) == 0 &&
                                   GetMenu(hwnd) == 0;
    if (alreadyCoversMonitor && alreadyBorderless)
        return true;
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
               WS_MAXIMIZEBOX | WS_SYSMENU | WS_BORDER | WS_DLGFRAME);
    style |= WS_POPUP | WS_VISIBLE;
    exStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE |
                 WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);

    SetWindowLongW(hwnd, GWL_STYLE, style);
    SetWindowLongW(hwnd, GWL_EXSTYLE, exStyle);
    if (GetMenu(hwnd))
        SetMenu(hwnd, 0);
    ShowWindow(hwnd, SW_RESTORE);

    const int width = mi.rcMonitor.right - mi.rcMonitor.left;
    const int height = mi.rcMonitor.bottom - mi.rcMonitor.top;
    return SetWindowPos(hwnd, HWND_TOP,
                        mi.rcMonitor.left, mi.rcMonitor.top, width, height,
                        SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE) != FALSE;
}

static bool shellLaunch(const QString &absoluteRomPath, qint64 *pidOut)
{
    SHELLEXECUTEINFOW sei;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = reinterpret_cast<LPCWSTR>(absoluteRomPath.utf16());
    sei.nShow = SW_SHOWMAXIMIZED;
    if (!ShellExecuteExW(&sei))
        return false;

    if (pidOut)
        *pidOut = sei.hProcess ? static_cast<qint64>(GetProcessId(sei.hProcess)) : 0;
    if (sei.hProcess)
        CloseHandle(sei.hProcess);
    return true;
}
#endif

} // namespace

QString EmulatorManager::configuredEmulator(const QString &system)
{
    QSettings s(settingsPath(), QSettings::IniFormat);
    QString configured = QDir::fromNativeSeparators(
        s.value(QStringLiteral("emulators/%1").arg(systemKey(system))).toString());

    // Repair the one incompatible automatic mapping created by the previous
    // generic PlayStation branch. Never send a Vita title to DuckStation.
    if (system.compare(QStringLiteral("PlayStation Vita"), Qt::CaseInsensitive) == 0 &&
        QFileInfo(configured).fileName().contains(QStringLiteral("duckstation"), Qt::CaseInsensitive))
        configured.clear();
    return configured;
}

void EmulatorManager::setConfiguredEmulator(const QString &system, const QString &executable)
{
    QSettings s(settingsPath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("emulators/%1").arg(systemKey(system)), QDir::toNativeSeparators(executable));
    s.sync();
}

bool EmulatorManager::launch(const QString &system, const QString &romPath, QWidget *parent,
                             qint64 *processIdOut)
{
    if (processIdOut)
        *processIdOut = 0;

    const QFileInfo romInfo(romPath);
    if (!romInfo.exists()) {
        QMessageBox::warning(parent, QStringLiteral("Mathery Kadia!"), QStringLiteral("The selected game file no longer exists."));
        return false;
    }

    // Always hand emulators an absolute path. QProcess changes the child
    // working directory to the emulator folder; a relative catalog path would
    // then resolve against PCSX2/DuckStation/etc. and appear to not exist.
    QString absoluteRomPath = romInfo.canonicalFilePath();
    if (absoluteRomPath.isEmpty())
        absoluteRomPath = romInfo.absoluteFilePath();
    absoluteRomPath = QDir::toNativeSeparators(absoluteRomPath);

    QString emulator = configuredEmulator(system);
    if (!emulator.isEmpty() && !QFileInfo(emulator).exists()) emulator.clear();

    if (emulator.isEmpty()) {
        QString windsRoot;
        if (WinDSProBootstrap::isInstalled(&windsRoot))
            emulator = findRecursive(windsRoot, executableHints(system), 5);
        if (!emulator.isEmpty())
            setConfiguredEmulator(system, emulator);
    }

    bool started = false;
    qint64 launchedPid = 0;
    if (!emulator.isEmpty()) {
        const QStringList args = launchArguments(emulator, system, absoluteRomPath);
        started = QProcess::startDetached(emulator, args, QFileInfo(emulator).absolutePath(), &launchedPid);
    }

#ifdef Q_OS_WIN
    if (!started)
        started = shellLaunch(absoluteRomPath, &launchedPid);
#endif

    if (!started) {
        QMessageBox::information(parent, QStringLiteral("Emulator not configured"),
            QStringLiteral("Kadia could not find a compatible emulator for %1. Configure an emulator path in Kadia Settings, or install WinDS PRO.").arg(system));
        return false;
    }

    if (processIdOut)
        *processIdOut = launchedPid;
    GameStats::recordLaunch(romPath);
    return true;
}

bool EmulatorManager::enforceFullscreen(qint64 processId)
{
#ifdef Q_OS_WIN
    if (processId <= 0)
        return false;
    return makeWindowBorderlessFullscreen(bestEmulatorWindow(static_cast<DWORD>(processId)));
#else
    Q_UNUSED(processId);
    return false;
#endif
}


bool EmulatorManager::isProcessTreeRunning(qint64 processId)
{
#ifdef Q_OS_WIN
    if (processId <= 0)
        return false;
    const QList<DWORD> pids = processTree(static_cast<DWORD>(processId));
    for (int i = 0; i < pids.size(); ++i) {
        HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, pids.at(i));
        if (!process)
            continue;
        DWORD exitCode = 0;
        const bool running = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
        CloseHandle(process);
        if (running)
            return true;
    }
    return false;
#else
    Q_UNUSED(processId);
    return false;
#endif
}
