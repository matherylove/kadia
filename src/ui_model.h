#pragma once

#include <QString>
#include <QVector>
#include <QStringList>
#include <QDateTime>

struct KadiaTileInfo {
    QString label;
    QString icon;
    QString title;
    QString description;
};

struct KadiaSectionInfo {
    QString name;
    QString caption;
    QVector<KadiaTileInfo> tiles;
};

struct KadiaGameInfo {
    QString title;
    QString subtitle;
    QString description;
    QString system;
    QString path;
    QString coverPath;
    QString releaseYear;
    QDateTime dateAdded;
    QDateTime lastPlayed;
    qint64 playSeconds;
    int playCount;

    KadiaGameInfo() : playSeconds(0), playCount(0) {}
};

void setKadiaDetectedStores(const QStringList &stores);
QStringList kadiaDetectedStores();
void setKadiaUnknownRoms(const QStringList &paths);
void refreshKadiaGameLibrary();
void updateKadiaGameFromPath(const QString &path);
enum KadiaGameSort {
    SortAlphabetical = 0,
    SortReleaseDate = 1,
    SortPlayedState = 2,
    SortPlayTime = 3,
    SortDateAdded = 4
};

void setKadiaActiveGameFilter(const QString &filter);
void setKadiaGameSort(KadiaGameSort sort);
KadiaGameSort kadiaGameSort();
QString kadiaGameSortLabel();
const QVector<KadiaSectionInfo> &kadiaSections();
const QVector<KadiaGameInfo> &kadiaGames();
