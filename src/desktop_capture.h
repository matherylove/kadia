#pragma once

#include <QImage>
#include <QRect>
#include <QSize>
#include <QtGui/qwindowdefs.h>

namespace DesktopCapture
{
    // Kept for source/settings compatibility. Wallpaper-only capture no longer
    // excludes Kadia from screen capture; this function simply clears any
    // legacy display-affinity flag on Windows.
    bool setCaptureExclusion(WId windowId, bool enabled);

    // Captures only the wallpaper renderer underneath Explorer's icon layer.
    // Wallpaper Engine is detected directly; other animated wallpaper hosts
    // parented to WorkerW are used as a generic fallback. Ordinary windows,
    // desktop icons and taskbars are never part of this capture path.
    QImage capture(const QRect &screenRect, const QSize &outputSize);
}
