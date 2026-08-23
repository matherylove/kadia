#include "kadia_window.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QShowEvent>

KadiaWindow::KadiaWindow(QWidget *parent)
    : QWidget(parent)
    , m_input(this)
    , m_frame(m_scene.logicalSize(), QImage::Format_ARGB32_Premultiplied)
    , m_rendererAttempted(false)
    , m_closing(false)
{
    setWindowTitle(QStringLiteral("Mathery Kadia!"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);
    setCursor(Qt::BlankCursor);
    resize(m_scene.logicalSize());

    m_input.initialize();

    m_timer.setInterval(16);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, SIGNAL(timeout()), this, SLOT(frameTick()));
}

KadiaWindow::~KadiaWindow()
{
    m_timer.stop();
    m_renderer.shutdown();
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
        isFullScreen() ? showNormal() : showFullScreen();
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
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

    const qint64 elapsedMs = m_clock.isValid() ? m_clock.restart() : 16;
    double dt = elapsedMs > 0 ? static_cast<double>(elapsedMs) / 1000.0 : 1.0 / 60.0;
    if (dt > 0.1)
        dt = 0.1;

    const InputManager::Action action = m_input.poll();
    if (action != InputManager::None)
        dispatch(action);
    m_scene.setControllerConnected(m_input.controllerConnected());

    m_scene.update(dt);
    m_scene.render(m_frame);
    if (!m_renderer.present(m_frame) && !m_renderer.lastError().isEmpty())
        qWarning() << m_renderer.lastError();
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
