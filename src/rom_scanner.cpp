#include "rom_scanner.h"
#include "rom_header_detector.h"

#include <QApplication>
#include <QByteArray>
#include <QCryptographicHash>
#include <QCloseEvent>
#include <QDesktopWidget>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QListWidget>
#include <QPushButton>
#include <QProgressBar>
#include <QSettings>
#include <QSet>
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

static QString scanCachePath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) base = QDir::homePath() + QStringLiteral("/.mathery-kadia");
    QDir().mkpath(base);
    return QDir(base).filePath(QStringLiteral("rom-scan-cache.ini"));
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

struct ScanCacheEntry
{
    ScanCacheEntry()
        : size(-1)
        , modifiedMs(-1)
        , confidence(0)
    {
    }

    QString path;
    qint64 size;
    qint64 modifiedMs;
    QString state;
    QString system;
    QString title;
    QString internalId;
    QString format;
    int confidence;
};

static const int kScanCacheVersion = 2;

static qint64 fileModifiedMs(const QFileInfo &fi)
{
    const QDateTime modified = fi.lastModified();
    return modified.isValid() ? modified.toMSecsSinceEpoch() : -1;
}

static bool cacheMatchesFile(const ScanCacheEntry &entry, const QFileInfo &fi)
{
    const qint64 modified = fileModifiedMs(fi);
    return entry.size >= 0 && entry.modifiedMs >= 0 && modified >= 0 &&
           entry.size == fi.size() && entry.modifiedMs == modified;
}

static QHash<QString, ScanCacheEntry> loadScanCache()
{
    QHash<QString, ScanCacheEntry> cache;
    QSettings s(scanCachePath(), QSettings::IniFormat);
    if (s.value(QStringLiteral("meta/version"), 0).toInt() != kScanCacheVersion)
        return cache;

    s.beginGroup(QStringLiteral("files"));
    const QStringList groups = s.childGroups();
    for (int i = 0; i < groups.size(); ++i) {
        s.beginGroup(groups.at(i));
        ScanCacheEntry entry;
        entry.path = QDir::fromNativeSeparators(s.value(QStringLiteral("path")).toString());
        entry.size = s.value(QStringLiteral("size"), -1).toLongLong();
        entry.modifiedMs = s.value(QStringLiteral("modifiedMs"), -1).toLongLong();
        entry.state = s.value(QStringLiteral("state")).toString();
        entry.system = s.value(QStringLiteral("system")).toString();
        entry.title = s.value(QStringLiteral("title")).toString();
        entry.internalId = s.value(QStringLiteral("internalId")).toString();
        entry.format = s.value(QStringLiteral("format")).toString();
        entry.confidence = s.value(QStringLiteral("confidence"), 0).toInt();
        s.endGroup();

        const QString key = normalizedScanPath(entry.path);
        if (!key.isEmpty() && !entry.state.isEmpty())
            cache.insert(key, entry);
    }
    s.endGroup();
    return cache;
}

static void saveScanCache(const QHash<QString, ScanCacheEntry> &cache)
{
    QSettings s(scanCachePath(), QSettings::IniFormat);
    s.clear();
    s.setValue(QStringLiteral("meta/version"), kScanCacheVersion);
    s.beginGroup(QStringLiteral("files"));

    QHash<QString, ScanCacheEntry>::const_iterator it = cache.constBegin();
    for (; it != cache.constEnd(); ++it) {
        const ScanCacheEntry &entry = it.value();
        if (entry.path.isEmpty() || entry.state.isEmpty())
            continue;
        s.beginGroup(keyForPath(entry.path));
        s.setValue(QStringLiteral("path"), QDir::toNativeSeparators(entry.path));
        s.setValue(QStringLiteral("size"), entry.size);
        s.setValue(QStringLiteral("modifiedMs"), entry.modifiedMs);
        s.setValue(QStringLiteral("state"), entry.state);
        s.setValue(QStringLiteral("system"), entry.system);
        s.setValue(QStringLiteral("title"), entry.title);
        s.setValue(QStringLiteral("internalId"), entry.internalId);
        s.setValue(QStringLiteral("format"), entry.format);
        s.setValue(QStringLiteral("confidence"), entry.confidence);
        s.endGroup();
    }
    s.endGroup();
    s.sync();
}

static ScanCacheEntry makeCacheEntry(const QFileInfo &fi, const QString &state,
                                     const RomHeaderInfo *header = 0)
{
    ScanCacheEntry entry;
    entry.path = fi.absoluteFilePath();
    entry.size = fi.size();
    entry.modifiedMs = fileModifiedMs(fi);
    entry.state = state;
    if (header) {
        entry.system = header->system;
        entry.title = header->title;
        entry.internalId = header->internalId;
        entry.format = header->format;
        entry.confidence = header->confidence;
    }
    return entry;
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
        "QLabel#dialogTitle { color:rgba(255,248,231,240); }"
        "QLabel#dialogPath { color:rgba(255,248,231,174); }"
        "QLabel#dialogHint { color:rgba(255,248,231,199); }"
        "QListWidget { background:rgba(8,12,22,176); color:#fff8e7; border:1px solid rgba(255,248,231,46); border-radius:12px; outline:none; padding:5px; }"
        "QListWidget::item { padding:7px 10px; margin:1px 0; border-radius:8px; }"
        "QListWidget::item:selected { background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(73,83,109,216), stop:1 rgba(34,42,61,216)); color:#fff8e7; border:1px solid rgba(255,240,200,138); }"
        "QPushButton { color:#fff8e7; background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(40,48,69,220), stop:1 rgba(18,24,37,220)); border:1px solid rgba(255,248,231,68); border-radius:12px; padding:8px 22px; min-width:104px; }"
        "QPushButton:hover, QPushButton:focus { background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(71,82,112,235), stop:1 rgba(26,34,54,230)); border:1px solid rgba(255,240,200,160); }"
        "QPushButton:pressed { background:rgba(22,28,44,235); }" );
}

static QString scanProgressStyleSheet()
{
    return QStringLiteral(
        "QDialog { background:transparent; }"
        "QFrame#glassPanel { background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(24,33,50,242), stop:0.50 rgba(10,16,28,234), stop:1 rgba(6,10,18,242)); border:1px solid rgba(255,248,231,58); border-radius:18px; }"
        "QFrame#accentGlow { background:qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 rgba(255,240,200,8), stop:0.18 rgba(255,240,200,120), stop:0.52 rgba(166,194,255,78), stop:1 rgba(166,194,255,0)); border:none; border-radius:3px; }"
        "QLabel { color:#fff8e7; background:transparent; }"
        "QLabel#scanTitle { color:rgba(255,248,231,240); }"
        "QLabel#scanStatus { color:rgba(255,248,231,209); }"
        "QLabel#scanPath { color:rgba(255,248,231,148); }"
        "QLabel#progressCaption { color:rgba(255,248,231,168); }"
        "QProgressBar { border:1px solid rgba(255,248,231,52); border-radius:8px; padding:1px; background:rgba(8,13,22,190); color:#fff8e7; text-align:center; min-height:20px; }"
        "QProgressBar::chunk { border-radius:6px; background:qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 rgba(113,126,160,235), stop:0.48 rgba(184,176,158,238), stop:1 rgba(255,240,200,252)); }"
        "QPushButton { color:#fff8e7; background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(40,48,69,224), stop:1 rgba(18,24,37,224)); border:1px solid rgba(255,248,231,68); border-radius:12px; padding:8px 22px; min-width:112px; }"
        "QPushButton:hover, QPushButton:focus { background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(71,82,112,238), stop:1 rgba(26,34,54,234)); border:1px solid rgba(255,240,200,164); }"
        "QPushButton:disabled { color:rgba(255,248,231,97); border-color:rgba(255,248,231,32); background:rgba(18,24,37,150); }" );
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
    // Do not perform header detection here. This function is called from the
    // GUI after the user answers the unresolved-ROM dialog; all binary analysis
    // was already completed by RomScanner's worker thread.
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files/%1").arg(keyForPath(path)));
    s.setValue(QStringLiteral("path"), QDir::toNativeSeparators(path));
    s.setValue(QStringLiteral("classification"), system);
    s.setValue(QStringLiteral("classificationSource"), QStringLiteral("manual"));
    s.endGroup();
    s.sync();
}

void saveInspectionMetadata(const QString &path, const QString &detectedSystem,
                            const QString &title, const QString &internalIdValue,
                            const QString &formatValue, int confidence)
{
    // Store the worker's inspection result without marking the file as already
    // classified. If the user chooses Later, Kadia may ask again next scan.
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files/%1").arg(keyForPath(path)));
    s.setValue(QStringLiteral("path"), QDir::toNativeSeparators(path));
    s.setValue(QStringLiteral("detectedSystem"), detectedSystem.simplified());
    s.setValue(QStringLiteral("internalTitle"), title.simplified());
    s.setValue(QStringLiteral("internalId"), internalIdValue.simplified());
    s.setValue(QStringLiteral("format"), formatValue.simplified());
    s.setValue(QStringLiteral("confidence"), confidence);
    s.endGroup();
    s.sync();
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
    return title;
}

QString internalId(const QString &path)
{
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files/%1").arg(keyForPath(path)));
    const QString value = s.value(QStringLiteral("internalId")).toString();
    s.endGroup();
    return value;
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

RomScanner::RomScanner(QObject *parent) : QThread(parent), m_stop(0) {}
RomScanner::~RomScanner(){ requestStop(); wait(); }
void RomScanner::requestStop(){ m_stop.storeRelease(1); }

void RomScanner::run()
{
    m_stop.storeRelease(0);
    const QFileInfoList drives = QDir::drives();
    const QStringList excludedRoots = excludedScanRoots();

    // This cache is loaded once in the worker. File contents are only reopened
    // when size or last-write time differs from the last completed scan.
    QHash<QString, ScanCacheEntry> scanCache = loadScanCache();
    QStringList candidates;
    QStringList cachedUnresolved;
    QElapsedTimer discoveryUiTimer;
    discoveryUiTimer.start();
    int cachedSkipped = 0;
    int directoryThrottle = 0;

    // Phase 1: directory walk + extension prefilter + cheap metadata comparison.
    // No candidate file content is opened here. Known unchanged files (both ROM
    // and non-ROM) are excluded from structural analysis.
    for (int d = 0; d < drives.size() && !m_stop.loadAcquire(); ++d) {
        const QString root = drives[d].absoluteFilePath();
        QStringList pending;
        pending << root;

        while (!pending.isEmpty() && !m_stop.loadAcquire()) {
            const QString directory = pending.takeLast();
            if (isExcludedScanDirectory(directory, root, excludedRoots))
                continue;

            if (discoveryUiTimer.elapsed() >= 100) {
                emit discoveryProgress(QDir::toNativeSeparators(directory), candidates.size());
                discoveryUiTimer.restart();
            }

            QDir dir(directory);
            const QFileInfoList entries = dir.entryInfoList(
                QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Readable,
                QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);

            for (int i = 0; i < entries.size() && !m_stop.loadAcquire(); ++i) {
                const QFileInfo &fi = entries.at(i);
                if (fi.isDir()) {
                    if (!fi.isSymLink() &&
                        !isExcludedScanDirectory(fi.absoluteFilePath(), root, excludedRoots))
                        pending << fi.absoluteFilePath();
                    continue;
                }
                if (!fi.isFile())
                    continue;

                const QString path = fi.absoluteFilePath();
                if (!RomHeaderDetector::isCandidatePath(path))
                    continue;

                const QString cacheKey = normalizedScanPath(path);
                const QHash<QString, ScanCacheEntry>::const_iterator cached = scanCache.constFind(cacheKey);
                if (cached != scanCache.constEnd() && cacheMatchesFile(cached.value(), fi)) {
                    // An unresolved file may have been manually classified after
                    // the previous scan. Respect that decision without reopening it.
                    if (cached.value().state == QStringLiteral("unresolved")) {
                        bool needsPrompt = true;
                        if (RomCatalog::isKnown(path)) {
                            const QString manual = RomCatalog::classification(path);
                            if (!manual.isEmpty())
                                needsPrompt = false;
                        }
                        if (needsPrompt)
                            cachedUnresolved << path;
                    }
                    ++cachedSkipped;
                    continue;
                }

                candidates << path;
                if (discoveryUiTimer.elapsed() >= 100) {
                    emit discoveryProgress(QDir::toNativeSeparators(path), candidates.size());
                    discoveryUiTimer.restart();
                }

                if ((i & 127) == 127)
                    QThread::msleep(1);
            }

            if ((++directoryThrottle & 7) == 0)
                QThread::msleep(1);
        }
    }

    candidates.removeDuplicates();
    cachedUnresolved.removeDuplicates();
    const int total = candidates.size();
    emit discoveryProgress(total > 0 ? QDir::toNativeSeparators(candidates.last())
                                     : QStringLiteral("ROM scan index is up to date"), total);
    emit analysisStarted(total);

    int recognizedCount = 0;
    int unresolvedCount = 0;
    int testedCandidates = 0;

    for (int index = 0; index < total && !m_stop.loadAcquire(); ++index) {
        const QString path = candidates.at(index);
        const QFileInfo fi(path);
        const int baseOverall = total > 0 ? (index * 100) / total : 100;
        emit fileProgress(path, 0, baseOverall, QStringLiteral("Preparing candidate"), index + 1, total);

        QElapsedTimer fileUiTimer;
        fileUiTimer.start();
        int lastReportedPercent = -1;
        const auto report = [this, path, index, total, &fileUiTimer, &lastReportedPercent](int filePercent, const QString &stage) {
            if (m_stop.loadAcquire())
                return;
            const int boundedFile = qBound(0, filePercent, 100);
            if (boundedFile < 100 && boundedFile != 0 && fileUiTimer.elapsed() < 50)
                return;
            if (boundedFile == lastReportedPercent && boundedFile != 100)
                return;
            lastReportedPercent = boundedFile;
            fileUiTimer.restart();
            const int overall = total > 0
                ? qBound(0, ((index * 100) + boundedFile) / total, 100)
                : 100;
            emit fileProgress(path, boundedFile, overall, stage, index + 1, total);
        };

        // User decisions Unknown/None are authoritative and do not require a
        // content read, even when this is the first scan-cache generation.
        if (RomCatalog::isKnown(path)) {
            const QString knownSystem = RomCatalog::classification(path);
            if (knownSystem.compare(QStringLiteral("Unknown"), Qt::CaseInsensitive) == 0 ||
                knownSystem.compare(QStringLiteral("None (ignore)"), Qt::CaseInsensitive) == 0) {
                ScanCacheEntry entry = makeCacheEntry(fi, QStringLiteral("classified"));
                entry.system = knownSystem;
                scanCache.insert(normalizedScanPath(path), entry);
                report(100, QStringLiteral("Already classified by user"));
                continue;
            }
        }

        ++testedCandidates;

        // Every byte/header read below happens on this worker thread.
        const RomHeaderInfo header = RomHeaderDetector::detect(path, report);

        if (RomCatalog::isKnown(path)) {
            const QString knownSystem = RomCatalog::classification(path);
            if (!header.isRom) {
                RomCatalog::removeEntry(path);
                scanCache.insert(normalizedScanPath(path), makeCacheEntry(fi, QStringLiteral("nonrom"), &header));
            } else if (RomCatalog::isAutomaticDetection(path)) {
                if (header.system.isEmpty() ||
                    header.system.compare(QStringLiteral("Unknown"), Qt::CaseInsensitive) == 0 ||
                    header.confidence < 90) {
                    RomCatalog::removeEntry(path);
                    scanCache.insert(normalizedScanPath(path), makeCacheEntry(fi, QStringLiteral("unresolved"), &header));
                    ++unresolvedCount;
                    const QString hint = header.system.isEmpty() ? QStringLiteral("Unknown") : header.system;
                    emit romDiscovered(path, hint, header.title, header.format);
                } else {
                    RomCatalog::saveDetectedRom(path, header.system, header.title,
                                                header.internalId, header.format,
                                                header.confidence);
                    scanCache.insert(normalizedScanPath(path), makeCacheEntry(fi, QStringLiteral("rom"), &header));
                    ++recognizedCount;
                }
            } else {
                // Manual recognized classification stays authoritative. Only the
                // metadata refresh occurs in this worker when the file changed.
                RomCatalog::saveInspectionMetadata(path, header.system, header.title,
                                                   header.internalId, header.format,
                                                   header.confidence);
                scanCache.insert(normalizedScanPath(path), makeCacheEntry(fi, QStringLiteral("rom"), &header));
                if (!knownSystem.isEmpty())
                    ++recognizedCount;
            }
            report(100, QStringLiteral("Finished"));
            QThread::msleep(1);
            continue;
        }

        if (!header.isRom) {
            scanCache.insert(normalizedScanPath(path), makeCacheEntry(fi, QStringLiteral("nonrom"), &header));
            report(100, QStringLiteral("No ROM structure detected"));
            QThread::msleep(1);
            continue;
        }

        const bool consoleKnown = !header.system.isEmpty() &&
            header.system.compare(QStringLiteral("Unknown"), Qt::CaseInsensitive) != 0 &&
            header.confidence >= 90;

        if (consoleKnown) {
            RomCatalog::saveDetectedRom(path, header.system, header.title,
                                        header.internalId, header.format,
                                        header.confidence);
            scanCache.insert(normalizedScanPath(path), makeCacheEntry(fi, QStringLiteral("rom"), &header));
            ++recognizedCount;
            // Do not send a per-ROM model-update signal to the GUI. Thousands of
            // queued library rebuilds were starving Kadia's render/message loop.
        } else {
            RomCatalog::saveInspectionMetadata(path, header.system, header.title,
                                               header.internalId, header.format,
                                               header.confidence);
            scanCache.insert(normalizedScanPath(path), makeCacheEntry(fi, QStringLiteral("unresolved"), &header));
            ++unresolvedCount;
            const QString hint = header.system.isEmpty() ? QStringLiteral("Unknown") : header.system;
            emit romDiscovered(path, hint, header.title, header.format);
        }
        report(100, QStringLiteral("Finished"));
        QThread::msleep(1);
    }

    // Unchanged unresolved files are not reopened, but they can still be shown
    // to the user after the scan if no manual classification exists yet.
    if (!m_stop.loadAcquire()) {
        for (int i = 0; i < cachedUnresolved.size(); ++i) {
            const QString path = cachedUnresolved.at(i);
            const QHash<QString, ScanCacheEntry>::const_iterator it = scanCache.constFind(normalizedScanPath(path));
            if (it == scanCache.constEnd())
                continue;
            const ScanCacheEntry &entry = it.value();
            const QString hint = entry.system.isEmpty() ? QStringLiteral("Unknown") : entry.system;
            ++unresolvedCount;
            emit romDiscovered(path, hint, entry.title, entry.format);
        }
    }

    const bool cancelled = m_stop.loadAcquire() != 0;
    if (!cancelled) {
        const QString lastPath = total > 0 ? candidates.last() : QStringLiteral("ROM scan cache");
        emit fileProgress(lastPath, 100, 100,
                          QStringLiteral("Saving scan results"), total, total);
        saveScanCache(scanCache);
    }

    Q_UNUSED(cachedSkipped);
    emit scanSummary(recognizedCount, unresolvedCount, testedCandidates, cancelled);
    emit scanFinished();
}

RomScanProgressDialog::RomScanProgressDialog(QWidget *parent)
    : QDialog(parent)
    , m_title(new QLabel(this))
    , m_status(new QLabel(this))
    , m_path(new QLabel(this))
    , m_fileCaption(new QLabel(this))
    , m_overallCaption(new QLabel(this))
    , m_fileProgress(new QProgressBar(this))
    , m_overallProgress(new QProgressBar(this))
    , m_action(new QPushButton(QStringLiteral("Cancel"), this))
    , m_input(this)
    , m_inputTimer(new QTimer(this))
    , m_completed(false)
    , m_cancelled(false)
    , m_cancelPending(false)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    // Keep Kadia's main native/D3D window enabled while the scan runs. Making
    // this window modal disabled the owner HWND on Windows and could make the
    // shell look hung even though the worker dialog continued updating.
    setModal(false);
    setWindowModality(Qt::NonModal);
    setFixedSize(680, 360);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setStyleSheet(scanProgressStyleSheet());

    m_title->setObjectName(QStringLiteral("scanTitle"));
    m_status->setObjectName(QStringLiteral("scanStatus"));
    m_path->setObjectName(QStringLiteral("scanPath"));
    m_fileCaption->setObjectName(QStringLiteral("progressCaption"));
    m_overallCaption->setObjectName(QStringLiteral("progressCaption"));

    m_title->setText(QStringLiteral("Searching for ROMs"));
    QFont tf = m_title->font(); tf.setPixelSize(28); tf.setWeight(QFont::Light); m_title->setFont(tf);
    m_status->setText(QStringLiteral("Looking for new or changed ROM candidates..."));
    QFont sf = m_status->font(); sf.setPixelSize(15); m_status->setFont(sf);
    m_path->setText(QStringLiteral("Preparing scanner..."));
    m_path->setWordWrap(true);

    m_fileCaption->setText(QStringLiteral("Current file"));
    m_overallCaption->setText(QStringLiteral("Overall scan"));

    // During directory discovery the total number of candidate files is not
    // known yet, so both bars are indeterminate. They become true percentages
    // before any file content/header analysis starts.
    m_fileProgress->setRange(0, 0);
    m_overallProgress->setRange(0, 0);
    m_fileProgress->setTextVisible(false);
    m_overallProgress->setTextVisible(false);

    QFrame *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("glassPanel"));
    QFrame *accent = new QFrame(panel);
    accent->setObjectName(QStringLiteral("accentGlow"));
    accent->setFixedHeight(6);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_action);

    QVBoxLayout *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(28, 18, 28, 24);
    panelLayout->setSpacing(9);
    panelLayout->addWidget(accent);
    panelLayout->addSpacing(3);
    panelLayout->addWidget(m_title);
    panelLayout->addWidget(m_status);
    panelLayout->addWidget(m_path);
    panelLayout->addSpacing(5);
    panelLayout->addWidget(m_fileCaption);
    panelLayout->addWidget(m_fileProgress);
    panelLayout->addSpacing(3);
    panelLayout->addWidget(m_overallCaption);
    panelLayout->addWidget(m_overallProgress);
    panelLayout->addSpacing(4);
    panelLayout->addLayout(buttons);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(panel);

    connect(m_action, SIGNAL(clicked()), this, SLOT(actionPressed()));
    m_input.initialize();
    connect(m_inputTimer, SIGNAL(timeout()), this, SLOT(pollController()));
    m_inputTimer->start(16);

    QRect target = parent ? parent->frameGeometry()
                          : QApplication::desktop()->screenGeometry(QApplication::desktop()->primaryScreen());
    move(target.center() - rect().center());
    m_action->setFocus();
}

bool RomScanProgressDialog::scanCompleted() const { return m_completed; }
bool RomScanProgressDialog::scanCancelled() const { return m_cancelled; }

void RomScanProgressDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
        event->key() == Qt::Key_Escape || event->key() == Qt::Key_Backspace) {
        actionPressed();
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void RomScanProgressDialog::closeEvent(QCloseEvent *event)
{
    if (!m_completed) {
        actionPressed();
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void RomScanProgressDialog::onDiscoveryProgress(const QString &currentPath, int candidatesFound)
{
    if (m_completed)
        return;
    m_status->setText(QStringLiteral("Indexing ROM candidates - %1 new/changed found").arg(candidatesFound));
    m_path->setText(currentPath);
}

void RomScanProgressDialog::onAnalysisStarted(int totalCandidates)
{
    if (m_completed)
        return;
    m_fileProgress->setRange(0, 100);
    m_overallProgress->setRange(0, 100);
    m_fileProgress->setTextVisible(true);
    m_overallProgress->setTextVisible(true);
    m_fileProgress->setValue(0);
    m_overallProgress->setValue(totalCandidates == 0 ? 100 : 0);
    if (totalCandidates == 0) {
        m_status->setText(QStringLiteral("No new or changed ROM files require analysis."));
        m_path->setText(QStringLiteral("Previously scanned unchanged files are being skipped from the cache."));
    } else {
        m_status->setText(QStringLiteral("Analyzing %1 new or changed candidates in the background...").arg(totalCandidates));
    }
}

void RomScanProgressDialog::onFileProgress(const QString &path, int filePercent, int overallPercent,
                                           const QString &stage, int currentIndex, int totalCandidates)
{
    if (m_completed)
        return;
    m_fileProgress->setRange(0, 100);
    m_overallProgress->setRange(0, 100);
    m_fileProgress->setTextVisible(true);
    m_overallProgress->setTextVisible(true);
    m_fileProgress->setValue(qBound(0, filePercent, 100));
    m_overallProgress->setValue(qBound(0, overallPercent, 100));
    m_status->setText(QStringLiteral("%1  -  file %2 of %3").arg(stage).arg(currentIndex).arg(totalCandidates));
    m_path->setText(QDir::toNativeSeparators(path));
}

void RomScanProgressDialog::onScanSummary(int recognizedCount, int unresolvedCount,
                                          int testedCandidates, bool cancelled)
{
    m_completed = true;
    m_cancelled = cancelled;
    m_cancelPending = false;
    m_fileProgress->setRange(0, 100);
    m_overallProgress->setRange(0, 100);
    m_fileProgress->setValue(100);
    m_overallProgress->setValue(cancelled ? m_overallProgress->value() : 100);
    m_fileProgress->setTextVisible(true);
    m_overallProgress->setTextVisible(true);

    if (cancelled) {
        m_title->setText(QStringLiteral("ROM search cancelled"));
        m_status->setText(QStringLiteral("Kadia stopped the background scan."));
        m_path->setText(QStringLiteral("%1 candidate files were tested before cancellation.").arg(testedCandidates));
    } else if (testedCandidates == 0 && unresolvedCount == 0) {
        m_title->setText(QStringLiteral("ROM library is up to date"));
        m_status->setText(QStringLiteral("No new or changed ROM files needed structural analysis."));
        m_path->setText(QStringLiteral("Kadia reused the results saved from the previous completed scan."));
    } else if (recognizedCount == 0 && unresolvedCount == 0) {
        m_title->setText(QStringLiteral("No new ROMs detected"));
        m_status->setText(QStringLiteral("Kadia did not detect a valid ROM among the new or changed files."));
        m_path->setText(QStringLiteral("%1 new/changed candidates were structurally checked.").arg(testedCandidates));
    } else {
        m_title->setText(QStringLiteral("ROM search complete"));
        m_status->setText(QStringLiteral("%1 new/updated recognized automatically, %2 need identification.")
                          .arg(recognizedCount).arg(unresolvedCount));
        m_path->setText(QStringLiteral("%1 new/changed candidates were structurally checked.").arg(testedCandidates));
    }

    m_action->setEnabled(true);
    m_action->setText(QStringLiteral("Continue"));
    m_action->setFocus();
}

void RomScanProgressDialog::actionPressed()
{
    if (m_completed) {
        accept();
        return;
    }
    if (m_cancelPending)
        return;
    m_cancelPending = true;
    m_action->setEnabled(false);
    m_action->setText(QStringLiteral("Stopping..."));
    m_status->setText(QStringLiteral("Stopping ROM search safely..."));
    emit cancelRequested();
}

void RomScanProgressDialog::pollController()
{
    const InputManager::Action a = m_input.poll();
    if (a == InputManager::None)
        return;
    if (a == InputManager::Accept || a == InputManager::Back)
        actionPressed();
}

RomClassificationDialog::RomClassificationDialog(const QString &path, const QString &hint,
                                                 const QString &internalTitle, const QString &format,
                                                 QWidget *parent)
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
    const QString detectedTitle = internalTitle.isEmpty()
        ? QStringLiteral("No standardized internal title is present in this ROM format")
        : internalTitle;
    const QString detectedFormat = format.isEmpty() ? QStringLiteral("ROM image") : format;
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
