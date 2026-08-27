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
    if (exe == QStringLiteral("project64.exe"))
        return args << QStringLiteral("/fullscreen") << absoluteRomPath;
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

static bool windowCoversMonitor(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
        return false;
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    RECT wr;
    ZeroMemory(&wr, sizeof(wr));
    if (!monitor || !GetMonitorInfoW(monitor, &mi) || !GetWindowRect(hwnd, &wr))
        return false;
    return qAbs(wr.left - mi.rcMonitor.left) <= 3 &&
           qAbs(wr.top - mi.rcMonitor.top) <= 3 &&
           qAbs(wr.right - mi.rcMonitor.right) <= 3 &&
           qAbs(wr.bottom - mi.rcMonitor.bottom) <= 3;
}

static bool postFullscreenShortcut(HWND hwnd, bool f11)
{
    if (!hwnd || !IsWindow(hwnd))
        return false;

    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);

    if (f11) {
        // F11 is the other common emulator-owned fullscreen toggle. Sending it
        // to the emulator (instead of rewriting its HWND styles) means the
        // emulator itself remains responsible for entering *and leaving* the
        // mode later.
        const UINT scan = MapVirtualKeyW(VK_F11, MAPVK_VK_TO_VSC);
        const LPARAM down = 1 | (static_cast<LPARAM>(scan) << 16);
        const LPARAM up = down | (1u << 30) | (1u << 31);
        PostMessageW(hwnd, WM_KEYDOWN, VK_F11, down);
        PostMessageW(hwnd, WM_KEYUP, VK_F11, up);
        return true;
    }

    // Alt+Enter is preferred because most emulators implement it as a native,
    // fully reversible fullscreen toggle.  Bit 29 marks the ALT context for a
    // WM_SYSKEY message.
    const UINT scan = MapVirtualKeyW(VK_RETURN, MAPVK_VK_TO_VSC);
    const LPARAM down = 1 | (static_cast<LPARAM>(scan) << 16) | (1u << 29);
    const LPARAM up = down | (1u << 30) | (1u << 31);
    PostMessageW(hwnd, WM_SYSKEYDOWN, VK_RETURN, down);
    PostMessageW(hwnd, WM_SYSKEYUP, VK_RETURN, up);
    return true;
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
                             qint64 *processIdOut)
{
    if (processIdOut)
        *processIdOut = 0;

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
    GameStats::recordLaunch(romPath);
    return true;
}

bool EmulatorManager::enforceFullscreen(qint64 processId, int stage)
{
#ifdef Q_OS_WIN
    if (processId <= 0)
        return false;
    HWND hwnd = bestEmulatorWindow(static_cast<DWORD>(processId));
    if (!hwnd)
        return false;
    if (windowCoversMonitor(hwnd))
        return true;

    if (stage <= 0) {
        postFullscreenShortcut(hwnd, false);
        return false; // let the emulator process the toggle before re-checking
    }
    if (stage == 1) {
        postFullscreenShortcut(hwnd, true);
        return false;
    }
    return maximizeEmulatorWindow(hwnd);
#else
    Q_UNUSED(processId);
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
