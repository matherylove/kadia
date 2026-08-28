#include "emulator_manager.h"
#include "game_stats.h"
#include "windspro_bootstrap.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <shellapi.h>
#  include <tlhelp32.h>
#endif

namespace {

static const char kShellAssociation[] = "__KADIA_WINDOWS_ASSOCIATION__";

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

static void migrateAutomaticSelections()
{
    QSettings s(settingsPath(), QSettings::IniFormat);
    const int schema = s.value(QStringLiteral("emulatorSelection/schema"), 0).toInt();
    if (schema >= 2)
        return;

    // Builds before schema 2 silently wrote auto-detected WinDS PRO paths to
    // emulators/<system>. There was no UI in those builds that could have
    // created a deliberate user choice, so discard those inherited automatic
    // assignments once. This guarantees the first launch on the fixed build
    // actually asks the user which emulator to use.
    s.remove(QStringLiteral("emulators"));
    s.setValue(QStringLiteral("emulatorSelection/schema"), 2);
    s.sync();
}

static QStringList supportedSystems()
{
    return QStringList()
        << QStringLiteral("Nintendo Entertainment System")
        << QStringLiteral("Super Nintendo")
        << QStringLiteral("Nintendo 64")
        << QStringLiteral("Nintendo GameCube")
        << QStringLiteral("Nintendo Wii")
        << QStringLiteral("Nintendo Wii U")
        << QStringLiteral("Nintendo Switch")
        << QStringLiteral("Sega Master System")
        << QStringLiteral("Sega Genesis / Mega Drive")
        << QStringLiteral("Sega Saturn")
        << QStringLiteral("Sega Dreamcast")
        << QStringLiteral("PlayStation")
        << QStringLiteral("PlayStation 2")
        << QStringLiteral("PlayStation 3")
        << QStringLiteral("Xbox")
        << QStringLiteral("Xbox 360")
        << QStringLiteral("Atari 2600")
        << QStringLiteral("Atari 5200")
        << QStringLiteral("Atari 7800")
        << QStringLiteral("PC Engine / TurboGrafx-16")
        << QStringLiteral("Neo Geo")
        << QStringLiteral("Game Boy")
        << QStringLiteral("Game Boy Color")
        << QStringLiteral("Game Boy Advance")
        << QStringLiteral("Nintendo DS")
        << QStringLiteral("Nintendo 3DS")
        << QStringLiteral("PlayStation Portable")
        << QStringLiteral("PlayStation Vita")
        << QStringLiteral("Sega Game Gear")
        << QStringLiteral("Atari Lynx")
        << QStringLiteral("Neo Geo Pocket")
        << QStringLiteral("Neo Geo Pocket Color")
        << QStringLiteral("WonderSwan")
        << QStringLiteral("WonderSwan Color")
        << QStringLiteral("MSX")
        << QStringLiteral("Commodore 64")
        << QStringLiteral("Amiga")
        << QStringLiteral("DOS / PC")
        << QStringLiteral("Arcade");
}

static QStringList executableHints(const QString &system)
{
    const QString s = system.toLower();

    // Keep the most-specific systems first so a portable/derived PlayStation
    // platform never falls through to the PS1 emulator list.
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
    if (s.contains(QStringLiteral("game gear")))
        return QStringList() << QStringLiteral("ares.exe") << QStringLiteral("mednafen.exe") << QStringLiteral("kega-fusion.exe") << QStringLiteral("fusion.exe");
    if (s.contains(QStringLiteral("master system")))
        return QStringList() << QStringLiteral("ares.exe") << QStringLiteral("kega-fusion.exe") << QStringLiteral("fusion.exe") << QStringLiteral("mednafen.exe");
    if (s.contains(QStringLiteral("genesis")) || s.contains(QStringLiteral("mega drive")))
        return QStringList() << QStringLiteral("ares.exe") << QStringLiteral("kega-fusion.exe") << QStringLiteral("fusion.exe") << QStringLiteral("mednafen.exe");
    if (s.contains(QStringLiteral("sega")))
        return QStringList() << QStringLiteral("ares.exe") << QStringLiteral("mednafen.exe") << QStringLiteral("kega-fusion.exe") << QStringLiteral("fusion.exe");
    if (s.contains(QStringLiteral("atari 2600")))
        return QStringList() << QStringLiteral("stella.exe");
    if (s.contains(QStringLiteral("atari 5200")) || s.contains(QStringLiteral("atari 7800")))
        return QStringList() << QStringLiteral("a7800.exe") << QStringLiteral("altirra64.exe") << QStringLiteral("altirra.exe") << QStringLiteral("mame.exe");
    if (s.contains(QStringLiteral("atari lynx")))
        return QStringList() << QStringLiteral("mednafen.exe");
    if (s.contains(QStringLiteral("pc engine")) || s.contains(QStringLiteral("turbografx")))
        return QStringList() << QStringLiteral("ares.exe") << QStringLiteral("mednafen.exe");
    if (s.contains(QStringLiteral("neo geo pocket")))
        return QStringList() << QStringLiteral("ares.exe") << QStringLiteral("mednafen.exe");
    if (s.contains(QStringLiteral("neo geo")))
        return QStringList() << QStringLiteral("fbneo.exe") << QStringLiteral("mame.exe") << QStringLiteral("mame64.exe") << QStringLiteral("ares.exe");
    if (s.contains(QStringLiteral("wonderswan")))
        return QStringList() << QStringLiteral("ares.exe") << QStringLiteral("mednafen.exe");
    if (s == QStringLiteral("msx"))
        return QStringList() << QStringLiteral("openmsx.exe");
    if (s.contains(QStringLiteral("commodore 64")))
        return QStringList() << QStringLiteral("x64sc.exe") << QStringLiteral("x64.exe");
    if (s == QStringLiteral("amiga"))
        return QStringList() << QStringLiteral("winuae64.exe") << QStringLiteral("winuae.exe");
    if (s.contains(QStringLiteral("dos")))
        return QStringList() << QStringLiteral("dosbox-staging.exe") << QStringLiteral("dosbox.exe");
    if (s == QStringLiteral("mame"))
        return QStringList() << QStringLiteral("mame.exe") << QStringLiteral("mame64.exe");
    if (s == QStringLiteral("fbneo") || s.contains(QStringLiteral("finalburn")))
        return QStringList() << QStringLiteral("fbneo.exe");
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
        return args << QStringLiteral("--fullscreen") << QStringLiteral("--pause-menu-exit") << absoluteRomPath;
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
    if (exe == QStringLiteral("fbneo.exe"))
        // Classic Win32 FBNeo already enters fullscreen for command-line game
        // launches, while SDL builds expose a different command-line surface.
        // Passing a frontend-specific switch to an unknown FBNeo build can be
        // interpreted as content, so launch the ROM normally and use FBNeo's
        // native Alt+Enter binding only if its renderer remains windowed.
        return args << absoluteRomPath;
    if (exe == QStringLiteral("mesen.exe") || exe == QStringLiteral("mesen2.exe"))
        return args << QStringLiteral("--fullscreen") << absoluteRomPath;
    if (exe == QStringLiteral("ares.exe"))
        return args << QStringLiteral("--fullscreen") << QStringLiteral("--no-file-prompt")
                    << absoluteRomPath;
    if (exe == QStringLiteral("stella.exe"))
        return args << QStringLiteral("-fullscreen") << QStringLiteral("1") << absoluteRomPath;
    if (exe == QStringLiteral("mednafen.exe"))
        // Mednafen exposes global settings directly as -<setting> <value>.
        return args << QStringLiteral("-video.fs") << QStringLiteral("1") << absoluteRomPath;
    if (exe == QStringLiteral("kega-fusion.exe") || exe == QStringLiteral("fusion.exe"))
        return args << absoluteRomPath << QStringLiteral("-fullscreen");
    if (exe == QStringLiteral("altirra.exe") || exe == QStringLiteral("altirra64.exe"))
        return args << QStringLiteral("/f") << absoluteRomPath;
    if (exe == QStringLiteral("x64sc.exe") || exe == QStringLiteral("x64.exe"))
        return args << QStringLiteral("-VICIIfull") << absoluteRomPath;
    if (exe == QStringLiteral("dosbox-staging.exe") || exe == QStringLiteral("dosbox.exe"))
        return args << QStringLiteral("-fullscreen") << absoluteRomPath;
    if (exe == QStringLiteral("openmsx.exe"))
        // openMSX has a native Alt+Enter fullscreen binding but no simple
        // one-shot fullscreen CLI flag. Launch media normally; the staged native
        // hotkey fallback below performs the transition after the renderer exists.
        return args << absoluteRomPath;
    if (exe == QStringLiteral("flycast.exe"))
        // Flycast's current command line accepts transient configuration values.
        // This asks its own window backend for fullscreen without persisting the
        // user's emu.cfg setting.
        return args << QStringLiteral("-config") << QStringLiteral("window:fullscreen=yes")
                    << absoluteRomPath;
    if (exe == QStringLiteral("project64.exe"))
        // Project64's own default fullscreen shortcut is Alt+Enter.  There is no
        // stable command-line fullscreen switch across the 2.x/3.x Windows
        // builds, so launch the ROM normally and let Kadia request the emulator's
        // native fullscreen action once the real game window exists.
        return args << absoluteRomPath;
    if (exe == QStringLiteral("nestopiaue.exe"))
        // Nestopia UE exposes -f/--fullscreen in its current command-line shell.
        return args << QStringLiteral("-f") << absoluteRomPath;
    if (exe == QStringLiteral("nestopia.exe"))
        // The classic Win32 Nestopia shell uses its configuration-key syntax on
        // the command line rather than the GNU-style --fullscreen switch.  This
        // is equivalent to Preferences > "Switch to fullscreen on startup"
        // without permanently editing the user's configuration by hand.
        return args << absoluteRomPath
                    << QStringLiteral("-preferences")
                    << QStringLiteral("fullscreen")
                    << QStringLiteral("on")
                    << QStringLiteral("start")
                    << QStringLiteral(":")
                    << QStringLiteral("yes")
                    << QStringLiteral("-view")
                    << QStringLiteral("size")
                    << QStringLiteral("fullscreen")
                    << QStringLiteral(":")
                    << QStringLiteral("stretched");
    if (exe == QStringLiteral("redream.exe"))
        return args << QStringLiteral("--fullscreen") << absoluteRomPath;

    return args << absoluteRomPath;
}

static void findRecursiveAll(const QString &root, const QStringList &names, int maxDepth,
                             QStringList *out, int maxResults)
{
    if (!out || root.isEmpty() || names.isEmpty() || maxDepth < 0 || out->size() >= maxResults)
        return;
    QDir d(root);
    if (!d.exists())
        return;

    const QFileInfoList files = d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (int i = 0; i < files.size() && out->size() < maxResults; ++i) {
        for (int n = 0; n < names.size(); ++n) {
            if (files.at(i).fileName().compare(names.at(n), Qt::CaseInsensitive) == 0) {
                const QString hit = QDir::cleanPath(files.at(i).absoluteFilePath());
                if (!out->contains(hit, Qt::CaseInsensitive))
                    out->append(hit);
                break;
            }
        }
    }
    if (maxDepth == 0 || out->size() >= maxResults)
        return;
    const QFileInfoList dirs = d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (int i = 0; i < dirs.size() && out->size() < maxResults; ++i)
        findRecursiveAll(dirs.at(i).absoluteFilePath(), names, maxDepth - 1, out, maxResults);
}

static QStringList detectedEmulators(const QString &system)
{
    QStringList candidates;
    QString windsRoot;
    if (WinDSProBootstrap::isInstalled(&windsRoot))
        findRecursiveAll(windsRoot, executableHints(system), 5, &candidates, 24);
    return candidates;
}

static QString displayEmulator(const QString &value)
{
    if (value == QString::fromLatin1(kShellAssociation))
        return QStringLiteral("Windows default application");
    if (value.isEmpty())
        return QStringLiteral("Not selected");
    return QDir::toNativeSeparators(value);
}

static QString chooseEmulator(const QString &system, QWidget *parent, bool *rememberOut,
                              const QString &current = QString())
{
    if (rememberOut)
        *rememberOut = true;

    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Choose emulator - %1").arg(system));
    dialog.setModal(true);
    dialog.resize(720, 430);

    QLabel *explanation = new QLabel(
        QStringLiteral("Choose which emulator Kadia should use for %1.\n"
                       "Detected emulators are suggestions only; Kadia will not choose one for you.").arg(system),
        &dialog);
    explanation->setWordWrap(true);

    QListWidget *list = new QListWidget(&dialog);
    const QStringList detected = detectedEmulators(system);
    for (int i = 0; i < detected.size(); ++i) {
        QListWidgetItem *item = new QListWidgetItem(
            QStringLiteral("%1\n%2").arg(QFileInfo(detected.at(i)).fileName(),
                                         QDir::toNativeSeparators(detected.at(i))), list);
        item->setData(Qt::UserRole, detected.at(i));
        if (!current.isEmpty() && QFileInfo(current) == QFileInfo(detected.at(i)))
            list->setCurrentItem(item);
    }

#ifdef Q_OS_WIN
    QListWidgetItem *shellItem = new QListWidgetItem(
        QStringLiteral("Windows default application\nUse the file association already configured in Windows."), list);
    shellItem->setData(Qt::UserRole, QString::fromLatin1(kShellAssociation));
    if (current == QString::fromLatin1(kShellAssociation))
        list->setCurrentItem(shellItem);
#endif

    QPushButton *browse = new QPushButton(QStringLiteral("Browse for emulator..."), &dialog);
    QCheckBox *remember = new QCheckBox(QStringLiteral("Remember this emulator for %1").arg(system), &dialog);
    remember->setChecked(true);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QPushButton *ok = buttons->button(QDialogButtonBox::Ok);
    ok->setText(QStringLiteral("Use selected emulator"));
    ok->setEnabled(list->currentItem() != 0);

    QObject::connect(list, &QListWidget::currentItemChanged, [&]() {
        ok->setEnabled(list->currentItem() != 0);
    });
    QObject::connect(list, &QListWidget::itemDoubleClicked, [&](QListWidgetItem *) {
        if (list->currentItem())
            dialog.accept();
    });
    QObject::connect(browse, &QPushButton::clicked, [&]() {
        const QString initial = !current.isEmpty() && current != QString::fromLatin1(kShellAssociation)
                                ? QFileInfo(current).absolutePath() : QString();
        const QString path = QFileDialog::getOpenFileName(
            &dialog, QStringLiteral("Choose emulator executable"), initial,
            QStringLiteral("Applications (*.exe);;All files (*.*)"));
        if (path.isEmpty())
            return;
        QListWidgetItem *item = new QListWidgetItem(
            QStringLiteral("%1\n%2").arg(QFileInfo(path).fileName(), QDir::toNativeSeparators(path)), list);
        item->setData(Qt::UserRole, QDir::cleanPath(path));
        list->setCurrentItem(item);
        list->scrollToItem(item);
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QHBoxLayout *actions = new QHBoxLayout;
    actions->addWidget(browse);
    actions->addStretch();

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(12);
    layout->addWidget(explanation);
    layout->addWidget(list, 1);
    layout->addLayout(actions);
    layout->addWidget(remember);
    layout->addWidget(buttons);

    dialog.setStyleSheet(QStringLiteral(
        "QDialog{background:#101623;color:#fff8e7;} QLabel,QCheckBox{color:#fff8e7;}"
        "QListWidget{background:#182133;color:#fff8e7;border:1px solid #59657d;padding:4px;}"
        "QListWidget::item{padding:7px;} QListWidget::item:selected{background:#344764;}"
        "QPushButton{background:#202b40;color:#fff8e7;border:1px solid #69758c;padding:7px 16px;border-radius:6px;}"
        "QPushButton:hover,QPushButton:focus{background:#33415c;border-color:#e8d6aa;}"));

    if (!list->currentItem() && list->count() > 0)
        list->setCurrentRow(0); // suggestion only; user still has to confirm it

    if (dialog.exec() != QDialog::Accepted || !list->currentItem())
        return QString();
    if (rememberOut)
        *rememberOut = remember->isChecked();
    return list->currentItem()->data(Qt::UserRole).toString();
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

static QList<DWORD> processesMatchingExecutable(const QString &emulatorExecutable)
{
    QList<DWORD> result;
    const QString exeName = QFileInfo(emulatorExecutable).fileName();
    if (exeName.isEmpty() || emulatorExecutable == QString::fromLatin1(kShellAssociation))
        return result;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return result;

    PROCESSENTRY32W entry;
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (QString::fromWCharArray(entry.szExeFile).compare(exeName, Qt::CaseInsensitive) == 0)
                result.append(entry.th32ProcessID);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

static QList<DWORD> launchProcessCandidates(DWORD rootPid, const QString &emulatorExecutable)
{
    QList<DWORD> result = processTree(rootPid);

    // Portable frontends and a few emulator packages bootstrap a second process
    // and let the original PID exit.  In that case the child can be re-parented
    // before Kadia's first 200 ms probe, so a pure parent/child walk loses the
    // real renderer.  Merge processes with the exact selected executable name as
    // a recovery path.  This is especially important for Project64/Nestopia
    // bundles distributed through emulator packs.
    const QList<DWORD> named = processesMatchingExecutable(emulatorExecutable);
    for (int i = 0; i < named.size(); ++i) {
        if (!result.contains(named.at(i)))
            result.append(named.at(i));
    }
    return result;
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
    const HWND foreground = GetForegroundWindow();
    if (foreground == hwnd || (foreground && IsChild(hwnd, foreground)) ||
        (foreground && IsChild(foreground, hwnd)))
        score += area * 4;
    if (_wcsicmp(className, L"#32770") == 0) {
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

static HWND bestEmulatorWindow(DWORD rootPid, const QString &emulatorExecutable)
{
    EmulatorWindowSearch search;
    search.pids = launchProcessCandidates(rootPid, emulatorExecutable);
    if (search.pids.isEmpty())
        return 0;
    EnumWindows(emulatorWindowCandidate, reinterpret_cast<LPARAM>(&search));
    return search.best;
}

static bool windowCoversMonitor(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
        return false;

    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!monitor || !GetMonitorInfoW(monitor, &mi))
        return false;

    // Measure the renderer/client area, not the decorated outer HWND. A maximized
    // window can have an outer rect matching the monitor while its client still
    // leaves borders/titlebar visible, which previously made Kadia think a
    // windowed emulator was already fullscreen.
    RECT cr;
    ZeroMemory(&cr, sizeof(cr));
    if (!GetClientRect(hwnd, &cr))
        return false;
    POINT tl = { cr.left, cr.top };
    POINT br = { cr.right, cr.bottom };
    if (!ClientToScreen(hwnd, &tl) || !ClientToScreen(hwnd, &br))
        return false;

    const int tolerance = 6;
    const bool clientCovers =
        qAbs(tl.x - mi.rcMonitor.left) <= tolerance &&
        qAbs(tl.y - mi.rcMonitor.top) <= tolerance &&
        qAbs(br.x - mi.rcMonitor.right) <= tolerance &&
        qAbs(br.y - mi.rcMonitor.bottom) <= tolerance;
    if (clientCovers)
        return true;

    // Exclusive/borderless render windows do not always expose a client rect
    // equal to the monitor even though the top-level renderer is already in
    // fullscreen.  Accept an outer monitor-sized popup only when normal window
    // decorations are gone; this still rejects an ordinary maximized window.
    RECT wr;
    ZeroMemory(&wr, sizeof(wr));
    if (!GetWindowRect(hwnd, &wr))
        return false;
    const bool outerCovers =
        qAbs(wr.left - mi.rcMonitor.left) <= tolerance &&
        qAbs(wr.top - mi.rcMonitor.top) <= tolerance &&
        qAbs(wr.right - mi.rcMonitor.right) <= tolerance &&
        qAbs(wr.bottom - mi.rcMonitor.bottom) <= tolerance;
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const bool decorated = (style & WS_CAPTION) != 0 || (style & WS_THICKFRAME) != 0;
    return outerCovers && !decorated;
}

static bool focusEmulatorWindow(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
        return false;
    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);

    const DWORD targetThread = GetWindowThreadProcessId(hwnd, 0);
    const DWORD currentThread = GetCurrentThreadId();
    const bool attached = targetThread && targetThread != currentThread &&
                          AttachThreadInput(currentThread, targetThread, TRUE) != FALSE;

    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);

    if (attached)
        AttachThreadInput(currentThread, targetThread, FALSE);
    return GetForegroundWindow() == hwnd || IsChild(hwnd, GetForegroundWindow());
}

static bool sendAltEnter(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
        return false;

    focusEmulatorWindow(hwnd);
    Sleep(25);

    INPUT input[4];
    ZeroMemory(input, sizeof(input));
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = VK_MENU;
    input[1].type = INPUT_KEYBOARD;
    input[1].ki.wVk = VK_RETURN;
    input[2].type = INPUT_KEYBOARD;
    input[2].ki.wVk = VK_RETURN;
    input[2].ki.dwFlags = KEYEVENTF_KEYUP;
    input[3].type = INPUT_KEYBOARD;
    input[3].ki.wVk = VK_MENU;
    input[3].ki.dwFlags = KEYEVENTF_KEYUP;
    if (SendInput(4, input, sizeof(INPUT)) == 4)
        return true;

    // Very old shells can reject injected global input while still accepting
    // the equivalent system-key messages in their own queue.  Keep this as a
    // secondary path only; the preferred path above remains a real Alt+Enter.
    const LPARAM down = static_cast<LPARAM>(1u | (1u << 29));
    const LPARAM up = static_cast<LPARAM>(1u | (1u << 29) | (1u << 30) | (1u << 31));
    const BOOL a = PostMessageW(hwnd, WM_SYSKEYDOWN, VK_RETURN, down);
    const BOOL b = PostMessageW(hwnd, WM_SYSKEYUP, VK_RETURN, up);
    return a != FALSE && b != FALSE;
}

enum FullscreenHotkey
{
    FullscreenHotkeyNone = 0,
    FullscreenHotkeyAltEnter,
    FullscreenHotkeyF11
};

static FullscreenHotkey emulatorFullscreenHotkey(const QString &emulatorExecutable)
{
    const QString exe = QFileInfo(emulatorExecutable).fileName().toLower();
    if (exe.isEmpty() || emulatorExecutable == QString::fromLatin1(kShellAssociation))
        return FullscreenHotkeyAltEnter;

    // Use only hotkeys that are native to the corresponding emulator. This is
    // intentionally not a generic "try F11 then Alt+Enter" routine: doing that
    // can trigger unrelated emulator actions and was the source of earlier
    // fullscreen regressions. Native CLI options always get first chance.
    if (exe == QStringLiteral("mesen.exe") || exe == QStringLiteral("mesen2.exe") ||
        exe == QStringLiteral("flycast.exe"))
        return FullscreenHotkeyF11;

    if (exe == QStringLiteral("project64.exe") ||
        exe == QStringLiteral("nestopia.exe") ||
        exe == QStringLiteral("nestopiaue.exe") ||
        exe == QStringLiteral("openmsx.exe") ||
        exe == QStringLiteral("stella.exe") ||
        exe == QStringLiteral("fbneo.exe") ||
        exe == QStringLiteral("dosbox.exe") ||
        exe == QStringLiteral("dosbox-staging.exe") ||
        exe.contains(QStringLiteral("duckstation")) ||
        exe.startsWith(QStringLiteral("pcsx2")) ||
        exe.startsWith(QStringLiteral("ppsspp")) ||
        exe.startsWith(QStringLiteral("snes9x")) ||
        exe.startsWith(QStringLiteral("dolphin")) ||
        exe == QStringLiteral("cemu.exe") ||
        exe == QStringLiteral("redream.exe") ||
        exe == QStringLiteral("mgba.exe") ||
        exe == QStringLiteral("melonds.exe") ||
        exe.contains(QStringLiteral("citra")) ||
        exe.contains(QStringLiteral("lime3ds")) ||
        exe == QStringLiteral("xemu.exe") ||
        exe.startsWith(QStringLiteral("xenia")))
        return FullscreenHotkeyAltEnter;

    return FullscreenHotkeyNone;
}

static bool sendFunctionKey(HWND hwnd, WORD key)
{
    if (!hwnd || !IsWindow(hwnd))
        return false;
    focusEmulatorWindow(hwnd);
    Sleep(25);

    INPUT input[2];
    ZeroMemory(input, sizeof(input));
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = key;
    input[1].type = INPUT_KEYBOARD;
    input[1].ki.wVk = key;
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    if (SendInput(2, input, sizeof(INPUT)) == 2)
        return true;

    const LPARAM down = static_cast<LPARAM>(1u);
    const LPARAM up = static_cast<LPARAM>(1u | (1u << 30) | (1u << 31));
    const BOOL a = PostMessageW(hwnd, WM_KEYDOWN, key, down);
    const BOOL b = PostMessageW(hwnd, WM_KEYUP, key, up);
    return a != FALSE && b != FALSE;
}

static bool maximizeEmulatorWindow(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
        return false;
    if (windowCoversMonitor(hwnd))
        return true;

    // Last-resort fallback: ordinary maximization is deliberately less
    // aggressive than fake borderless fullscreen.  It is reversible through
    // the standard Restore command and, crucially, cannot desynchronize a
    // renderer child/swapchain into a small square at the monitor corner.
    ShowWindow(hwnd, SW_MAXIMIZE);
    return true;
}

static bool shellLaunch(const QString &absoluteRomPath, qint64 *pidOut)
{
    SHELLEXECUTEINFOW sei;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = reinterpret_cast<LPCWSTR>(absoluteRomPath.utf16());
    sei.nShow = SW_SHOWNORMAL;
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
    migrateAutomaticSelections();
    QSettings s(settingsPath(), QSettings::IniFormat);
    QString configured = QDir::fromNativeSeparators(
        s.value(QStringLiteral("emulators/%1").arg(systemKey(system))).toString());

    if (system.compare(QStringLiteral("PlayStation Vita"), Qt::CaseInsensitive) == 0 &&
        QFileInfo(configured).fileName().contains(QStringLiteral("duckstation"), Qt::CaseInsensitive))
        configured.clear();
    return configured;
}

void EmulatorManager::setConfiguredEmulator(const QString &system, const QString &executable)
{
    migrateAutomaticSelections();
    QSettings s(settingsPath(), QSettings::IniFormat);
    const QString key = QStringLiteral("emulators/%1").arg(systemKey(system));
    if (executable.isEmpty())
        s.remove(key);
    else if (executable == QString::fromLatin1(kShellAssociation))
        s.setValue(key, executable);
    else
        s.setValue(key, QDir::toNativeSeparators(QFileInfo(executable).absoluteFilePath()));
    s.sync();
}

bool EmulatorManager::configureEmulators(QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Emulators - Mathery Kadia!"));
    dialog.resize(850, 560);
    dialog.setModal(true);

    QLabel *help = new QLabel(QStringLiteral(
        "Kadia never auto-selects an emulator here. Choose a console and assign exactly the emulator you want to use."), &dialog);
    help->setWordWrap(true);

    QTreeWidget *tree = new QTreeWidget(&dialog);
    tree->setColumnCount(2);
    tree->setHeaderLabels(QStringList() << QStringLiteral("Console") << QStringLiteral("Emulator"));
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(true);
    tree->header()->setStretchLastSection(true);

    const QStringList systems = supportedSystems();
    for (int i = 0; i < systems.size(); ++i) {
        QTreeWidgetItem *item = new QTreeWidgetItem(tree);
        item->setText(0, systems.at(i));
        item->setText(1, displayEmulator(configuredEmulator(systems.at(i))));
        item->setData(0, Qt::UserRole, systems.at(i));
    }
    if (tree->topLevelItemCount() > 0)
        tree->setCurrentItem(tree->topLevelItem(0));

    QPushButton *change = new QPushButton(QStringLiteral("Choose / change..."), &dialog);
    QPushButton *clear = new QPushButton(QStringLiteral("Clear assignment"), &dialog);
    QPushButton *close = new QPushButton(QStringLiteral("Done"), &dialog);

    auto changeCurrent = [&]() {
        QTreeWidgetItem *item = tree->currentItem();
        if (!item)
            return;
        const QString system = item->data(0, Qt::UserRole).toString();
        bool remember = true;
        const QString choice = chooseEmulator(system, &dialog, &remember, configuredEmulator(system));
        if (choice.isEmpty())
            return;
        // In the settings editor a selection is, by definition, persistent;
        // ignore the launch-dialog "remember" checkbox value.
        Q_UNUSED(remember);
        setConfiguredEmulator(system, choice);
        item->setText(1, displayEmulator(choice));
    };

    QObject::connect(change, &QPushButton::clicked, changeCurrent);
    QObject::connect(tree, &QTreeWidget::itemDoubleClicked, [&](QTreeWidgetItem *, int) { changeCurrent(); });
    QObject::connect(clear, &QPushButton::clicked, [&]() {
        QTreeWidgetItem *item = tree->currentItem();
        if (!item)
            return;
        const QString system = item->data(0, Qt::UserRole).toString();
        setConfiguredEmulator(system, QString());
        item->setText(1, QStringLiteral("Not selected"));
    });
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);

    QHBoxLayout *actions = new QHBoxLayout;
    actions->addWidget(change);
    actions->addWidget(clear);
    actions->addStretch();
    actions->addWidget(close);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(12);
    layout->addWidget(help);
    layout->addWidget(tree, 1);
    layout->addLayout(actions);

    dialog.setStyleSheet(QStringLiteral(
        "QDialog{background:#101623;color:#fff8e7;} QLabel{color:#fff8e7;}"
        "QTreeWidget{background:#182133;color:#fff8e7;border:1px solid #59657d;}"
        "QHeaderView::section{background:#202b40;color:#fff8e7;padding:6px;border:0;border-right:1px solid #59657d;}"
        "QPushButton{background:#202b40;color:#fff8e7;border:1px solid #69758c;padding:7px 16px;border-radius:6px;}"
        "QPushButton:hover,QPushButton:focus{background:#33415c;border-color:#e8d6aa;}"));

    return dialog.exec() == QDialog::Accepted;
}

bool EmulatorManager::launch(const QString &system, const QString &romPath, QWidget *parent,
                             qint64 *processIdOut, QString *emulatorExecutableOut)
{
    if (processIdOut)
        *processIdOut = 0;
    if (emulatorExecutableOut)
        emulatorExecutableOut->clear();

    const QFileInfo romInfo(romPath);
    if (!romInfo.exists()) {
        QMessageBox::warning(parent, QStringLiteral("Mathery Kadia!"),
                             QStringLiteral("The selected game file no longer exists:\n%1")
                                 .arg(QDir::toNativeSeparators(romInfo.absoluteFilePath())));
        return false;
    }

    QString absoluteRomPath = romInfo.canonicalFilePath();
    if (absoluteRomPath.isEmpty())
        absoluteRomPath = romInfo.absoluteFilePath();
    absoluteRomPath = QDir::toNativeSeparators(absoluteRomPath);

    QString emulator = configuredEmulator(system);
    if (!emulator.isEmpty() && emulator != QString::fromLatin1(kShellAssociation) && !QFileInfo(emulator).exists()) {
        QMessageBox::warning(parent, QStringLiteral("Emulator not found"),
            QStringLiteral("The emulator previously selected for %1 no longer exists:\n%2\n\nChoose another emulator.")
                .arg(system, QDir::toNativeSeparators(emulator)));
        setConfiguredEmulator(system, QString());
        emulator.clear();
    }

    if (emulator.isEmpty()) {
        bool remember = true;
        emulator = chooseEmulator(system, parent, &remember);
        if (emulator.isEmpty())
            return false; // user cancelled; never silently fall through
        if (remember)
            setConfiguredEmulator(system, emulator);
    }

    bool started = false;
    qint64 launchedPid = 0;
    if (emulator != QString::fromLatin1(kShellAssociation)) {
        const QStringList args = launchArguments(emulator, system, absoluteRomPath);
        started = QProcess::startDetached(emulator, args, QFileInfo(emulator).absolutePath(), &launchedPid);
        if (!started) {
            QMessageBox::warning(parent, QStringLiteral("Could not start emulator"),
                QStringLiteral("Kadia could not start:\n%1\n\nNo different emulator was selected automatically.")
                    .arg(QDir::toNativeSeparators(emulator)));
            return false;
        }
    } else {
#ifdef Q_OS_WIN
        started = shellLaunch(absoluteRomPath, &launchedPid);
#else
        started = false;
#endif
    }

    if (!started) {
        QMessageBox::information(parent, QStringLiteral("Could not open game"),
            QStringLiteral("Windows could not open this game with the selected association."));
        return false;
    }

    if (processIdOut)
        *processIdOut = launchedPid;
    if (emulatorExecutableOut)
        *emulatorExecutableOut = emulator;
    GameStats::recordLaunch(romPath);
    return true;
}

bool EmulatorManager::enforceFullscreen(qint64 processId, const QString &emulatorExecutable, int stage)
{
#ifdef Q_OS_WIN
    if (processId <= 0)
        return false;
    HWND hwnd = bestEmulatorWindow(static_cast<DWORD>(processId), emulatorExecutable);
    if (!hwnd)
        return false;
    if (windowCoversMonitor(hwnd))
        return true;

    // Do not use a universal F11 or mutate window styles. F11 means unrelated
    // actions in several emulators, while style rewriting caused the small
    // renderer-in-a-corner bug. A focused Alt+Enter is reversible and is the
    // emulator-specific native path after its CLI options had time to work. Stage
    // 1 is a second attempt in case stage 0 landed on a transient
    // launcher window which was replaced by the real renderer.
    if (stage <= 1) {
        const FullscreenHotkey hotkey = emulatorFullscreenHotkey(emulatorExecutable);
        if (hotkey == FullscreenHotkeyAltEnter) {
            sendAltEnter(hwnd);
            return false;
        }
        if (hotkey == FullscreenHotkeyF11) {
            sendFunctionKey(hwnd, VK_F11);
            return false;
        }
    }

    // Final compatibility fallback is ordinary Windows maximization only. It
    // never strips WS_CAPTION/WS_THICKFRAME or changes a child renderer size, so
    // the emulator can always Restore or toggle its own fullscreen later.
    if (stage >= 2)
        return maximizeEmulatorWindow(hwnd);
    return false;
#else
    Q_UNUSED(processId);
    Q_UNUSED(emulatorExecutable);
    Q_UNUSED(stage);
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

bool EmulatorManager::isLaunchRunning(qint64 processId, const QString &emulatorExecutable)
{
#ifdef Q_OS_WIN
    if (processId <= 0)
        return false;
    const QList<DWORD> pids = launchProcessCandidates(static_cast<DWORD>(processId), emulatorExecutable);
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
    Q_UNUSED(emulatorExecutable);
    return false;
#endif
}
