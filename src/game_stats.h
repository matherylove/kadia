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
    // Batch mode is available for grouped updates. The legacy game-stats.ini
    // is never parsed or rewritten; v2 only persists actual runtime changes.
    void beginBatch();
    void endBatch();
    void flush();

    KadiaGameStats load(const QString &path);
    void ensureAdded(const QString &path, const QDateTime &fallback = QDateTime());
    void recordLaunch(const QString &path);
    void addPlayTime(const QString &path, qint64 seconds);
    QString humanPlayTime(qint64 seconds);
}
