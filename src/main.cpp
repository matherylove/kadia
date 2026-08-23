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

    if (windowed) {
        QRect available = QApplication::desktop()->availableGeometry();
        QSize wanted(1280, 720);
        if (wanted.width() > available.width() || wanted.height() > available.height()) {
            const QSize safeSize(qMax(320, static_cast<int>(available.width() * 0.90)),
                                 qMax(240, static_cast<int>(available.height() * 0.90)));
            wanted.scale(safeSize, Qt::KeepAspectRatio);
        }
        window.resize(wanted);
        window.move(available.center() - QPoint(wanted.width() / 2, wanted.height() / 2));
        window.show();
    } else {
        window.showFullScreen();
    }

    return app.exec();
}
