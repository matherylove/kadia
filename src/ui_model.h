#pragma once

#include <QString>
#include <QVector>
#include <QStringList>

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
};

void setKadiaDetectedStores(const QStringList &stores);
QStringList kadiaDetectedStores();
void setKadiaUnknownRoms(const QStringList &paths);
void refreshKadiaGameLibrary();
void updateKadiaGameFromPath(const QString &path);
void setKadiaActiveGameFilter(const QString &filter);
const QVector<KadiaSectionInfo> &kadiaSections();
const QVector<KadiaGameInfo> &kadiaGames();
