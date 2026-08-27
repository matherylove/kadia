#pragma once

#include <QImage>
#include <QColor>
#include <QFont>
#include <QSize>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <random>
#include "ui_model.h"

class QPainter;

class KadiaScene
{
public:
    enum Action {
        MoveUp,
        MoveDown,
        MoveLeft,
        MoveRight,
        Accept,
        Back,
        ToggleGallery,
        CycleSort
    };

    enum Command {
        NoCommand,
        OpenBackgroundSettings,
        OpenKadiaSettings,
        LaunchSelectedGame,
        RunTileAction
    };

    KadiaScene();

    QSize logicalSize() const;
    void setViewportSize(const QSize &size);
    QSize viewportSize() const;
    void update(double dtSeconds);
    void render(QImage &target, const QSize &renderSize = QSize());
    void handle(Action action);
    void cycleWordmarkFont();
    void setControllerConnected(bool connected);
    void setBackgroundImage(const QImage &image);
    void setBackgroundOpacity(qreal opacity);
    Command takePendingCommand();

    bool inLibrary() const;
    QString selectedSectionName() const;
    QString selectedTileName() const;
    QString selectedGamePath() const;
    QString selectedGameSystem() const;
    bool galleryMode() const;

    // Native mouse support. Coordinates are client pixels because the scene
    // renders at the monitor's actual resolution (no 720p intermediate).
    bool hoverAt(const QPointF &point);
    bool clickAt(const QPointF &point);
    bool doubleClickAt(const QPointF &point);
    void wheelAt(const QPointF &point, int delta);

private:
    struct Star {
        float x;
        float y;
        float z;
        float previousZ;
        float warmth;
    };

    struct TileGeometry {
        QRectF rect;
        float selection;
    };

    void resetStars();
    Star makeStar(bool randomDepth);
    void updateStars(double dtSeconds);

    void drawFrame(QPainter &p);
    void drawVistaBackground(QPainter &p);
    void drawStarfield(QPainter &p);
    void drawVignette(QPainter &p);
    void drawTopBar(QPainter &p);
    void drawLogo(QPainter &p);
    void drawHome(QPainter &p);
    void drawLibrary(QPainter &p);
    void drawFooter(QPainter &p);

    void drawCategoryRail(QPainter &p);
    QVector<TileGeometry> tileGeometries(int count, int selected, int previous,
                                         double changeAge, qreal startX, qreal y,
                                         qreal viewportWidth) const;
    void drawTile(QPainter &p, const QRectF &rect, float selection,
                  const QString &icon, const QString &label, int index);
    void drawTileIcon(QPainter &p, const QRectF &rect, const QString &icon,
                      const QString &label, float selection);
    void drawDescriptionPanel(QPainter &p, const QRectF &rect,
                              const QString &title, const QString &sub,
                              const QString &description, bool withPlayHint = false);
    void drawGameCard(QPainter &p, const QRectF &rect, float selection, int index);
    void drawGallery(QPainter &p, const QVector<KadiaGameInfo> &games, qreal opacity);
    QRectF galleryCardRect(int index, int count) const;
    int galleryColumns() const;
    void drawGlassPanel(QPainter &p, const QRectF &rect, qreal radius = 8.0);
    void drawMediaControls(QPainter &p, qreal right, qreal centerY);
    void drawControllerHints(QPainter &p, qreal x, qreal centerY);
    void drawTextShadow(QPainter &p, const QRectF &rect, int flags,
                        const QString &text, const QColor &color,
                        const QPointF &shadowOffset = QPointF(0.0, 2.0));
    void drawMarqueeLine(QPainter &p, const QRectF &rect, const QString &text,
                         const QColor &color, bool animate,
                         const QPointF &shadowOffset = QPointF());
    void drawEllipticGlow(QPainter &p, const QPointF &center, qreal rx, qreal ry,
                          const QColor &inner, const QColor &outer);

    QFont fontForPixelSize(int pixels, int weight = -1, const QString &family = QString()) const;
    QString uiFontFamily() const;
    QString symbolFontFamily() const;
    static qreal easeOutCubic(qreal t);
    static QColor latte(int alpha);

    QSize m_viewportSize;
    QImage m_logo;
    QImage m_logoWhite;
    QImage m_backgroundImage;
    qreal m_backgroundOpacity;
    Command m_pendingCommand;
    QVector<Star> m_stars;
    std::mt19937 m_rng;

    int m_category;
    int m_tile;
    int m_previousTile;
    int m_game;
    int m_previousGame;
    bool m_library;
    bool m_gallery;
    bool m_controllerConnected;
    int m_fontMode;
    QString m_libraryTitle;
    QString m_uiFontFamily;
    QString m_symbolFontFamily;

    double m_time;
    double m_tileChangeAge;
    double m_gameChangeAge;
    double m_categoryChangeAge;
    double m_galleryBlend;
};
