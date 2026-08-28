#include "kadia_window.h"
#include "background_settings.h"
#include "desktop_capture.h"
#include "rom_scanner.h"
#include "libretro_metadata.h"
#include "windspro_bootstrap.h"
#include "ui_model.h"
#include "kadia_settings.h"
#include "emulator_manager.h"
#include "game_stats.h"
#include "media_library.h"
#include "media_library_dialog.h"

#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QDebug>
#include <QDesktopWidget>
#include <QKeyEvent>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QFileInfo>
#include <QDir>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>
#include <QtMath>

KadiaWindow::KadiaWindow(QWidget *parent)
    : QWidget(parent)
    , m_input(this)
    , m_frame(m_scene.logicalSize(), QImage::Format_ARGB32_Premultiplied)
    , m_rendererAttempted(false)
    , m_closing(false)
    , m_monitorMode(false)
    , m_romScanner(0)
    , m_romScanDialog(0)
    , m_metadataWorker(0)
    , m_metadataDialog(0)
    , m_romDialogActive(false)
    , m_romScanCancelled(false)
    , m_postStartupChecksCompleted(false)
    , m_liveDesktopBackground(false)
    , m_wallpaperCapture(0)
    , m_activeEmulatorPid(0)
    , m_fullscreenEnforceAttempts(0)
{
    setWindowTitle(QStringLiteral("Mathery Kadia!"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    resize(m_scene.logicalSize());
    m_scene.setViewportSize(size());
    const BackgroundPreferences initialBackground = BackgroundSettings::load();
    m_liveDesktopBackground = initialBackground.mode == BackgroundPreferences::DesktopWallpaper && initialBackground.opacity > 0;
    BackgroundSettings::applyToScene(&m_scene, initialBackground);
    m_wallpaperCapture = new DesktopCapture::WallpaperCaptureThread(this);
    m_wallpaperCapture->setEnabled(m_liveDesktopBackground);
    m_wallpaperCapture->start();
    refreshKadiaGameLibrary();
    setKadiaUnknownRoms(RomCatalog::pathsForClassification(QStringLiteral("Unknown")));

    m_input.initialize();

    // Present() is VSync-capped by D3D9. A zero-interval precise timer feeds the
    // renderer immediately after each vertical blank instead of hard-capping at 60 Hz.
    m_timer.setInterval(0);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, SIGNAL(timeout()), this, SLOT(frameTick()));

    // Some emulator builds ignore their fullscreen CLI option or create a new
    // game window after their launcher window. For a short period after launch,
    // give the emulator-owned fullscreen toggle a focused Alt+Enter attempt,
    // with ordinary maximization as the final reversible fallback. Kadia never
    // rewrites emulator window styles or swapchain geometry.
    m_emulatorFullscreenTimer.setInterval(200);
    m_emulatorFullscreenTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_emulatorFullscreenTimer, SIGNAL(timeout()), this, SLOT(enforceExternalFullscreen()));
}

KadiaWindow::~KadiaWindow()
{
    m_timer.stop();
    m_emulatorFullscreenTimer.stop();
    if (m_wallpaperCapture)
        m_wallpaperCapture->stop();
    DesktopCapture::setCaptureExclusion(winId(), false);
    if (m_romScanner) {
        m_romScanner->requestStop();
        m_romScanner->wait();
    }
    if (m_romScanDialog)
        m_romScanDialog->close();
    if (m_metadataWorker) {
        m_metadataWorker->requestStop();
        m_metadataWorker->wait();
    }
    if (m_metadataDialog)
        m_metadataDialog->close();
    m_renderer.shutdown();
}

void KadiaWindow::showOnPrimaryMonitor()
{
    QDesktopWidget *desktop = QApplication::desktop();
    const int primary = desktop ? desktop->primaryScreen() : 0;
    const QRect target = desktop ? desktop->screenGeometry(primary) : QRect(0, 0, 1280, 720);

    if (!m_monitorMode && isVisible())
        m_windowedGeometry = geometry();

    m_monitorMode = true;
    setGeometry(target);
    m_scene.setViewportSize(target.size());
    show();
    activateWindow();
}

void KadiaWindow::showWindowed()
{
    QDesktopWidget *desktop = QApplication::desktop();
    const int primary = desktop ? desktop->primaryScreen() : 0;
    const QRect available = desktop ? desktop->availableGeometry(primary) : QRect(0, 0, 1280, 720);

    QSize wanted(1280, 720);
    if (wanted.width() > available.width() || wanted.height() > available.height()) {
        const QSize safe(qMax(320, static_cast<int>(available.width() * 0.90)),
                         qMax(240, static_cast<int>(available.height() * 0.90)));
        wanted.scale(safe, Qt::KeepAspectRatio);
    }

    QRect target(QPoint(0, 0), wanted);
    target.moveCenter(available.center());
    m_monitorMode = false;
    m_windowedGeometry = target;
    setGeometry(target);
    m_scene.setViewportSize(target.size());
    show();
    activateWindow();
}

void KadiaWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    // Intentionally empty. Direct3D owns presentation.
}

void KadiaWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    ensureRenderer();
    // Wallpaper-only capture never excludes Kadia from OBS/screen capture.
    DesktopCapture::setCaptureExclusion(winId(), false);
    m_clock.start();
    if (m_wallpaperCapture) {
        QSize backgroundSize = size();
        const QSize budget(1920, 1080);
        if (backgroundSize.width() > budget.width() || backgroundSize.height() > budget.height())
            backgroundSize.scale(budget, Qt::KeepAspectRatio);
        m_wallpaperCapture->setTarget(QRect(mapToGlobal(QPoint(0, 0)), size()), backgroundSize, 30);
        m_wallpaperCapture->setEnabled(m_liveDesktopBackground);
    }
    if (!m_timer.isActive())
        m_timer.start();
}

void KadiaWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_scene.setViewportSize(event->size());
    if (m_renderer.isReady())
        m_renderer.resize(qMax(1, event->size().width()), qMax(1, event->size().height()));
    if (m_wallpaperCapture) {
        QSize backgroundSize = event->size();
        const QSize budget(1920, 1080);
        if (backgroundSize.width() > budget.width() || backgroundSize.height() > budget.height())
            backgroundSize.scale(budget, Qt::KeepAspectRatio);
        m_wallpaperCapture->setTarget(QRect(mapToGlobal(QPoint(0, 0)), event->size()), backgroundSize, 30);
    }
}

void KadiaWindow::keyPressEvent(QKeyEvent *event)
{
    if ((m_romScanDialog && m_romScanDialog->isVisible()) ||
        (m_metadataDialog && m_metadataDialog->isVisible()) || m_romDialogActive) {
        event->accept();
        return;
    }

    if (event->isAutoRepeat() &&
        (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down ||
         event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)) {
        event->accept();
        return;
    }

    switch (event->key()) {
    case Qt::Key_Up:
        m_scene.handle(KadiaScene::MoveUp);
        break;
    case Qt::Key_Down:
        m_scene.handle(KadiaScene::MoveDown);
        break;
    case Qt::Key_Left:
        m_scene.handle(KadiaScene::MoveLeft);
        break;
    case Qt::Key_Right:
        m_scene.handle(KadiaScene::MoveRight);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        m_scene.handle(KadiaScene::Accept);
        break;
    case Qt::Key_Escape:
    case Qt::Key_Backspace:
        m_scene.handle(KadiaScene::Back);
        break;
    case Qt::Key_Tab:
        m_scene.handle(KadiaScene::ToggleGallery);
        break;
    case Qt::Key_S:
        m_scene.handle(KadiaScene::CycleSort);
        break;
    case Qt::Key_F:
        m_scene.cycleWordmarkFont();
        break;
    case Qt::Key_F11:
        m_monitorMode ? showWindowed() : showOnPrimaryMonitor();
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

void KadiaWindow::mouseMoveEvent(QMouseEvent *event)
{
    if ((m_romScanDialog && m_romScanDialog->isVisible()) ||
        (m_metadataDialog && m_metadataDialog->isVisible()) || m_romDialogActive) {
        event->accept();
        return;
    }
    if (m_scene.hoverAt(event->localPos()))
        event->accept();
    else
        QWidget::mouseMoveEvent(event);
}

void KadiaWindow::mousePressEvent(QMouseEvent *event)
{
    if ((m_romScanDialog && m_romScanDialog->isVisible()) ||
        (m_metadataDialog && m_metadataDialog->isVisible()) || m_romDialogActive) {
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton) {
        m_scene.handle(KadiaScene::Back);
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        m_scene.handle(KadiaScene::ToggleGallery);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_scene.clickAt(event->localPos())) {
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void KadiaWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if ((m_romScanDialog && m_romScanDialog->isVisible()) ||
        (m_metadataDialog && m_metadataDialog->isVisible()) || m_romDialogActive) {
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_scene.doubleClickAt(event->localPos())) {
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void KadiaWindow::wheelEvent(QWheelEvent *event)
{
    if ((m_romScanDialog && m_romScanDialog->isVisible()) ||
        (m_metadataDialog && m_metadataDialog->isVisible()) || m_romDialogActive) {
        event->accept();
        return;
    }
    const int delta = event->angleDelta().y();
    if (delta != 0) {
        m_scene.wheelAt(QPointF(event->pos()), delta);
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void KadiaWindow::closeEvent(QCloseEvent *event)
{
    m_closing = true;
    m_timer.stop();
    m_emulatorFullscreenTimer.stop();
    if (m_wallpaperCapture)
        m_wallpaperCapture->setEnabled(false);
    GameStats::flush();
    m_renderer.shutdown();
    QWidget::closeEvent(event);
}

void KadiaWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() != QEvent::ActivationChange)
        return;

    // A fullscreen emulator can change display mode and invalidate a D3D9
    // device. Do not keep presenting Kadia behind the game: release D3D as
    // soon as the frontend loses focus and recreate it only when Kadia is
    // activated again. This also removes the zero-timer CPU load while a game
    // is running.
    if (!isActiveWindow()) {
        if (!m_activeGamePath.isEmpty()) {
            if (m_wallpaperCapture)
                m_wallpaperCapture->setEnabled(false);
            m_timer.stop();
            m_renderer.shutdown();
            m_rendererAttempted = false;
        }
        return;
    }

    if (!m_activeGamePath.isEmpty() && m_activeGameStarted.isValid()) {
        // Regaining Kadia focus does not necessarily mean the game closed; the
        // user may have Alt-Tabbed, or an emulator may briefly switch windows
        // during boot. Only close the play session when the launched process
        // tree is actually gone. This also keeps the fullscreen watcher alive
        // through slow launcher -> renderer transitions.
        const bool emulatorStillRunning = m_activeEmulatorPid > 0 &&
                                           EmulatorManager::isLaunchRunning(m_activeEmulatorPid, m_activeEmulatorExecutable);
        if (!emulatorStillRunning) {
            const QString finishedPath = m_activeGamePath;
            const qint64 seconds = m_activeGameStarted.secsTo(QDateTime::currentDateTimeUtc());
            if (seconds >= 5)
                GameStats::addPlayTime(finishedPath, seconds);
            m_activeGamePath.clear();
            m_activeGameStarted = QDateTime();
            m_activeEmulatorPid = 0;
            m_activeEmulatorExecutable.clear();
            m_emulatorFullscreenTimer.stop();
            // Updating one record is enough. Rebuilding/sorting every catalog
            // item here caused a visible stall whenever the emulator returned.
            updateKadiaGameFromPath(finishedPath);
        }
    }

    if (m_wallpaperCapture)
        m_wallpaperCapture->setEnabled(m_liveDesktopBackground);
    ensureRenderer();
    m_clock.restart();
    if (!m_timer.isActive())
        m_timer.start();
}

void KadiaWindow::enforceExternalFullscreen()
{
    if (m_closing || m_activeEmulatorPid <= 0) {
        m_emulatorFullscreenTimer.stop();
        return;
    }

    ++m_fullscreenEnforceAttempts;

    // Native emulator CLI fullscreen always gets first chance.  Some emulator
    // packs create the real renderer several seconds after the launcher, so keep
    // the watcher alive long enough to see that replacement instead of giving up
    // after ~5 seconds.  Each request first re-checks the actual window geometry,
    // which prevents a successful Alt+Enter from being toggled back out.
    if (m_fullscreenEnforceAttempts < 10)
        return; // ~2.0 seconds at 200 ms

    if (m_fullscreenEnforceAttempts == 10 ||
        m_fullscreenEnforceAttempts == 30) {
        // Give slow Direct3D/OpenGL mode switches a full four seconds before a
        // retry. Nestopia in particular can take a moment to rebuild its video
        // device; hammering Alt+Enter again while that transition is in flight can
        // immediately toggle it back to windowed mode.
        const int stage = m_fullscreenEnforceAttempts == 10 ? 0 : 1;
        if (EmulatorManager::enforceFullscreen(m_activeEmulatorPid, m_activeEmulatorExecutable, stage))
            m_emulatorFullscreenTimer.stop();
        return;
    }

    if (m_fullscreenEnforceAttempts == 50) {
        // A normal maximize is the final compatibility fallback. It is not fake
        // borderless fullscreen, but it is fully reversible and cannot corrupt
        // an emulator child renderer/swapchain.
        EmulatorManager::enforceFullscreen(m_activeEmulatorPid, m_activeEmulatorExecutable, 2);
        m_emulatorFullscreenTimer.stop();
        return;
    }

    if (m_fullscreenEnforceAttempts >= 52)
        m_emulatorFullscreenTimer.stop();
}

void KadiaWindow::resumeRenderingAfterExternalLaunch()
{
    if (m_closing || m_timer.isActive())
        return;

    // If the emulator owns the foreground, remain completely dormant. The
    // ActivationChange handler recreates D3D when Kadia becomes active again.
    if (!m_activeGamePath.isEmpty() && !isActiveWindow())
        return;

    ensureRenderer();
    m_clock.restart();
    if (!m_timer.isActive())
        m_timer.start();
}

void KadiaWindow::frameTick()
{
    if (m_closing)
        return;

    if (!m_activeGamePath.isEmpty() && !isActiveWindow()) {
        m_timer.stop();
        m_renderer.shutdown();
        m_rendererAttempted = false;
        return;
    }

    ensureRenderer();
    if (!m_renderer.isReady())
        return;

    const qint64 elapsedNs = m_clock.isValid() ? m_clock.nsecsElapsed() : 0;
    if (m_clock.isValid())
        m_clock.restart();
    else
        m_clock.start();
    const double fallbackDt = 1.0 / static_cast<double>(qMax(1, m_renderer.refreshRate()));
    double dt = elapsedNs > 0 ? static_cast<double>(elapsedNs) / 1000000000.0 : fallbackDt;
    if (dt > 0.1)
        dt = 0.1;

    // Keep polling to update edge state, but do not navigate the main menu
    // behind a scanner/classification dialog. Dialogs use their own XInput
    // manager while the renderer continues at the monitor refresh rate.
    const InputManager::Action action = m_input.poll();
    const bool scannerDialogVisible = m_romScanDialog && m_romScanDialog->isVisible();
    const bool metadataDialogVisible = m_metadataDialog && m_metadataDialog->isVisible();
    if (!scannerDialogVisible && !metadataDialogVisible && !m_romDialogActive && action != InputManager::None)
        dispatch(action);
    if (!scannerDialogVisible && !metadataDialogVisible && !m_romDialogActive)
        processSceneCommands();

    // LaunchSelectedGame deliberately tears down D3D inside the command
    // handler. Do not continue this same frame with a stale device pointer.
    if (!m_renderer.isReady())
        return;

    m_scene.setControllerConnected(m_input.controllerConnected());

    m_scene.setViewportSize(size());
    m_scene.update(dt);

    const QSize renderSize = renderSurfaceSize();
    if (m_wallpaperCapture) {
        // Updating the capture target is a tiny mutex-protected metadata write.
        // The expensive PrintWindow/GDI work happens only in WallpaperCaptureThread.
        // No capture call can ever block this monitor-rate render loop.
        QSize backgroundSize = size();
        const QSize backgroundBudget(1920, 1080);
        if (backgroundSize.width() > backgroundBudget.width() ||
            backgroundSize.height() > backgroundBudget.height())
            backgroundSize.scale(backgroundBudget, Qt::KeepAspectRatio);
        m_wallpaperCapture->setTarget(QRect(mapToGlobal(QPoint(0, 0)), size()), backgroundSize, 30);
        m_wallpaperCapture->setEnabled(m_liveDesktopBackground && m_activeGamePath.isEmpty());

        QImage desktopFrame;
        if (m_liveDesktopBackground && m_wallpaperCapture->takeLatest(&desktopFrame) && !desktopFrame.isNull())
            m_scene.setBackgroundImage(desktopFrame);
    }

    m_scene.render(m_frame, renderSize);
    if (!m_renderer.present(m_frame) && !m_renderer.lastError().isEmpty())
        qWarning() << m_renderer.lastError();
}

QSize KadiaWindow::renderSurfaceSize() const
{
    // Never downscale the complete UI. The previous adaptive software-raster
    // budget made text, thin lines and console artwork visibly soft at high
    // refresh rates because D3D9 had to enlarge the entire composed frame.
    // Performance work now targets expensive subsystems (wallpaper sampling,
    // async artwork, cached data) while Kadia stays pixel-sharp at native size.
    return QSize(qMax(1, width()), qMax(1, height()));
}

void KadiaWindow::runPostStartupChecks()
{
    if (m_closing)
        return;

    const bool automaticStartupPass = !m_postStartupChecksCompleted;
    m_postStartupChecksCompleted = true;

    WinDSProBootstrap::offerOnce(this);

    // Once a catalog exists, startup should be a cache load, not a full walk
    // of every mounted drive. The same slot is also used by Update Gamelist /
    // Scrape Now; those later calls are explicit and therefore still scan.
    if (automaticStartupPass && !kadiaGames().isEmpty())
        return;

    if (!m_romScanner) {
        m_romScanCancelled = false;
        m_romScanner = new RomScanner(this);
        m_romScanDialog = new RomScanProgressDialog(this);

        connect(m_romScanner, SIGNAL(discoveryProgress(QString,int)),
                m_romScanDialog, SLOT(onDiscoveryProgress(QString,int)));
        connect(m_romScanner, SIGNAL(analysisStarted(int)),
                m_romScanDialog, SLOT(onAnalysisStarted(int)));
        connect(m_romScanner, SIGNAL(fileProgress(QString,int,int,QString,int,int)),
                m_romScanDialog, SLOT(onFileProgress(QString,int,int,QString,int,int)));
        connect(m_romScanner, SIGNAL(scanSummary(int,int,int,bool)),
                m_romScanDialog, SLOT(onScanSummary(int,int,int,bool)));

        connect(m_romScanner, SIGNAL(romDiscovered(QString,QString,QString,QString)),
                this, SLOT(onRomDiscovered(QString,QString,QString,QString)));
        connect(m_romScanner, SIGNAL(scanSummary(int,int,int,bool)),
                this, SLOT(onRomScanSummary(int,int,int,bool)));
        connect(m_romScanDialog, SIGNAL(cancelRequested()),
                m_romScanner, SLOT(requestStop()), Qt::DirectConnection);
        connect(m_romScanDialog, SIGNAL(finished(int)),
                this, SLOT(onRomScanDialogFinished(int)));

        m_romScanDialog->show();
        m_romScanDialog->raise();
        m_romScanDialog->activateWindow();
        m_romScanner->start(QThread::LowestPriority);
    }
}

void KadiaWindow::onRomDiscovered(const QString &path, const QString &hint,
                                  const QString &internalTitle, const QString &format)
{
    PendingRom candidate;
    candidate.path = path;
    candidate.hint = hint;
    candidate.internalTitle = internalTitle;
    candidate.format = format;
    m_romQueue.enqueue(candidate);

    // Do not interrupt the scan with classification prompts. The worker keeps
    // running independently and unresolved dialogs begin only after the scan
    // progress window is dismissed.
}

void KadiaWindow::onRomRecognized(const QString &path, const QString &system, const QString &title)
{
    Q_UNUSED(path);
    Q_UNUSED(system);
    Q_UNUSED(title);
    // Intentionally batched. Rebuilding/sorting the complete GUI library once
    // per recognized ROM can flood the main event loop during a large scan.
}

void KadiaWindow::onRomScanSummary(int recognizedCount, int unresolvedCount,
                                   int testedCandidates, bool cancelled)
{
    Q_UNUSED(recognizedCount);
    Q_UNUSED(unresolvedCount);
    Q_UNUSED(testedCandidates);
    m_romScanCancelled = cancelled;
    if (cancelled)
        m_romQueue.clear();
}

void KadiaWindow::onRomScanDialogFinished(int result)
{
    Q_UNUSED(result);
    if (m_romScanDialog) {
        m_romScanDialog->deleteLater();
        m_romScanDialog = 0;
    }

    activateWindow();
    setFocus();

    // Metadata is deliberately a second phase. ROM discovery/header analysis is
    // complete before any database download, hashing for identification or cover
    // download begins. All of that work runs in its own low-priority thread.
    if (!m_romScanCancelled && !m_metadataWorker) {
        m_metadataWorker = new LibretroMetadataWorker(this);
        m_metadataDialog = new LibretroMetadataProgressDialog(this);

        connect(m_metadataWorker, SIGNAL(metadataStarted(int)),
                m_metadataDialog, SLOT(onMetadataStarted(int)));
        connect(m_metadataWorker, SIGNAL(metadataProgress(QString,QString,QString,int,int,int,int)),
                m_metadataDialog, SLOT(onMetadataProgress(QString,QString,QString,int,int,int,int)));
        connect(m_metadataWorker, SIGNAL(metadataSummary(int,int,int,bool)),
                m_metadataDialog, SLOT(onMetadataSummary(int,int,int,bool)));
        connect(m_metadataDialog, SIGNAL(cancelRequested()),
                m_metadataWorker, SLOT(requestStop()), Qt::DirectConnection);
        connect(m_metadataDialog, SIGNAL(finished(int)),
                this, SLOT(onMetadataDialogFinished(int)));

        m_metadataDialog->show();
        m_metadataDialog->raise();
        m_metadataDialog->activateWindow();
        m_metadataWorker->start(QThread::LowestPriority);
        return;
    }

    refreshKadiaGameLibrary();
    setKadiaUnknownRoms(RomCatalog::pathsForClassification(QStringLiteral("Unknown")));
    if (!m_romScanCancelled && !m_romQueue.isEmpty())
        QTimer::singleShot(0, this, SLOT(showNextRomDialog()));
}

void KadiaWindow::onMetadataDialogFinished(int result)
{
    Q_UNUSED(result);
    if (m_metadataDialog) {
        m_metadataDialog->deleteLater();
        m_metadataDialog = 0;
    }

    if (m_metadataWorker && m_metadataWorker->isRunning()) {
        // The summary is emitted immediately before the worker exits. Never wait
        // here: keeping the QThread object parented to Kadia lets it finish its
        // last instructions without blocking the Windows message/render loop.
        m_metadataWorker->requestStop();
    }

    // This refresh is memory/catalog only; it never reopens ROM contents.
    refreshKadiaGameLibrary();
    setKadiaUnknownRoms(RomCatalog::pathsForClassification(QStringLiteral("Unknown")));

    activateWindow();
    setFocus();

    if (!m_romScanCancelled && !m_romQueue.isEmpty())
        QTimer::singleShot(0, this, SLOT(showNextRomDialog()));
}

void KadiaWindow::showNextRomDialog()
{
    if (m_romDialogActive || m_romQueue.isEmpty() || m_closing ||
        (m_romScanDialog && m_romScanDialog->isVisible()) ||
        (m_metadataDialog && m_metadataDialog->isVisible()))
        return;

    m_romDialogActive = true;
    const PendingRom candidate = m_romQueue.dequeue();
    RomClassificationDialog dialog(candidate.path, candidate.hint,
                                   candidate.internalTitle, candidate.format, this);
    if (dialog.exec() == QDialog::Accepted && !dialog.selectedSystem().isEmpty()) {
        RomCatalog::saveClassification(candidate.path, dialog.selectedSystem());
        setKadiaUnknownRoms(RomCatalog::pathsForClassification(QStringLiteral("Unknown")));
        if (dialog.selectedSystem().compare(QStringLiteral("Unknown"), Qt::CaseInsensitive) != 0 &&
            dialog.selectedSystem().compare(QStringLiteral("None (ignore)"), Qt::CaseInsensitive) != 0)
            updateKadiaGameFromPath(candidate.path);
    }
    m_romDialogActive = false;

    if (!m_romQueue.isEmpty())
        QTimer::singleShot(0, this, SLOT(showNextRomDialog()));
}

void KadiaWindow::processSceneCommands()
{
    const KadiaScene::Command command = m_scene.takePendingCommand();
    if (command == KadiaScene::NoCommand)
        return;

    if (command == KadiaScene::OpenBackgroundSettings) {
        BackgroundSettingsDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted) {
            const BackgroundPreferences preferences = dialog.preferences();
            m_liveDesktopBackground = preferences.mode == BackgroundPreferences::DesktopWallpaper && preferences.opacity > 0;
            DesktopCapture::setCaptureExclusion(winId(), false);
            if (m_wallpaperCapture)
                m_wallpaperCapture->setEnabled(m_liveDesktopBackground);
            BackgroundSettings::applyToScene(&m_scene, preferences);
        }
        return;
    }

    if (command == KadiaScene::OpenKadiaSettings) {
        KadiaSettingsDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted) {
            setKadiaGameSort(static_cast<KadiaGameSort>(KadiaSettings::defaultGallerySort()));
            refreshKadiaGameLibrary();
            const BackgroundPreferences preferences = BackgroundSettings::load();
            m_liveDesktopBackground = preferences.mode == BackgroundPreferences::DesktopWallpaper && preferences.opacity > 0;
            DesktopCapture::setCaptureExclusion(winId(), false);
            if (m_wallpaperCapture)
                m_wallpaperCapture->setEnabled(m_liveDesktopBackground);
            BackgroundSettings::applyToScene(&m_scene, preferences);
        }
        return;
    }

    if (command == KadiaScene::LaunchSelectedGame) {
        const QString path = m_scene.selectedGamePath();
        qint64 launchedPid = 0;
        QString launchedEmulator;
        if (EmulatorManager::launch(m_scene.selectedGameSystem(), path, this, &launchedPid, &launchedEmulator)) {
            m_activeGamePath = path;
            m_activeGameStarted = QDateTime::currentDateTimeUtc();
            m_activeEmulatorPid = launchedPid;
            m_activeEmulatorExecutable = launchedEmulator;
            m_fullscreenEnforceAttempts = 0;
            if (m_activeEmulatorPid > 0) {
                // Wait for the actual renderer window before staged,
                // emulator-owned fullscreen shortcuts. Native CLI fullscreen
                // gets first chance and Kadia never rewrites the HWND styles.
                m_emulatorFullscreenTimer.start();
            }
            updateKadiaGameFromPath(path);

            // Give the emulator exclusive ownership of the display transition.
            // Keeping a live D3D9 device while many emulators switch to their
            // own fullscreen device was the main background-crash path.
            m_timer.stop();
            m_renderer.shutdown();
            m_rendererAttempted = false;
            QTimer::singleShot(1500, this, SLOT(resumeRenderingAfterExternalLaunch()));
        }
        return;
    }

    if (command != KadiaScene::RunTileAction)
        return;

    const QString section = m_scene.selectedSectionName();
    const QString tile = m_scene.selectedTileName();

    if (section == QStringLiteral("Power")) {
        if (tile == QStringLiteral("Cancel")) return;
        if (tile == QStringLiteral("Exit to Windows") || tile == QStringLiteral("Quit Frontend")) {
            close();
            return;
        }
#ifdef Q_OS_WIN
        if (tile == QStringLiteral("Sleep")) {
            QProcess::startDetached(QStringLiteral("rundll32.exe"), QStringList() << QStringLiteral("powrprof.dll,SetSuspendState") << QStringLiteral("0,1,0"));
            return;
        }
        if (tile == QStringLiteral("Restart") || tile == QStringLiteral("Shut Down")) {
            const QString verb = tile == QStringLiteral("Restart") ? QStringLiteral("restart") : QStringLiteral("shut down");
            if (QMessageBox::question(this, QStringLiteral("Mathery Kadia!"),
                                      QStringLiteral("Do you want to %1 Windows now?").arg(verb),
                                      QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
                QProcess::startDetached(QStringLiteral("shutdown.exe"),
                                        QStringList() << (tile == QStringLiteral("Restart") ? QStringLiteral("/r") : QStringLiteral("/s")) << QStringLiteral("/t") << QStringLiteral("0"));
            }
            return;
        }
#endif
    }

    if (section == QStringLiteral("Controllers")) {
        QProcess::startDetached(QStringLiteral("control.exe"), QStringList() << QStringLiteral("joy.cpl"));
        return;
    }
    if (section == QStringLiteral("Sound")) {
        QProcess::startDetached(QStringLiteral("control.exe"), QStringList() << QStringLiteral("mmsys.cpl"));
        return;
    }
    if (section == QStringLiteral("System Settings")) {
        if (tile == QStringLiteral("Language")) QProcess::startDetached(QStringLiteral("control.exe"), QStringList() << QStringLiteral("intl.cpl"));
        else if (tile == QStringLiteral("Power Saving")) QProcess::startDetached(QStringLiteral("control.exe"), QStringList() << QStringLiteral("powercfg.cpl"));
        else if (tile == QStringLiteral("Video Options")) QProcess::startDetached(QStringLiteral("control.exe"), QStringList() << QStringLiteral("desk.cpl"));
        else if (tile == QStringLiteral("Data Management")) QProcess::startDetached(QStringLiteral("explorer.exe"), QStringList() << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
        else if (tile == QStringLiteral("Developer Tools")) QProcess::startDetached(QStringLiteral("explorer.exe"), QStringList() << QCoreApplication::applicationDirPath());
        else {
            KadiaSettingsDialog dialog(this); dialog.exec();
        }
        return;
    }

    if (section == QStringLiteral("Updates + Downloads") || tile == QStringLiteral("Update Gamelist") || tile == QStringLiteral("Scrape Now")) {
        if (tile == QStringLiteral("Update Gamelist") || tile == QStringLiteral("Scrape Now") || tile == QStringLiteral("Check Updates")) {
            if (m_romScanner && !m_romScanner->isRunning()) {
                delete m_romScanner;
                m_romScanner = 0;
            }
            if (m_metadataWorker && !m_metadataWorker->isRunning()) {
                delete m_metadataWorker;
                m_metadataWorker = 0;
            }
            runPostStartupChecks();
        } else {
            QMessageBox::information(this, QStringLiteral("Mathery Kadia!"),
                                     QStringLiteral("%1 is available through the configured frontend/download provider.").arg(tile));
        }
        return;
    }

    if (tile == QStringLiteral("User Manual") || tile == QStringLiteral("Manual")) {
        const QString readme = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("README.md"));
        if (QFileInfo(readme).exists()) QProcess::startDetached(QStringLiteral("notepad.exe"), QStringList() << readme);
        else QMessageBox::information(this, QStringLiteral("Mathery Kadia!"), QStringLiteral("The local manual was not found next to Kadia.exe."));
        return;
    }

    // First functional Windows Media Center layer. Media indexing is strictly
    // on-demand and runs in MediaLibraryDialog's worker thread, so none of these
    // features add work to Kadia startup or the D3D render loop.
    if (section == QStringLiteral("Home") && tile == QStringLiteral("Live TV")) {
        QMessageBox::information(this, QStringLiteral("Kadia Live TV"),
                                 QStringLiteral("Live TV and the guide require the tuner/source backend, which is the next Windows Media Center phase. Local media libraries are already enabled."));
        return;
    }
    if ((section == QStringLiteral("Home") && tile == QStringLiteral("Music")) ||
        (section == QStringLiteral("Music") &&
         (tile == QStringLiteral("Music Library") || tile == QStringLiteral("Search")))) {
        MediaLibraryDialog dialog(MediaMusic, this);
        dialog.exec();
        return;
    }
    if (section == QStringLiteral("Music") && tile == QStringLiteral("Play All")) {
        MediaLibraryDialog dialog(MediaMusic, this, true);
        dialog.exec();
        return;
    }
    if (section == QStringLiteral("TV + Movies") && tile == QStringLiteral("Recorded TV")) {
        MediaLibraryDialog dialog(MediaRecordedTV, this);
        dialog.exec();
        return;
    }
    if (section == QStringLiteral("TV + Movies") &&
        (tile == QStringLiteral("Movies") || tile == QStringLiteral("Search"))) {
        MediaLibraryDialog dialog(MediaVideo, this);
        dialog.exec();
        return;
    }
    if (section == QStringLiteral("Pictures + Videos") &&
        (tile == QStringLiteral("Picture Library") || tile == QStringLiteral("Slide Show"))) {
        MediaLibraryDialog dialog(MediaPicture, this);
        dialog.exec();
        return;
    }
    if (section == QStringLiteral("Pictures + Videos") &&
        (tile == QStringLiteral("Video Library") || tile == QStringLiteral("Search"))) {
        MediaLibraryDialog dialog(tile == QStringLiteral("Search") ? MediaAny : MediaVideo, this);
        dialog.exec();
        return;
    }
    if (section == QStringLiteral("Pictures + Videos") && tile == QStringLiteral("Play All")) {
        MediaLibraryDialog dialog(MediaVideo, this, true);
        dialog.exec();
        return;
    }
    if (section == QStringLiteral("Tasks") && tile == QStringLiteral("Media Libraries")) {
        MediaLibrarySettingsDialog dialog(this);
        dialog.exec();
        return;
    }
    if (section == QStringLiteral("Tasks") && tile == QStringLiteral("Media Only")) {
        MediaLibraryDialog dialog(MediaAny, this);
        dialog.exec();
        return;
    }

    // Settings-like tiles are persisted instead of remaining inert. This gives
    // every non-Media-Center configuration tile an immediate, reversible action.
    const bool mediaCenterOnly = section == QStringLiteral("TV + Movies") || section == QStringLiteral("Music") ||
                                 section == QStringLiteral("Pictures + Videos") || section == QStringLiteral("Sports") ||
                                 section == QStringLiteral("Online Media") || section == QStringLiteral("Extras") ||
                                 section == QStringLiteral("Tasks");
    if (mediaCenterOnly) {
        QMessageBox::information(this, QStringLiteral("Windows Media Center feature"),
                                 QStringLiteral("%1 belongs to the next Media Center backend phase. Music, local video/movie, picture, recorded-TV libraries and Media Libraries setup are now functional.").arg(tile));
        return;
    }

    QSettings settings(KadiaSettings::settingsPath(), QSettings::IniFormat);
    QString key = (section + QLatin1Char('/') + tile).toLower();
    for (int i = 0; i < key.size(); ++i)
        if (!key.at(i).isLetterOrNumber() && key.at(i) != QLatin1Char('/')) key[i] = QLatin1Char('_');
    const bool current = settings.value(QStringLiteral("actions/") + key, false).toBool();
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tile, QStringLiteral("%1 is currently %2. Toggle it?").arg(tile, current ? QStringLiteral("enabled") : QStringLiteral("disabled")),
        QMessageBox::Yes | QMessageBox::No);
    if (answer == QMessageBox::Yes) {
        settings.setValue(QStringLiteral("actions/") + key, !current);
        settings.sync();
    }
}

void KadiaWindow::ensureRenderer()
{
    if (m_renderer.isReady() || m_rendererAttempted)
        return;

    m_rendererAttempted = true;
    const WId id = winId();
    if (!m_renderer.initialize(id, qMax(1, width()), qMax(1, height()))) {
        qCritical() << "Kadia could not initialize Direct3D 9:" << m_renderer.lastError();
        setWindowTitle(QStringLiteral("Mathery Kadia! - Direct3D 9 initialization failed"));
    } else {
        qDebug() << "Renderer:" << m_renderer.description();
    }
}

void KadiaWindow::dispatch(InputManager::Action action)
{
    switch (action) {
    case InputManager::Up: m_scene.handle(KadiaScene::MoveUp); break;
    case InputManager::Down: m_scene.handle(KadiaScene::MoveDown); break;
    case InputManager::Left: m_scene.handle(KadiaScene::MoveLeft); break;
    case InputManager::Right: m_scene.handle(KadiaScene::MoveRight); break;
    case InputManager::Accept: m_scene.handle(KadiaScene::Accept); break;
    case InputManager::Back: m_scene.handle(KadiaScene::Back); break;
    case InputManager::ToggleGallery: m_scene.handle(KadiaScene::ToggleGallery); break;
    case InputManager::CycleSort: m_scene.handle(KadiaScene::CycleSort); break;
    default: break;
    }
}
