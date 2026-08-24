#include "kadia_window.h"
#include "background_settings.h"
#include "rom_scanner.h"
#include "windspro_bootstrap.h"
#include "ui_model.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QDesktopWidget>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>

KadiaWindow::KadiaWindow(QWidget *parent)
    : QWidget(parent)
    , m_input(this)
    , m_frame(m_scene.logicalSize(), QImage::Format_ARGB32_Premultiplied)
    , m_rendererAttempted(false)
    , m_closing(false)
    , m_monitorMode(false)
    , m_romScanner(0)
    , m_romDialogActive(false)
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
    BackgroundSettings::applyToScene(&m_scene, BackgroundSettings::load());
    refreshKadiaGameLibrary();
    setKadiaUnknownRoms(RomCatalog::pathsForClassification(QStringLiteral("Unknown")));

    m_input.initialize();

    // Present() is VSync-capped by D3D9. A zero-interval precise timer feeds the
    // renderer immediately after each vertical blank instead of hard-capping at 60 Hz.
    m_timer.setInterval(0);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, SIGNAL(timeout()), this, SLOT(frameTick()));
}

KadiaWindow::~KadiaWindow()
{
    m_timer.stop();
    if (m_romScanner) {
        m_romScanner->requestStop();
        m_romScanner->wait();
    }
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
    m_clock.start();
    if (!m_timer.isActive())
        m_timer.start();
}

void KadiaWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_scene.setViewportSize(event->size());
    if (m_renderer.isReady())
        m_renderer.resize(qMax(1, event->size().width()), qMax(1, event->size().height()));
}

void KadiaWindow::keyPressEvent(QKeyEvent *event)
{
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
    if (m_scene.hoverAt(event->localPos()))
        event->accept();
    else
        QWidget::mouseMoveEvent(event);
}

void KadiaWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        m_scene.handle(KadiaScene::Back);
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
    if (event->button() == Qt::LeftButton && m_scene.doubleClickAt(event->localPos())) {
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void KadiaWindow::wheelEvent(QWheelEvent *event)
{
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
    m_renderer.shutdown();
    QWidget::closeEvent(event);
}

void KadiaWindow::frameTick()
{
    if (m_closing)
        return;

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

    const InputManager::Action action = m_input.poll();
    if (action != InputManager::None)
        dispatch(action);
    processSceneCommands();
    m_scene.setControllerConnected(m_input.controllerConnected());

    m_scene.setViewportSize(size());
    m_scene.update(dt);
    m_scene.render(m_frame);
    if (!m_renderer.present(m_frame) && !m_renderer.lastError().isEmpty())
        qWarning() << m_renderer.lastError();
}

void KadiaWindow::runPostStartupChecks()
{
    if (m_closing)
        return;

    WinDSProBootstrap::offerOnce(this);

    if (!m_romScanner) {
        m_romScanner = new RomScanner(this);
        connect(m_romScanner, SIGNAL(romDiscovered(QString,QString)),
                this, SLOT(onRomDiscovered(QString,QString)));
        connect(m_romScanner, SIGNAL(romRecognized(QString,QString,QString)),
                this, SLOT(onRomRecognized(QString,QString,QString)));
        m_romScanner->start();
    }
}

void KadiaWindow::onRomDiscovered(const QString &path, const QString &hint)
{
    m_romQueue.enqueue(qMakePair(path, hint));
    if (!m_romDialogActive)
        QTimer::singleShot(0, this, SLOT(showNextRomDialog()));
}

void KadiaWindow::onRomRecognized(const QString &path, const QString &system, const QString &title)
{
    Q_UNUSED(system);
    Q_UNUSED(title);
    // The scanner has already persisted the trusted structural detection.
    // Refreshing the in-memory model makes the game appear immediately while
    // scanning continues in the background.
    if (!path.isEmpty())
        updateKadiaGameFromPath(path);
    setKadiaUnknownRoms(RomCatalog::pathsForClassification(QStringLiteral("Unknown")));
}

void KadiaWindow::showNextRomDialog()
{
    if (m_romDialogActive || m_romQueue.isEmpty() || m_closing)
        return;

    m_romDialogActive = true;
    const QPair<QString, QString> candidate = m_romQueue.dequeue();
    RomClassificationDialog dialog(candidate.first, candidate.second, this);
    if (dialog.exec() == QDialog::Accepted && !dialog.selectedSystem().isEmpty()) {
        RomCatalog::saveClassification(candidate.first, dialog.selectedSystem());
        setKadiaUnknownRoms(RomCatalog::pathsForClassification(QStringLiteral("Unknown")));
        if (dialog.selectedSystem().compare(QStringLiteral("Unknown"), Qt::CaseInsensitive) != 0 &&
            dialog.selectedSystem().compare(QStringLiteral("None (ignore)"), Qt::CaseInsensitive) != 0)
            refreshKadiaGameLibrary();
    }
    m_romDialogActive = false;

    if (!m_romQueue.isEmpty())
        QTimer::singleShot(0, this, SLOT(showNextRomDialog()));
}

void KadiaWindow::processSceneCommands()
{
    const KadiaScene::Command command = m_scene.takePendingCommand();
    if (command == KadiaScene::OpenBackgroundSettings) {
        BackgroundSettingsDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted)
            BackgroundSettings::applyToScene(&m_scene, dialog.preferences());
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
    default: break;
    }
}
