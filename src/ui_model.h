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
};

void setKadiaDetectedStores(const QStringList &stores);
QStringList kadiaDetectedStores();
void setKadiaUnknownRoms(const QStringList &paths);
const QVector<KadiaSectionInfo> &kadiaSections();
const QVector<KadiaGameInfo> &kadiaGames();
