#pragma once

#include <QString>

struct RomHeaderInfo
{
    RomHeaderInfo()
        : isRom(false)
        , confidence(0)
    {
    }

    bool isRom;
    QString system;
    QString title;
    QString internalId;
    QString format;
    int confidence;
};

namespace RomHeaderDetector
{
    RomHeaderInfo detect(const QString &path);
}
