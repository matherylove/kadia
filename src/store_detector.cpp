#include "store_detector.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QSet>
#include <QString>

namespace {

static bool fileExistsAny(const QStringList &paths)
{
    for (int i = 0; i < paths.size(); ++i) {
        if (QFileInfo(paths[i]).exists())
            return true;
    }
    return false;
}

static bool uninstallContains(const QStringList &needles)
{
#ifdef Q_OS_WIN
    const QStringList roots = QStringList()
        << QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall")
        << QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall")
        << QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall");

    for (int r = 0; r < roots.size(); ++r) {
        QSettings root(roots[r], QSettings::NativeFormat);
        const QStringList groups = root.childGroups();
        for (int i = 0; i < groups.size(); ++i) {
            root.beginGroup(groups[i]);
            const QString name = root.value(QStringLiteral("DisplayName")).toString();
            root.endGroup();
            for (int n = 0; n < needles.size(); ++n) {
                if (!name.isEmpty() && name.contains(needles[n], Qt::CaseInsensitive))
                    return true;
            }
        }
    }
#else
    Q_UNUSED(needles);
#endif
    return false;
}

static QString pf(const char *name)
{
    return QString::fromLocal8Bit(qgetenv(name));
}

}

namespace StoreDetector {

QStringList detectInstalledStores()
{
    QStringList stores;
#ifdef Q_OS_WIN
    const QString programFiles = pf("ProgramFiles");
    const QString programFilesX86 = pf("ProgramFiles(x86)");
    const QString localAppData = pf("LOCALAPPDATA");
    const QString programData = pf("ProgramData");

    {
        QSettings steam(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"), QSettings::NativeFormat);
        const QString steamPath = steam.value(QStringLiteral("SteamPath")).toString();
        if ((!steamPath.isEmpty() && QFileInfo(QDir(steamPath).filePath(QStringLiteral("steam.exe"))).exists()) ||
            fileExistsAny(QStringList() << QDir(programFilesX86).filePath(QStringLiteral("Steam/steam.exe"))
                                        << QDir(programFiles).filePath(QStringLiteral("Steam/steam.exe"))) ||
            uninstallContains(QStringList() << QStringLiteral("Steam")))
            stores << QStringLiteral("Steam");
    }

    if (fileExistsAny(QStringList()
                      << QDir(programFilesX86).filePath(QStringLiteral("Epic Games/Launcher/Portal/Binaries/Win32/EpicGamesLauncher.exe"))
                      << QDir(programFiles).filePath(QStringLiteral("Epic Games/Launcher/Portal/Binaries/Win64/EpicGamesLauncher.exe"))) ||
        uninstallContains(QStringList() << QStringLiteral("Epic Games Launcher")))
        stores << QStringLiteral("Epic");

    if (fileExistsAny(QStringList()
                      << QDir(programFilesX86).filePath(QStringLiteral("GOG Galaxy/GalaxyClient.exe"))
                      << QDir(programFiles).filePath(QStringLiteral("GOG Galaxy/GalaxyClient.exe"))) ||
        uninstallContains(QStringList() << QStringLiteral("GOG GALAXY")))
        stores << QStringLiteral("GOG");

    if (fileExistsAny(QStringList()
                      << QDir(programFiles).filePath(QStringLiteral("Electronic Arts/EA Desktop/EA Desktop/EADesktop.exe"))
                      << QDir(programFilesX86).filePath(QStringLiteral("Origin/Origin.exe"))) ||
        uninstallContains(QStringList() << QStringLiteral("EA app") << QStringLiteral("Origin")))
        stores << QStringLiteral("EA");

    if (fileExistsAny(QStringList()
                      << QDir(localAppData).filePath(QStringLiteral("Amazon Games/App/Amazon Games.exe"))
                      << QDir(programFiles).filePath(QStringLiteral("Amazon Games/App/Amazon Games.exe"))) ||
        uninstallContains(QStringList() << QStringLiteral("Amazon Games")))
        stores << QStringLiteral("Amazon");

    if (fileExistsAny(QStringList()
                      << QDir(programFilesX86).filePath(QStringLiteral("Ubisoft/Ubisoft Game Launcher/UbisoftConnect.exe"))
                      << QDir(programFiles).filePath(QStringLiteral("Ubisoft/Ubisoft Game Launcher/UbisoftConnect.exe"))) ||
        uninstallContains(QStringList() << QStringLiteral("Ubisoft Connect") << QStringLiteral("Ubisoft Game Launcher")))
        stores << QStringLiteral("Ubisoft");

    if (fileExistsAny(QStringList()
                      << QDir(programFilesX86).filePath(QStringLiteral("Battle.net/Battle.net Launcher.exe"))
                      << QDir(programFiles).filePath(QStringLiteral("Battle.net/Battle.net Launcher.exe"))
                      << QDir(programData).filePath(QStringLiteral("Battle.net/Agent/Agent.exe"))) ||
        uninstallContains(QStringList() << QStringLiteral("Battle.net")))
        stores << QStringLiteral("Battle.net");
#endif
    stores.removeDuplicates();
    return stores;
}

} // namespace StoreDetector
