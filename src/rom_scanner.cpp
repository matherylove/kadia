#include "rom_scanner.h"
#include "rom_header_detector.h"
#include "screenscraper_client.h"

#include <QApplication>
#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopWidget>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
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

#ifdef Q_OS_WIN
    const QString publicRoot = environmentPath("PUBLIC");
    if (!publicRoot.isEmpty())
        addExcludedRoot(&roots, QDir(publicRoot).filePath(QStringLiteral("Documents/WinDS PRO")));
#endif

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
    if (name == QStringLiteral("appdata") || name == QStringLiteral("local settings") ||
        name == QStringLiteral("winds pro") || name == QStringLiteral("windspro"))
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

static QString romDialogStyleSheet()
{
    return QStringLiteral(
        "QDialog { background:transparent; }"
        "QFrame#glassPanel {"
        " background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(24,33,50,238), stop:0.52 rgba(10,16,28,230), stop:1 rgba(6,10,18,238));"
        " border:1px solid rgba(255,248,231,54); border-radius:18px; }"
        "QFrame#accentGlow { background:qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 rgba(255,240,200,8), stop:0.18 rgba(255,240,200,120), stop:0.52 rgba(166,194,255,78), stop:1 rgba(166,194,255,0)); border:none; border-radius:3px; }"
        "QLabel { color:#fff8e7; background:transparent; }"
        "QLabel#dialogTitle { color:rgba(255,248,231,0.94); }"
        "QLabel#dialogPath { color:rgba(255,248,231,0.68); }"
        "QLabel#dialogHint { color:rgba(255,248,231,0.78); }"
        "QListWidget { background:rgba(8,12,22,176); color:#fff8e7; border:1px solid rgba(255,248,231,46); border-radius:12px; outline:none; padding:5px; }"
        "QListWidget::item { padding:7px 10px; margin:1px 0; border-radius:8px; }"
        "QListWidget::item:selected { background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(73,83,109,216), stop:1 rgba(34,42,61,216)); color:#fff8e7; border:1px solid rgba(255,240,200,138); }"
        "QPushButton { color:#fff8e7; background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(40,48,69,220), stop:1 rgba(18,24,37,220)); border:1px solid rgba(255,248,231,68); border-radius:12px; padding:8px 22px; min-width:104px; }"
        "QPushButton:hover, QPushButton:focus { background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(71,82,112,235), stop:1 rgba(26,34,54,230)); border:1px solid rgba(255,240,200,160); }"
        "QPushButton:pressed { background:rgba(22,28,44,235); }" );
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
    const RomHeaderInfo header = RomHeaderDetector::detect(path);
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files/%1").arg(keyForPath(path)));
    s.setValue(QStringLiteral("path"), QDir::toNativeSeparators(path));
    s.setValue(QStringLiteral("classification"), system);
    s.setValue(QStringLiteral("classificationSource"), QStringLiteral("manual"));
    if (header.isRom) {
        s.setValue(QStringLiteral("detectedSystem"), header.system);
        s.setValue(QStringLiteral("internalTitle"), header.title);
        s.setValue(QStringLiteral("internalId"), header.internalId);
        s.setValue(QStringLiteral("format"), header.format);
        s.setValue(QStringLiteral("confidence"), header.confidence);
    }
    s.endGroup(); s.sync();
}

void saveDetectedRom(const QString &path, const QString &system, const QString &title,
                     const QString &internalIdValue, const QString &formatValue, int confidence)
{
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files/%1").arg(keyForPath(path)));
    s.setValue(QStringLiteral("path"), QDir::toNativeSeparators(path));
    s.setValue(QStringLiteral("classification"), system);
    s.setValue(QStringLiteral("classificationSource"), QStringLiteral("automatic"));
    s.setValue(QStringLiteral("detectedSystem"), system);
    s.setValue(QStringLiteral("internalTitle"), title.simplified());
    s.setValue(QStringLiteral("internalId"), internalIdValue.simplified());
    s.setValue(QStringLiteral("format"), formatValue.simplified());
    s.setValue(QStringLiteral("confidence"), confidence);
    s.endGroup();
    s.sync();
}

void removeEntry(const QString &path)
{
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files"));
    s.remove(keyForPath(path));
    s.endGroup();
    s.sync();
}

QString internalTitle(const QString &path)
{
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files/%1").arg(keyForPath(path)));
    QString title = s.value(QStringLiteral("internalTitle")).toString();
    s.endGroup();
    if (!title.isEmpty())
        return title;
    const RomHeaderInfo header = RomHeaderDetector::detect(path);
    return header.title;
}

QString displayTitle(const QString &path)
{
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files/%1").arg(keyForPath(path)));
    const QString value = s.value(QStringLiteral("scrapedTitle")).toString().simplified();
    s.endGroup();
    if (!value.isEmpty())
        return value;
    return internalTitle(path);
}

QString internalId(const QString &path)
{
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files/%1").arg(keyForPath(path)));
    const QString value = s.value(QStringLiteral("internalId")).toString();
    s.endGroup();
    return value;
}

QString description(const QString &path)
{
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files/%1").arg(keyForPath(path)));
    const QString value = s.value(QStringLiteral("scrapedDescription")).toString().simplified();
    s.endGroup();
    return value;
}

QString coverArtPath(const QString &path)
{
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files/%1").arg(keyForPath(path)));
    const QString value = s.value(QStringLiteral("coverArtPath")).toString();
    s.endGroup();
    return value;
}

bool hasScreenScraperMetadata(const QString &path)
{
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files/%1").arg(keyForPath(path)));
    const bool hasTitle = !s.value(QStringLiteral("scrapedTitle")).toString().trimmed().isEmpty();
    const bool hasDescription = !s.value(QStringLiteral("scrapedDescription")).toString().trimmed().isEmpty();
    const bool hasCover = !s.value(QStringLiteral("coverArtPath")).toString().trimmed().isEmpty();
    s.endGroup();
    return hasTitle || hasDescription || hasCover;
}

void saveScreenScraperMetadata(const QString &path, const QString &title,
                               const QString &descriptionValue, const QString &coverPath,
                               const QString &source)
{
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files/%1").arg(keyForPath(path)));
    if (!title.simplified().isEmpty())
        s.setValue(QStringLiteral("scrapedTitle"), title.simplified());
    if (!descriptionValue.simplified().isEmpty())
        s.setValue(QStringLiteral("scrapedDescription"), descriptionValue.simplified());
    if (!coverPath.trimmed().isEmpty())
        s.setValue(QStringLiteral("coverArtPath"), QDir::toNativeSeparators(coverPath));
    if (!source.trimmed().isEmpty())
        s.setValue(QStringLiteral("metadataSource"), source.trimmed());
    s.setValue(QStringLiteral("metadataUpdatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    s.endGroup();
    s.sync();
}

QString format(const QString &path)
{
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files/%1").arg(keyForPath(path)));
    const QString value = s.value(QStringLiteral("format")).toString();
    s.endGroup();
    return value;
}

bool isAutomaticDetection(const QString &path)
{
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files/%1").arg(keyForPath(path)));
    const QString source = s.value(QStringLiteral("classificationSource")).toString();
    s.endGroup();
    return source.compare(QStringLiteral("automatic"), Qt::CaseInsensitive) == 0;
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

QStringList recognizedPaths()
{
    QStringList paths;
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files"));
    const QStringList groups = s.childGroups();
    for (int i = 0; i < groups.size(); ++i) {
        s.beginGroup(groups[i]);
        const QString classificationValue = s.value(QStringLiteral("classification")).toString();
        const QString path = QDir::fromNativeSeparators(s.value(QStringLiteral("path")).toString());
        s.endGroup();
        if (classificationValue.isEmpty() ||
            classificationValue.compare(QStringLiteral("Unknown"), Qt::CaseInsensitive) == 0 ||
            classificationValue.compare(QStringLiteral("None (ignore)"), Qt::CaseInsensitive) == 0)
            continue;
        if (!path.isEmpty() && QFileInfo(path).exists())
            paths << path;
    }
    s.endGroup();
    paths.removeDuplicates();
    paths.sort(Qt::CaseInsensitive);
    return paths;
}

}


static void maybeEnrichWithScreenScraper(const QString &path, const QString &system,
                                         const QString &headerTitle, const QString &internalId)
{
    if (system.trimmed().isEmpty() ||
        system.compare(QStringLiteral("Unknown"), Qt::CaseInsensitive) == 0 ||
        system.compare(QStringLiteral("None (ignore)"), Qt::CaseInsensitive) == 0)
        return;
    if (RomCatalog::hasScreenScraperMetadata(path) || !ScreenScraperClient::isConfigured())
        return;

    const ScreenScraperMetadata meta = ScreenScraperClient::fetchMetadata(path, system, headerTitle, internalId);
    if (meta.success) {
        RomCatalog::saveScreenScraperMetadata(path, meta.title, meta.description,
                                              meta.coverPath, meta.source);
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
        emit scanStatus(QStringLiteral("Scanning %1 for ROM headers...").arg(QDir::toNativeSeparators(root)));

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

                if (!fi.isFile())
                    continue;

                const QString path = fi.absoluteFilePath();

                // Existing ignored/unknown choices are never prompted again.
                // Existing recognized entries are revalidated with the current
                // structural detector so old false positives disappear after a
                // detector update (for example RIFF/WAVE files once mistaken
                // for a cartridge by weak heuristics).
                if (RomCatalog::isKnown(path)) {
                    const QString knownSystem = RomCatalog::classification(path);
                    if (knownSystem.compare(QStringLiteral("Unknown"), Qt::CaseInsensitive) == 0 ||
                        knownSystem.compare(QStringLiteral("None (ignore)"), Qt::CaseInsensitive) == 0)
                        continue;

                    const RomHeaderInfo knownHeader = RomHeaderDetector::detect(path);
                    if (!knownHeader.isRom) {
                        RomCatalog::removeEntry(path);
                        emit romRecognized(path, QString(), QString());
                    } else if (RomCatalog::isAutomaticDetection(path)) {
                        if (knownHeader.system.isEmpty() ||
                            knownHeader.system.compare(QStringLiteral("Unknown"), Qt::CaseInsensitive) == 0 ||
                            knownHeader.confidence < 90) {
                            RomCatalog::removeEntry(path);
                            emit romRecognized(path, QString(), QString());
                        } else {
                            RomCatalog::saveDetectedRom(path, knownHeader.system, knownHeader.title,
                                                        knownHeader.internalId, knownHeader.format,
                                                        knownHeader.confidence);
                        }
                    }
                    maybeEnrichWithScreenScraper(path, knownSystem,
                                                 RomCatalog::internalTitle(path),
                                                 RomCatalog::internalId(path));
                    continue;
                }

                // Recognition is entirely structural.  The extension and the
                // filename are never used to decide whether this is a ROM.
                const RomHeaderInfo header = RomHeaderDetector::detect(path);
                if (!header.isRom)
                    continue;

                const bool consoleKnown = !header.system.isEmpty() &&
                    header.system.compare(QStringLiteral("Unknown"), Qt::CaseInsensitive) != 0 &&
                    header.confidence >= 90;

                if (consoleKnown) {
                    RomCatalog::saveDetectedRom(path, header.system, header.title,
                                                header.internalId, header.format,
                                                header.confidence);
                    maybeEnrichWithScreenScraper(path, header.system, header.title,
                                                 header.internalId);
                    emit romRecognized(path, header.system,
                                       RomCatalog::displayTitle(path));
                } else {
                    // Only unresolved structural ROM detections reach the user.
                    emit romDiscovered(path, QStringLiteral("Unknown"));
                }
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
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setModal(true);
    resize(760, 560);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setStyleSheet(romDialogStyleSheet());

    QLabel *title = new QLabel(QStringLiteral("New ROM detected"), this);
    title->setObjectName(QStringLiteral("dialogTitle"));
    QFont tf = title->font(); tf.setPixelSize(28); tf.setWeight(QFont::Light); title->setFont(tf);

    m_pathLabel->setObjectName(QStringLiteral("dialogPath"));
    m_pathLabel->setText(QDir::toNativeSeparators(path));
    m_pathLabel->setWordWrap(true);
    QFont pf = m_pathLabel->font(); pf.setPixelSize(12); m_pathLabel->setFont(pf);

    m_hintLabel->setObjectName(QStringLiteral("dialogHint"));
    const RomHeaderInfo header = RomHeaderDetector::detect(path);
    const QString detectedTitle = header.title.isEmpty()
        ? QStringLiteral("No standardized internal title is present in this ROM format")
        : header.title;
    const QString detectedFormat = header.format.isEmpty() ? QStringLiteral("ROM image") : header.format;
    m_hintLabel->setText(QStringLiteral("Internal title: %1\nDetected system: %2  |  Format: %3\nConfirm the console, choose Unknown if the detection is ambiguous, or None to discard this file permanently.")
                         .arg(detectedTitle, hint, detectedFormat));
    m_hintLabel->setWordWrap(true);
    QFont hf = m_hintLabel->font(); hf.setPixelSize(13); m_hintLabel->setFont(hf);

    m_systems->addItems(RomCatalog::systems());
    QList<QListWidgetItem*> exact = m_systems->findItems(hint, Qt::MatchFixedString);
    int row = exact.isEmpty() ? 1 : m_systems->row(exact.first());
    m_systems->setCurrentRow(row);

    QFrame *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("glassPanel"));
    QFrame *accent = new QFrame(panel);
    accent->setObjectName(QStringLiteral("accentGlow"));
    accent->setFixedHeight(6);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    buttons->addStretch();
    buttons->addWidget(m_confirm);
    buttons->addWidget(m_later);

    QVBoxLayout *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(26, 18, 26, 24);
    panelLayout->setSpacing(10);
    panelLayout->addWidget(accent);
    panelLayout->addSpacing(4);
    panelLayout->addWidget(title);
    panelLayout->addWidget(m_pathLabel);
    panelLayout->addWidget(m_hintLabel);
    panelLayout->addWidget(m_systems, 1);
    panelLayout->addLayout(buttons);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(panel);

    connect(m_confirm, SIGNAL(clicked()), this, SLOT(confirmSelection()));
    connect(m_later, SIGNAL(clicked()), this, SLOT(deferSelection()));
    m_input.initialize();
    connect(m_inputTimer, SIGNAL(timeout()), this, SLOT(pollController()));
    m_inputTimer->start(16);
    QRect target = parent ? parent->frameGeometry() : QApplication::desktop()->screenGeometry(QApplication::desktop()->primaryScreen());
    move(target.center() - rect().center());
    m_systems->setFocus();
}

QString RomClassificationDialog::selectedSystem() const { return m_selectedSystem; }
void RomClassificationDialog::confirmSelection(){QListWidgetItem *item=m_systems->currentItem();if(!item)return;m_selectedSystem=item->text();accept();}
void RomClassificationDialog::deferSelection(){reject();}
void RomClassificationDialog::pollController(){const InputManager::Action a=m_input.poll();if(a==InputManager::None)return;if(a==InputManager::Up)m_systems->setCurrentRow(qMax(0,m_systems->currentRow()-1));else if(a==InputManager::Down)m_systems->setCurrentRow(qMin(m_systems->count()-1,m_systems->currentRow()+1));else if(a==InputManager::Accept)confirmSelection();else if(a==InputManager::Back)deferSelection();}
