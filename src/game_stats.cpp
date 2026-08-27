#include "game_stats.h"

#include <QCryptographicHash>
#include <QDir>
#include <QHash>
#include <QSet>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace {
struct CachedStats
{
    KadiaGameStats stats;
    QString path;
};

static QString statsBasePath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.mathery-kadia");
    QDir().mkpath(base);
    return base;
}

static QString statsPath()
{
    // New writes use a compact file containing only records that actually
    // changed. The legacy game-stats.ini may contain one group per ROM from an
    // older build and is intentionally never rewritten by this version.
    return QDir(statsBasePath()).filePath(QStringLiteral("game-stats-v2.ini"));
}

static QString keyForPath(const QString &path)
{
    return QString::fromLatin1(QCryptographicHash::hash(QDir::cleanPath(path).toLower().toUtf8(),
                                                        QCryptographicHash::Sha1).toHex());
}

static QHash<QString, CachedStats> &cache()
{
    static QHash<QString, CachedStats> value;
    return value;
}

static QSet<QString> &dirtyKeys()
{
    static QSet<QString> value;
    return value;
}

static bool &cacheLoaded()
{
    static bool value = false;
    return value;
}

static int &batchDepth()
{
    static int value = 0;
    return value;
}

static void readStatsFile(const QString &path)
{
    if (!QFileInfo(path).exists())
        return;

    QSettings s(path, QSettings::IniFormat);
    s.beginGroup(QStringLiteral("games"));
    const QStringList groups = s.childGroups();
    for (int i = 0; i < groups.size(); ++i) {
        const QString key = groups.at(i);
        s.beginGroup(key);
        CachedStats item;
        item.stats.playSeconds = qMax<qint64>(0, s.value(QStringLiteral("playSeconds"), 0).toLongLong());
        item.stats.playCount = qMax(0, s.value(QStringLiteral("playCount"), 0).toInt());
        item.stats.lastPlayed = s.value(QStringLiteral("lastPlayed")).toDateTime();
        item.stats.dateAdded = s.value(QStringLiteral("dateAdded")).toDateTime();
        item.path = QDir::fromNativeSeparators(s.value(QStringLiteral("path")).toString());
        s.endGroup();
        cache().insert(key, item);
    }
    s.endGroup();
}

static void ensureCacheLoaded()
{
    if (cacheLoaded())
        return;

    cacheLoaded() = true;
    // Do not parse the legacy game-stats.ini at startup. Older builds could
    // create one group per discovered ROM, turning the file into a multi-MB
    // startup penalty even when it is never modified. v2 is deliberately
    // compact and only contains games that have actual runtime statistics.
    readStatsFile(statsPath());
}

static CachedStats &entryForPath(const QString &path)
{
    ensureCacheLoaded();
    return cache()[keyForPath(path)];
}

static void markDirty(const QString &path)
{
    dirtyKeys().insert(keyForPath(path));
}

static void flushDirty()
{
    if (dirtyKeys().isEmpty())
        return;

    QSettings s(statsPath(), QSettings::IniFormat);
    const QSet<QString> keys = dirtyKeys();
    for (QSet<QString>::const_iterator it = keys.constBegin(); it != keys.constEnd(); ++it) {
        const QString key = *it;
        const CachedStats item = cache().value(key);
        s.beginGroup(QStringLiteral("games/%1").arg(key));
        s.setValue(QStringLiteral("playSeconds"), item.stats.playSeconds);
        s.setValue(QStringLiteral("playCount"), item.stats.playCount);
        if (item.stats.lastPlayed.isValid())
            s.setValue(QStringLiteral("lastPlayed"), item.stats.lastPlayed.toUTC());
        else
            s.remove(QStringLiteral("lastPlayed"));
        if (item.stats.dateAdded.isValid())
            s.setValue(QStringLiteral("dateAdded"), item.stats.dateAdded.toUTC());
        if (!item.path.isEmpty())
            s.setValue(QStringLiteral("path"), QDir::toNativeSeparators(item.path));
        s.endGroup();
    }
    s.sync();
    if (s.status() == QSettings::NoError)
        dirtyKeys().subtract(keys);
}

static void flushIfNotBatching()
{
    if (batchDepth() == 0)
        flushDirty();
}
}

void GameStats::beginBatch()
{
    ensureCacheLoaded();
    ++batchDepth();
}

void GameStats::endBatch()
{
    if (batchDepth() <= 0)
        return;
    --batchDepth();
    if (batchDepth() == 0)
        flushDirty();
}

void GameStats::flush()
{
    ensureCacheLoaded();
    flushDirty();
}

KadiaGameStats GameStats::load(const QString &path)
{
    KadiaGameStats out;
    if (path.trimmed().isEmpty())
        return out;
    ensureCacheLoaded();
    const QString key = keyForPath(path);
    if (!cache().contains(key))
        return out;
    return cache().value(key).stats;
}

void GameStats::ensureAdded(const QString &path, const QDateTime &fallback)
{
    if (path.trimmed().isEmpty())
        return;

    CachedStats &item = entryForPath(path);
    bool changed = false;
    if (!item.stats.dateAdded.isValid()) {
        item.stats.dateAdded = (fallback.isValid() ? fallback : QDateTime::currentDateTimeUtc()).toUTC();
        changed = true;
    }
    const QString cleanPath = QDir::cleanPath(path);
    if (QDir::cleanPath(item.path).compare(cleanPath, Qt::CaseInsensitive) != 0) {
        item.path = cleanPath;
        changed = true;
    }
    if (changed) {
        markDirty(path);
        flushIfNotBatching();
    }
}

void GameStats::recordLaunch(const QString &path)
{
    if (path.trimmed().isEmpty())
        return;

    CachedStats &item = entryForPath(path);
    // dateAdded belongs to the ROM catalog unless an older stats entry already
    // has it. Launching a game must not make it look newly added.
    item.path = QDir::cleanPath(path);
    ++item.stats.playCount;
    item.stats.lastPlayed = QDateTime::currentDateTimeUtc();
    markDirty(path);
    // Do not rewrite a potentially large INI on the launch hot path. The
    // launch update stays in memory and is coalesced with play-time persistence
    // when Kadia regains focus (or with flush() during shutdown).
}

void GameStats::addPlayTime(const QString &path, qint64 seconds)
{
    if (path.trimmed().isEmpty() || seconds <= 0)
        return;

    CachedStats &item = entryForPath(path);
    item.path = QDir::cleanPath(path);
    item.stats.playSeconds = qMax<qint64>(0, item.stats.playSeconds) + seconds;
    markDirty(path);
    flushIfNotBatching();
}

QString GameStats::humanPlayTime(qint64 seconds)
{
    seconds = qMax<qint64>(0, seconds);
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    if (hours > 0)
        return QStringLiteral("%1 h %2 min").arg(hours).arg(minutes);
    if (minutes > 0)
        return QStringLiteral("%1 min").arg(minutes);
    return QStringLiteral("Not played");
}
