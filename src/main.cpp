#include <QApplication>
#include <QDebug>
#include <QDesktopWidget>
#include <QStringList>

#include "ffmpeg_runtime.h"
#include "kadia_window.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Mathery Kadia!"));
    QApplication::setOrganizationName(QStringLiteral("Mathery"));
    QApplication::setQuitOnLastWindowClosed(true);

    qDebug() << FfmpegRuntime::buildInfo();

    KadiaWindow window;
    const QStringList args = app.arguments();
    const bool windowed = args.contains(QStringLiteral("--windowed"), Qt::CaseInsensitive);

    // Default mode is a normal, borderless window sized to the primary monitor.
    // It is deliberately NOT Qt::FullScreen and never uses WindowStaysOnTop, so
    // Alt+Tab, Win key, task switching and returning to Windows keep working.
    if (windowed)
        window.showWindowed();
    else
        window.showOnPrimaryMonitor();

    return app.exec();
}
