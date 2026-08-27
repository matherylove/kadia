#pragma once

#include <QImage>
#include <QRect>
#include <QSize>
#include <QtGui/qwindowdefs.h>

namespace DesktopCapture
{
    // On Windows 10 2004+ this excludes Kadia itself from public screen
    // capture APIs, allowing a true live capture of whatever is behind it.
    // Older Windows versions return false and use the wallpaper-window fallback.
    bool setCaptureExclusion(WId windowId, bool enabled);

    // Captures the desktop wallpaper surface underneath Kadia. On Windows this
    // prefers Wallpaper Engine's own render window when it is present, then
    // falls back to the shell wallpaper host. The returned image is already
    // scaled to outputSize so the main render loop never has to copy a native
    // 1440p/4K desktop frame just to downsample it afterwards.
    QImage capture(const QRect &screenRect, const QSize &outputSize);
}
