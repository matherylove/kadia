#include "ffmpeg_runtime.h"

extern "C" {
#include <libavutil/avutil.h>
}

QString FfmpegRuntime::version()
{
    const char *text = av_version_info();
    return text ? QString::fromLatin1(text) : QStringLiteral("unknown");
}

QString FfmpegRuntime::buildInfo()
{
    return QStringLiteral("FFmpeg %1 (XP runtime from the supplied Sightline project)")
        .arg(version());
}
