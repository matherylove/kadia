#include "rom_scanner.h"

#include <QApplication>
#include <QByteArray>
#include <QCryptographicHash>
#include <QDesktopWidget>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

namespace {

static QString catalogPath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) base = QDir::homePath() + QStringLiteral("/.mathery-kadia");
    QDir().mkpath(base);
    return QDir(base).filePath(QStringLiteral("rom-catalog.ini"));
}

static QString keyForPath(const QString &path)
{
    return QString::fromLatin1(QCryptographicHash::hash(QDir::cleanPath(path).toLower().toUtf8(), QCryptographicHash::Sha1).toHex());
}

static QString hintForSuffix(const QString &suffix)
{
    const QString s = suffix.toLower();
    if (s == QStringLiteral("nes")) return QStringLiteral("Nintendo Entertainment System");
    if (s == QStringLiteral("sfc") || s == QStringLiteral("smc")) return QStringLiteral("Super Nintendo");
    if (s == QStringLiteral("n64") || s == QStringLiteral("z64") || s == QStringLiteral("v64")) return QStringLiteral("Nintendo 64");
    if (s == QStringLiteral("gb")) return QStringLiteral("Game Boy");
    if (s == QStringLiteral("gbc")) return QStringLiteral("Game Boy Color");
    if (s == QStringLiteral("gba")) return QStringLiteral("Game Boy Advance");
    if (s == QStringLiteral("nds")) return QStringLiteral("Nintendo DS");
    if (s == QStringLiteral("3ds") || s == QStringLiteral("cia")) return QStringLiteral("Nintendo 3DS");
    if (s == QStringLiteral("md") || s == QStringLiteral("gen") || s == QStringLiteral("smd")) return QStringLiteral("Sega Genesis / Mega Drive");
    if (s == QStringLiteral("sms")) return QStringLiteral("Sega Master System");
    if (s == QStringLiteral("gg")) return QStringLiteral("Sega Game Gear");
    if (s == QStringLiteral("gdi") || s == QStringLiteral("cdi")) return QStringLiteral("Sega Dreamcast");
    if (s == QStringLiteral("pce")) return QStringLiteral("PC Engine / TurboGrafx-16");
    if (s == QStringLiteral("ws")) return QStringLiteral("WonderSwan");
    if (s == QStringLiteral("wsc")) return QStringLiteral("WonderSwan Color");
    if (s == QStringLiteral("ngp")) return QStringLiteral("Neo Geo Pocket");
    if (s == QStringLiteral("ngc")) return QStringLiteral("Neo Geo Pocket Color");
    if (s == QStringLiteral("a26")) return QStringLiteral("Atari 2600");
    if (s == QStringLiteral("a52")) return QStringLiteral("Atari 5200");
    if (s == QStringLiteral("a78")) return QStringLiteral("Atari 7800");
    if (s == QStringLiteral("lnx") || s == QStringLiteral("lynx")) return QStringLiteral("Atari Lynx");
    if (s == QStringLiteral("cso")) return QStringLiteral("PlayStation Portable");
    if (s == QStringLiteral("xci") || s == QStringLiteral("nsp")) return QStringLiteral("Nintendo Switch");
    if (s == QStringLiteral("wad")) return QStringLiteral("Nintendo Wii");
    if (s == QStringLiteral("wbfs") || s == QStringLiteral("rvz")) return QStringLiteral("Nintendo Wii / GameCube");
    if (s == QStringLiteral("iso") || s == QStringLiteral("bin") || s == QStringLiteral("cue") ||
        s == QStringLiteral("rom") || s == QStringLiteral("chd") || s == QStringLiteral("img") ||
        s == QStringLiteral("mdf") || s == QStringLiteral("pbp")) return QStringLiteral("Unknown");
    return QString();
}

static bool candidateSuffix(const QString &suffix)
{
    return !hintForSuffix(suffix).isEmpty();
}

static QString normalizedScanPath(const QString &path)
{
    QString p = QDir::cleanPath(QDir::fromNativeSeparators(path));
    while (p.length() > 3 && p.endsWith(QLatin1Char('/')))
        p.chop(1);
#ifdef Q_OS_WIN
    p = p.toLower();
#endif
    return p;
}

static void addExcludedRoot(QStringList *roots, const QString &path)
{
    if (!roots || path.trimmed().isEmpty())
        return;
    const QString normalized = normalizedScanPath(path);
    if (!normalized.isEmpty() && !roots->contains(normalized))
        roots->append(normalized);
}

static QString environmentPath(const char *name)
{
    const QByteArray value = qgetenv(name);
    if (value.isEmpty())
        return QString();
    return QDir::fromNativeSeparators(QString::fromLocal8Bit(value));
}

static QStringList excludedScanRoots()
{
    QStringList roots;

    // Operating-system and application locations.  These paths contain many
    // BIN/IMG/ROM-looking support files which are not game images and used to
    // generate large numbers of false positives.
    addExcludedRoot(&roots, environmentPath("WINDIR"));
    addExcludedRoot(&roots, environmentPath("SystemRoot"));
    addExcludedRoot(&roots, environmentPath("ProgramFiles"));
    addExcludedRoot(&roots, environmentPath("ProgramFiles(x86)"));
    addExcludedRoot(&roots, environmentPath("ProgramW6432"));
    addExcludedRoot(&roots, environmentPath("CommonProgramFiles"));
    addExcludedRoot(&roots, environmentPath("CommonProgramFiles(x86)"));
    addExcludedRoot(&roots, environmentPath("ProgramData"));

    // User/application caches are also poor ROM search locations.  Do not
    // exclude the user profile itself: Downloads, Documents, Desktop, etc. are
    // intentionally still scanned.
    addExcludedRoot(&roots, environmentPath("APPDATA"));
    addExcludedRoot(&roots, environmentPath("LOCALAPPDATA"));
    addExcludedRoot(&roots, environmentPath("TEMP"));
    addExcludedRoot(&roots, environmentPath("TMP"));
    addExcludedRoot(&roots, QStandardPaths::writableLocation(QStandardPaths::TempLocation));

    roots.removeDuplicates();
    return roots;
}

static bool isSameOrBelow(const QString &path, const QString &root)
{
    if (root.isEmpty())
        return false;
    if (path == root)
        return true;
    QString prefix = root;
    if (!prefix.endsWith(QLatin1Char('/')))
        prefix += QLatin1Char('/');
    return path.startsWith(prefix);
}

static bool isExcludedScanDirectory(const QString &directory,
                                    const QString &driveRoot,
                                    const QStringList &excludedRoots)
{
    const QString path = normalizedScanPath(directory);
    const QString root = normalizedScanPath(driveRoot);
    if (path.isEmpty() || path == root)
        return false;

    for (int i = 0; i < excludedRoots.size(); ++i) {
        if (isSameOrBelow(path, excludedRoots[i]))
            return true;
    }

    const QFileInfo fi(directory);
#ifdef Q_OS_WIN
    const QString name = fi.fileName().toLower();
#else
    const QString name = fi.fileName();
#endif

    // Never descend into Windows-managed metadata/cache trees, regardless of
    // which drive they live on.  This also covers secondary drives where
    // Windows creates $RECYCLE.BIN, System Volume Information, WindowsApps,
    // recovery data, and similar folders.
    static const char *const blockedNames[] = {
        "$recycle.bin", "recycler", "recycled", "system volume information",
        "windowsapps", "wpsystem", "wudownloadcache", "recovery", "msocache",
        "config.msi", "$winreagent", "$windows.~bt", "$windows.~ws",
        "$getcurrent", "$sysreset", "esd"
    };
    for (unsigned int i = 0; i < sizeof(blockedNames) / sizeof(blockedNames[0]); ++i) {
        if (name == QString::fromLatin1(blockedNames[i]))
            return true;
    }

    // AppData/Local Settings are excluded by folder name too so that profiles
    // belonging to other Windows users do not get exhaustively scanned.
    if (name == QStringLiteral("appdata") || name == QStringLiteral("local settings"))
        return true;

    // Root-level operating-system/application directories are ignored even if
    // environment variables point to a different Windows installation.
    const QDir rootDir(driveRoot);
    const QString parent = normalizedScanPath(fi.absolutePath());
    if (parent == normalizedScanPath(rootDir.absolutePath())) {
        static const char *const blockedRootNames[] = {
            "windows", "windows.old", "program files", "program files (x86)",
            "programdata", "perflogs", "boot", "steam library", "steamlibrary",
            "epic games", "ea games", "gog games", "xboxgames"
        };
        for (unsigned int i = 0; i < sizeof(blockedRootNames) / sizeof(blockedRootNames[0]); ++i) {
            if (name == QString::fromLatin1(blockedRootNames[i]))
                return true;
        }
    }

    return false;
}

static QString styleSheet()
{
    return QStringLiteral(
        "QDialog { background:#070b12; color:#fff8e7; border:1px solid rgba(255,248,231,42); }"
        "QLabel { color:#fff8e7; background:transparent; }"
        "QListWidget { background:#0b1019; color:#fff8e7; border:1px solid rgba(255,248,231,45); outline:none; }"
        "QListWidget::item { padding:7px 10px; }"
        "QListWidget::item:selected { background:#343842; color:#fff8e7; border:1px solid #9d978a; }"
        "QPushButton { color:#fff8e7; background:#151b26; border:1px solid rgba(255,248,231,60); border-radius:4px; padding:7px 18px; }"
        "QPushButton:focus { background:#252a34; border:1px solid #fff0c8; }");
}

}

namespace RomCatalog {

bool isKnown(const QString &path)
{
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files/%1").arg(keyForPath(path)));
    const bool known = s.contains(QStringLiteral("classification"));
    s.endGroup();
    return known;
}

void saveClassification(const QString &path, const QString &system)
{
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files/%1").arg(keyForPath(path)));
    s.setValue(QStringLiteral("path"), QDir::toNativeSeparators(path));
    s.setValue(QStringLiteral("classification"), system);
    s.endGroup(); s.sync();
}

QString classification(const QString &path)
{
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files/%1").arg(keyForPath(path)));
    const QString c = s.value(QStringLiteral("classification")).toString();
    s.endGroup(); return c;
}

QStringList systems()
{
    return QStringList()
        << QStringLiteral("None (ignore)")
        << QStringLiteral("Unknown")
        << QStringLiteral("Nintendo Entertainment System")
        << QStringLiteral("Super Nintendo")
        << QStringLiteral("Nintendo 64")
        << QStringLiteral("Game Boy")
        << QStringLiteral("Game Boy Color")
        << QStringLiteral("Game Boy Advance")
        << QStringLiteral("Nintendo DS")
        << QStringLiteral("Nintendo 3DS")
        << QStringLiteral("Nintendo GameCube")
        << QStringLiteral("Nintendo Wii")
        << QStringLiteral("Nintendo Wii U")
        << QStringLiteral("Nintendo Switch")
        << QStringLiteral("Sega Master System")
        << QStringLiteral("Sega Genesis / Mega Drive")
        << QStringLiteral("Sega Game Gear")
        << QStringLiteral("Sega Saturn")
        << QStringLiteral("Sega Dreamcast")
        << QStringLiteral("PlayStation")
        << QStringLiteral("PlayStation 2")
        << QStringLiteral("PlayStation 3")
        << QStringLiteral("PlayStation Portable")
        << QStringLiteral("PlayStation Vita")
        << QStringLiteral("Xbox")
        << QStringLiteral("Xbox 360")
        << QStringLiteral("Atari 2600")
        << QStringLiteral("Atari 5200")
        << QStringLiteral("Atari 7800")
        << QStringLiteral("Atari Lynx")
        << QStringLiteral("PC Engine / TurboGrafx-16")
        << QStringLiteral("Neo Geo")
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

QStringList pathsForClassification(const QString &wanted)
{
    QStringList paths;
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files"));
    const QStringList groups = s.childGroups();
    for (int i = 0; i < groups.size(); ++i) {
        s.beginGroup(groups[i]);
        const QString classification = s.value(QStringLiteral("classification")).toString();
        const QString path = QDir::fromNativeSeparators(s.value(QStringLiteral("path")).toString());
        s.endGroup();
        if (classification.compare(wanted, Qt::CaseInsensitive) == 0 && !path.isEmpty() && QFileInfo(path).exists())
            paths << path;
    }
    s.endGroup();
    paths.removeDuplicates();
    paths.sort(Qt::CaseInsensitive);
    return paths;
}

}

RomScanner::RomScanner(QObject *parent) : QThread(parent), m_stop(false) {}
RomScanner::~RomScanner(){ requestStop(); wait(); }
void RomScanner::requestStop(){ m_stop = true; }

void RomScanner::run()
{
    const QFileInfoList drives = QDir::drives();
    const QStringList excludedRoots = excludedScanRoots();

    for (int d = 0; d < drives.size() && !m_stop; ++d) {
        const QString root = drives[d].absoluteFilePath();
        emit scanStatus(QStringLiteral("Scanning %1 for ROM files...").arg(QDir::toNativeSeparators(root)));

        // Use an explicit directory stack instead of QDirIterator::Subdirectories.
        // QDirIterator cannot prune a subtree after entering it, while the stack
        // lets Kadia completely avoid Windows, Program Files, caches, recycle
        // bins, recovery data, etc.
        QStringList pending;
        pending << root;

        while (!pending.isEmpty() && !m_stop) {
            const QString directory = pending.takeLast();
            if (isExcludedScanDirectory(directory, root, excludedRoots))
                continue;

            QDir dir(directory);
            const QFileInfoList entries = dir.entryInfoList(
                QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Readable,
                QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);

            for (int i = 0; i < entries.size() && !m_stop; ++i) {
                const QFileInfo &fi = entries[i];

                if (fi.isDir()) {
                    // Do not follow symlinks/junction-like entries into another
                    // part of the filesystem; this avoids duplicate scans and
                    // recursive directory cycles.
                    if (!fi.isSymLink() &&
                        !isExcludedScanDirectory(fi.absoluteFilePath(), root, excludedRoots))
                        pending << fi.absoluteFilePath();
                    continue;
                }

                if (!fi.isFile() || !candidateSuffix(fi.suffix()))
                    continue;

                const QString path = fi.absoluteFilePath();
                if (RomCatalog::isKnown(path))
                    continue;
                emit romDiscovered(path, hintForSuffix(fi.suffix()));
            }
        }
    }
    emit scanFinished();
}

RomClassificationDialog::RomClassificationDialog(const QString &path, const QString &hint, QWidget *parent)
    : QDialog(parent), m_path(path), m_pathLabel(new QLabel(this)), m_hintLabel(new QLabel(this)),
      m_systems(new QListWidget(this)), m_confirm(new QPushButton(QStringLiteral("Confirm"), this)),
      m_later(new QPushButton(QStringLiteral("Later"), this)), m_input(this), m_inputTimer(new QTimer(this))
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint); setModal(true); resize(720, 520); setStyleSheet(styleSheet());
    QLabel *title = new QLabel(QStringLiteral("New ROM detected"), this); QFont tf=title->font(); tf.setPixelSize(28); tf.setWeight(QFont::Light); title->setFont(tf);
    m_pathLabel->setText(QDir::toNativeSeparators(path)); m_pathLabel->setWordWrap(true);
    m_hintLabel->setText(QStringLiteral("Detected hint: %1. Confirm the console, choose Unknown for ambiguous images, or None to discard this file permanently.").arg(hint)); m_hintLabel->setWordWrap(true);
    m_systems->addItems(RomCatalog::systems());
    int row=m_systems->findItems(hint,Qt::MatchFixedString).isEmpty()?1:m_systems->row(m_systems->findItems(hint,Qt::MatchFixedString).first()); m_systems->setCurrentRow(row);
    QHBoxLayout *buttons=new QHBoxLayout; buttons->addStretch(); buttons->addWidget(m_confirm); buttons->addWidget(m_later);
    QVBoxLayout *layout=new QVBoxLayout(this); layout->setContentsMargins(28,22,28,22); layout->setSpacing(10); layout->addWidget(title); layout->addWidget(m_pathLabel); layout->addWidget(m_hintLabel); layout->addWidget(m_systems,1); layout->addLayout(buttons);
    connect(m_confirm,SIGNAL(clicked()),this,SLOT(confirmSelection())); connect(m_later,SIGNAL(clicked()),this,SLOT(deferSelection()));
    m_input.initialize(); connect(m_inputTimer,SIGNAL(timeout()),this,SLOT(pollController())); m_inputTimer->start(16);
    QRect target=parent?parent->frameGeometry():QApplication::desktop()->screenGeometry(QApplication::desktop()->primaryScreen()); move(target.center()-rect().center()); m_systems->setFocus();
}

QString RomClassificationDialog::selectedSystem() const { return m_selectedSystem; }
void RomClassificationDialog::confirmSelection(){QListWidgetItem *item=m_systems->currentItem();if(!item)return;m_selectedSystem=item->text();accept();}
void RomClassificationDialog::deferSelection(){reject();}
void RomClassificationDialog::pollController(){const InputManager::Action a=m_input.poll();if(a==InputManager::None)return;if(a==InputManager::Up)m_systems->setCurrentRow(qMax(0,m_systems->currentRow()-1));else if(a==InputManager::Down)m_systems->setCurrentRow(qMin(m_systems->count()-1,m_systems->currentRow()+1));else if(a==InputManager::Accept)confirmSelection();else if(a==InputManager::Back)deferSelection();}
