#include "game_stats.h"

#include <QCryptographicHash>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace {
static QString statsPath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.mathery-kadia");
    QDir().mkpath(base);
    return QDir(base).filePath(QStringLiteral("game-stats.ini"));
}

static QString keyForPath(const QString &path)
{
    return QString::fromLatin1(QCryptographicHash::hash(QDir::cleanPath(path).toLower().toUtf8(),
                                                        QCryptographicHash::Sha1).toHex());
}

static QString groupForPath(const QString &path)
{
    return QStringLiteral("games/%1").arg(keyForPath(path));
}
}

KadiaGameStats GameStats::load(const QString &path)
{
    KadiaGameStats out;
    if (path.trimmed().isEmpty())
        return out;
    QSettings s(statsPath(), QSettings::IniFormat);
    s.beginGroup(groupForPath(path));
    out.playSeconds = qMax<qint64>(0, s.value(QStringLiteral("playSeconds"), 0).toLongLong());
    out.playCount = qMax(0, s.value(QStringLiteral("playCount"), 0).toInt());
    out.lastPlayed = s.value(QStringLiteral("lastPlayed")).toDateTime();
    out.dateAdded = s.value(QStringLiteral("dateAdded")).toDateTime();
    s.endGroup();
    return out;
}

void GameStats::ensureAdded(const QString &path, const QDateTime &fallback)
{
    if (path.trimmed().isEmpty())
        return;
    QSettings s(statsPath(), QSettings::IniFormat);
    s.beginGroup(groupForPath(path));
    if (!s.value(QStringLiteral("dateAdded")).toDateTime().isValid()) {
        const QDateTime value = fallback.isValid() ? fallback : QDateTime::currentDateTimeUtc();
        s.setValue(QStringLiteral("dateAdded"), value.toUTC());
    }
    s.setValue(QStringLiteral("path"), QDir::toNativeSeparators(path));
    s.endGroup();
    s.sync();
}

void GameStats::recordLaunch(const QString &path)
{
    if (path.trimmed().isEmpty())
        return;
    ensureAdded(path);
    QSettings s(statsPath(), QSettings::IniFormat);
    s.beginGroup(groupForPath(path));
    s.setValue(QStringLiteral("playCount"), s.value(QStringLiteral("playCount"), 0).toInt() + 1);
    s.setValue(QStringLiteral("lastPlayed"), QDateTime::currentDateTimeUtc());
    s.endGroup();
    s.sync();
}

void GameStats::addPlayTime(const QString &path, qint64 seconds)
{
    if (path.trimmed().isEmpty() || seconds <= 0)
        return;
    QSettings s(statsPath(), QSettings::IniFormat);
    s.beginGroup(groupForPath(path));
    const qint64 old = qMax<qint64>(0, s.value(QStringLiteral("playSeconds"), 0).toLongLong());
    s.setValue(QStringLiteral("playSeconds"), old + seconds);
    s.endGroup();
    s.sync();
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
