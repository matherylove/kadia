#include "emulator_manager.h"
#include "game_stats.h"
#include "windspro_bootstrap.h"

#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <shellapi.h>
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
    if (s.contains(QStringLiteral("3ds"))) return QStringList() << QStringLiteral("lime3ds.exe") << QStringLiteral("citra-qt.exe") << QStringLiteral("citra.exe");
    if (s.contains(QStringLiteral("ds"))) return QStringList() << QStringLiteral("melonds.exe") << QStringLiteral("desmume.exe");
    if (s.contains(QStringLiteral("game boy"))) return QStringList() << QStringLiteral("mgba.exe") << QStringLiteral("visualboyadvance-m.exe") << QStringLiteral("vba-m.exe");
    if (s.contains(QStringLiteral("psp")) || s.contains(QStringLiteral("portable"))) return QStringList() << QStringLiteral("ppssppwindows64.exe") << QStringLiteral("ppssppwindows.exe") << QStringLiteral("ppsspp.exe");
    if (s.contains(QStringLiteral("playstation 3"))) return QStringList() << QStringLiteral("rpcs3.exe");
    if (s.contains(QStringLiteral("playstation 2"))) return QStringList() << QStringLiteral("pcsx2-qt.exe") << QStringLiteral("pcsx2-qtx64-avx2.exe") << QStringLiteral("pcsx2-qtx64-sse4.exe") << QStringLiteral("pcsx2.exe");
    if (s.contains(QStringLiteral("playstation"))) return QStringList() << QStringLiteral("duckstation-qt-x64-release.exe") << QStringLiteral("duckstation-qt-x64-releaseltcg.exe") << QStringLiteral("duckstation.exe") << QStringLiteral("epsxe.exe");
    if (s.contains(QStringLiteral("wii u"))) return QStringList() << QStringLiteral("cemu.exe");
    if (s.contains(QStringLiteral("switch"))) return QStringList() << QStringLiteral("ryujinx.exe") << QStringLiteral("yuzu.exe");
    if (s.contains(QStringLiteral("gamecube")) || s.contains(QStringLiteral("wii"))) return QStringList() << QStringLiteral("dolphin.exe") << QStringLiteral("dolphinwx.exe");
    if (s == QStringLiteral("xbox")) return QStringList() << QStringLiteral("xemu.exe");
    if (s.contains(QStringLiteral("xbox 360"))) return QStringList() << QStringLiteral("xenia_canary.exe") << QStringLiteral("xenia.exe");
    if (s.contains(QStringLiteral("super nintendo"))) return QStringList() << QStringLiteral("snes9x-x64.exe") << QStringLiteral("snes9x.exe");
    if (s.contains(QStringLiteral("nintendo 64"))) return QStringList() << QStringLiteral("project64.exe") << QStringLiteral("mupen64plus-ui-console.exe");
    if (s.contains(QStringLiteral("nintendo"))) return QStringList() << QStringLiteral("mesen.exe") << QStringLiteral("nestopia.exe");
    if (s.contains(QStringLiteral("dreamcast"))) return QStringList() << QStringLiteral("redream.exe") << QStringLiteral("flycast.exe");
    if (s.contains(QStringLiteral("sega")) || s.contains(QStringLiteral("saturn"))) return QStringList() << QStringLiteral("kega-fusion.exe") << QStringLiteral("fusion.exe") << QStringLiteral("mednafen.exe");
    if (s.contains(QStringLiteral("arcade"))) return QStringList() << QStringLiteral("mame.exe") << QStringLiteral("mame64.exe");
    return QStringList();
}

static QStringList launchArguments(const QString &emulator, const QString &system,
                                   const QString &absoluteRomPath)
{
    Q_UNUSED(system);
    const QString exe = QFileInfo(emulator).fileName().toLower();
    QStringList args;

    // New PCSX2 Qt builds require the boot filename after "--". This also
    // prevents a game whose name begins with '-' from being parsed as a CLI
    // switch. Older wx builds expect the ISO first and use double-dash flags.
    if (exe.startsWith(QStringLiteral("pcsx2-qt")) || exe.startsWith(QStringLiteral("pcsx2-qtx")))
        return args << QStringLiteral("-fullscreen") << QStringLiteral("-batch")
                    << QStringLiteral("--") << absoluteRomPath;
    if (exe == QStringLiteral("pcsx2.exe"))
        return args << absoluteRomPath << QStringLiteral("--fullscreen") << QStringLiteral("--nogui");

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
                    << QStringLiteral("-C") << QStringLiteral("Graphics.Settings.BorderlessFullscreen=True")
                    << QStringLiteral("-e") << absoluteRomPath;
    if (exe == QStringLiteral("cemu.exe"))
        return args << QStringLiteral("-f") << QStringLiteral("-g") << absoluteRomPath;
    if (exe == QStringLiteral("yuzu.exe"))
        return args << QStringLiteral("-f") << QStringLiteral("-g") << absoluteRomPath;
    if (exe == QStringLiteral("ryujinx.exe"))
        return args << QStringLiteral("--fullscreen") << absoluteRomPath;
    if (exe == QStringLiteral("rpcs3.exe"))
        return args << QStringLiteral("--fullscreen") << QStringLiteral("--no-gui") << absoluteRomPath;
    if (exe.startsWith(QStringLiteral("xenia")))
        return args << absoluteRomPath << QStringLiteral("--fullscreen=true");
    if (exe == QStringLiteral("mame.exe") || exe == QStringLiteral("mame64.exe"))
        return args << QStringLiteral("-nowindow") << absoluteRomPath;
    if (exe == QStringLiteral("project64.exe"))
        return args << QStringLiteral("/fullscreen") << absoluteRomPath;
    if (exe == QStringLiteral("redream.exe"))
        return args << QStringLiteral("--fullscreen") << absoluteRomPath;

    // For emulators without a stable documented fullscreen CLI, preserve the
    // old direct-file behavior rather than guessing a switch that might stop
    // the game from booting. Their saved fullscreen preference still applies.
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
}

QString EmulatorManager::configuredEmulator(const QString &system)
{
    QSettings s(settingsPath(), QSettings::IniFormat);
    return QDir::fromNativeSeparators(s.value(QStringLiteral("emulators/%1").arg(systemKey(system))).toString());
}

void EmulatorManager::setConfiguredEmulator(const QString &system, const QString &executable)
{
    QSettings s(settingsPath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("emulators/%1").arg(systemKey(system)), QDir::toNativeSeparators(executable));
    s.sync();
}

bool EmulatorManager::launch(const QString &system, const QString &romPath, QWidget *parent)
{
    const QFileInfo romInfo(romPath);
    if (!romInfo.exists()) {
        QMessageBox::warning(parent, QStringLiteral("Mathery Kadia!"), QStringLiteral("The selected game file no longer exists."));
        return false;
    }

    // Always hand emulators an absolute path. A catalog entry may originate
    // as a relative path; QProcess changes the child working directory to the
    // emulator folder, which previously made PCSX2 report a perfectly valid
    // ISO as "file does not exist". canonicalFilePath() also normalizes dots
    // and separator aliases when the filesystem can resolve them.
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
    if (!emulator.isEmpty()) {
        const QStringList args = launchArguments(emulator, system, absoluteRomPath);
        started = QProcess::startDetached(emulator, args, QFileInfo(emulator).absolutePath());
    }

#ifdef Q_OS_WIN
    if (!started) {
        const HINSTANCE result = ShellExecuteW(0, L"open", reinterpret_cast<LPCWSTR>(absoluteRomPath.utf16()), 0, 0, SW_SHOWNORMAL);
        started = reinterpret_cast<INT_PTR>(result) > 32;
    }
#endif

    if (!started) {
        QMessageBox::information(parent, QStringLiteral("Emulator not configured"),
            QStringLiteral("Kadia could not find a compatible emulator for %1. Configure an emulator path in Kadia Settings, or install WinDS PRO.").arg(system));
        return false;
    }

    GameStats::recordLaunch(romPath);
    return true;
}
