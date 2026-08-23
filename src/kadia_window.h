#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QTimer>
#include <QWidget>
#include <QRect>

#include "d3d9_renderer.h"
#include "input_manager.h"
#include "kadia_scene.h"

class QMouseEvent;
class QWheelEvent;

class KadiaWindow : public QWidget
{
    Q_OBJECT
public:
    explicit KadiaWindow(QWidget *parent = 0);
    ~KadiaWindow();

    void showOnPrimaryMonitor();
    void showWindowed();

protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;
    void showEvent(QShowEvent *event) Q_DECL_OVERRIDE;
    void resizeEvent(QResizeEvent *event) Q_DECL_OVERRIDE;
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;
    void mouseMoveEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void mouseDoubleClickEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void wheelEvent(QWheelEvent *event) Q_DECL_OVERRIDE;
    void closeEvent(QCloseEvent *event) Q_DECL_OVERRIDE;

private slots:
    void frameTick();

private:
    void ensureRenderer();
    void dispatch(InputManager::Action action);

    D3D9Renderer m_renderer;
    InputManager m_input;
    KadiaScene m_scene;
    QImage m_frame;
    QTimer m_timer;
    QElapsedTimer m_clock;
    bool m_rendererAttempted;
    bool m_closing;
    bool m_monitorMode;
    QRect m_windowedGeometry;
};
