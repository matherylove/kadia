#pragma once

#include <QDateTime>
#include <QString>

struct KadiaGameStats
{
    qint64 playSeconds;
    int playCount;
    QDateTime lastPlayed;
    QDateTime dateAdded;

    KadiaGameStats() : playSeconds(0), playCount(0) {}
};

namespace GameStats
{
    KadiaGameStats load(const QString &path);
    void ensureAdded(const QString &path, const QDateTime &fallback = QDateTime());
    void recordLaunch(const QString &path);
    void addPlayTime(const QString &path, qint64 seconds);
    QString humanPlayTime(qint64 seconds);
}
