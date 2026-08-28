#pragma once

#include <QImage>
#include <QMutex>
#include <QRect>
#include <QSize>
#include <QThread>
#include <QWaitCondition>
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

    // Runs the expensive cross-process wallpaper capture completely outside the
    // UI/render thread. It keeps only one latest frame (no queue/backlog), so a
    // slow wallpaper renderer can never throttle Kadia's monitor-rate D3D loop.
    class WallpaperCaptureThread : public QThread
    {
    public:
        explicit WallpaperCaptureThread(QObject *parent = 0);
        ~WallpaperCaptureThread();

        void setEnabled(bool enabled);
        void setTarget(const QRect &screenRect, const QSize &outputSize, int captureHz = 30);
        bool takeLatest(QImage *frameOut);
        void stop();

    protected:
        void run() Q_DECL_OVERRIDE;

    private:
        QMutex m_mutex;
        QWaitCondition m_wake;
        QRect m_screenRect;
        QSize m_outputSize;
        int m_captureHz;
        bool m_enabled;
        bool m_stopping;
        QImage m_latest;
        quint64 m_producedSerial;
        quint64 m_consumedSerial;
    };
}
