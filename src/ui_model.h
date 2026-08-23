#pragma once

#include <QString>
#include <QVector>

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

const QVector<KadiaSectionInfo> &kadiaSections();
const QVector<KadiaGameInfo> &kadiaGames();
