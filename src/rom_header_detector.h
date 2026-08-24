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
    // Cheap metadata-only prefilter. This checks the filename extension only and
    // never opens the file. Content/header analysis is performed only when this
    // returns true.
    bool isCandidatePath(const QString &path);
    RomHeaderInfo detect(const QString &path);
}
