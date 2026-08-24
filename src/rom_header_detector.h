#pragma once

#include <QString>
#include <functional>

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
    typedef std::function<void (int percent, const QString &stage)> ProgressCallback;
    // Cheap metadata-only prefilter. This checks the filename extension only and
    // never opens the file. Content/header analysis is performed only when this
    // returns true.
    bool isCandidatePath(const QString &path);
    RomHeaderInfo detect(const QString &path);
    RomHeaderInfo detect(const QString &path, const ProgressCallback &progress);
}
