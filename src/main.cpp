#include <QApplication>
#include <QDebug>
#include <QDesktopWidget>
#include <QStringList>
#include <QTimer>

#include "ffmpeg_runtime.h"
#include "kadia_window.h"
#include "klite_bootstrap.h"
#include "store_detector.h"
#include "ui_model.h"
#include "rom_scanner.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Mathery Kadia!"));
    QApplication::setOrganizationName(QStringLiteral("Mathery"));
    QApplication::setQuitOnLastWindowClosed(true);

    qDebug() << FfmpegRuntime::buildInfo();

    // Detect storefronts before the scene model is first consumed. PC-store
    // tiles are generated dynamically from what actually exists on this PC.
    setKadiaDetectedStores(StoreDetector::detectInstalledStores());
    setKadiaUnknownRoms(RomCatalog::pathsForClassification(QStringLiteral("Unknown")));

    // K-Lite Codec Pack Full is a hard runtime prerequisite. If it is not
    // installed, show Kadia's own setup-progress window, download the
    // OS-compatible Full installer from Codec Guide, verify SHA-256, and run
    // the official installer in /verysilent mode before the main UI starts.
    if (!KLiteBootstrap::ensureInstalled(0))
        return 3;

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

    QTimer::singleShot(350, &window, SLOT(runPostStartupChecks()));
    return app.exec();
}
