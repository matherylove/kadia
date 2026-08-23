#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QTimer>
#include <QWidget>

#include "d3d9_renderer.h"
#include "input_manager.h"
#include "kadia_scene.h"

class KadiaWindow : public QWidget
{
    Q_OBJECT
public:
    explicit KadiaWindow(QWidget *parent = 0);
    ~KadiaWindow();

protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;
    void showEvent(QShowEvent *event) Q_DECL_OVERRIDE;
    void resizeEvent(QResizeEvent *event) Q_DECL_OVERRIDE;
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;
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
};
