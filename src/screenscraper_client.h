#pragma once

#include <QString>

struct ScreenScraperMetadata
{
    ScreenScraperMetadata()
        : success(false)
    {
    }

    bool success;
    QString title;
    QString description;
    QString coverPath;
    QString source;
};

namespace ScreenScraperClient
{
    bool isConfigured();
    ScreenScraperMetadata fetchMetadata(const QString &romPath,
                                        const QString &system,
                                        const QString &headerTitle,
                                        const QString &internalId);
}
