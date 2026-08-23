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
        Back
    };

    KadiaScene();

    QSize logicalSize() const;
    void update(double dtSeconds);
    void render(QImage &target);
    void handle(Action action);
    void cycleWordmarkFont();
    void setControllerConnected(bool connected);

    bool inLibrary() const;
    QString selectedSectionName() const;
    QString selectedTileName() const;

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
    void drawDescriptionPanel(QPainter &p, const QRectF &rect,
                              const QString &title, const QString &sub,
                              const QString &description, bool withPlayHint = false);
    void drawGameCard(QPainter &p, const QRectF &rect, float selection, int index);
    void drawGlassPanel(QPainter &p, const QRectF &rect, qreal radius = 8.0);
    void drawMediaControls(QPainter &p, qreal right, qreal centerY);
    void drawControllerHints(QPainter &p, qreal x, qreal centerY);
    void drawTextShadow(QPainter &p, const QRectF &rect, int flags,
                        const QString &text, const QColor &color,
                        const QPointF &shadowOffset = QPointF(0.0, 2.0));
    void drawEllipticGlow(QPainter &p, const QPointF &center, qreal rx, qreal ry,
                          const QColor &inner, const QColor &outer);

    QFont fontForPixelSize(int pixels, int weight = -1, const QString &family = QString()) const;
    QString uiFontFamily() const;
    QString symbolFontFamily() const;
    static qreal easeOutCubic(qreal t);
    static QColor latte(int alpha);

    QImage m_logo;
    QImage m_logoWhite;
    QVector<Star> m_stars;
    std::mt19937 m_rng;

    int m_category;
    int m_tile;
    int m_previousTile;
    int m_game;
    int m_previousGame;
    bool m_library;
    bool m_controllerConnected;
    int m_fontMode;
    QString m_libraryTitle;
    QString m_uiFontFamily;
    QString m_symbolFontFamily;

    double m_time;
    double m_tileChangeAge;
    double m_gameChangeAge;
    double m_categoryChangeAge;
};
