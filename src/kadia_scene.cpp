#include "kadia_scene.h"
#include "ui_model.h"

#include <QDateTime>
#include <QFontDatabase>
#include <QFile>
#include <QtCore/qendian.h>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRadialGradient>
#include <QTransform>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
const int kDesignWidth = 1280;
const int kDesignHeight = 720;

const QRectF kFrameRect(10.0, 10.0, 1260.0, 700.0);
const qreal kShellLeft = 54.0;  // frame inset 10 + shell padding 44
const qreal kShellTop = 38.0;   // frame inset 10 + shell padding 28
const qreal kShellRight = 54.0; // frame inset 10 + shell padding 44
const qreal kTopbarHeight = 74.0;
const qreal kHubLeft = kShellLeft + 74.0;
const qreal kHubTop = kShellTop + kTopbarHeight + 6.0;
const qreal kHubWidth = 1075.0; // min(1180px, 84vw) at 1280x720
const qreal kPanelTotalWidth = 344.0; // 300 content + 44 horizontal padding
const qreal kPanelRightMargin = 24.0;
const qreal kPanelX = kHubLeft + kHubWidth - kPanelRightMargin - kPanelTotalWidth;
const qreal kStripX = kHubLeft;
const qreal kStripWidth = kPanelX - kStripX - 24.0;

static QRectF adjustedRect(const QRectF &r, qreal dx1, qreal dy1, qreal dx2, qreal dy2)
{
    return QRectF(r.left() + dx1, r.top() + dy1,
                  r.width() + dx2 - dx1, r.height() + dy2 - dy1);
}

static QColor withAlpha(const QColor &c, int alpha)
{
    QColor out(c);
    out.setAlpha(alpha);
    return out;
}
}

KadiaScene::KadiaScene()
    : m_rng(0x4B414449u)
    , m_category(0)
    , m_tile(0)
    , m_previousTile(0)
    , m_game(0)
    , m_previousGame(0)
    , m_library(false)
    , m_controllerConnected(false)
    , m_fontMode(0)
    , m_libraryTitle(QStringLiteral("Super Nintendo"))
    , m_uiFontFamily(QStringLiteral("Tahoma"))
    , m_symbolFontFamily(QStringLiteral("Tahoma"))
    , m_time(0.0)
    , m_tileChangeAge(1.0)
    , m_gameChangeAge(1.0)
    , m_categoryChangeAge(1.0)
{
    const QStringList families = QFontDatabase().families();
    if (families.contains(QStringLiteral("Segoe UI")))
        m_uiFontFamily = QStringLiteral("Segoe UI");
    else if (families.contains(QStringLiteral("Corbel")))
        m_uiFontFamily = QStringLiteral("Corbel");
    else if (families.contains(QStringLiteral("Trebuchet MS")))
        m_uiFontFamily = QStringLiteral("Trebuchet MS");

    if (families.contains(QStringLiteral("Segoe UI Symbol")))
        m_symbolFontFamily = QStringLiteral("Segoe UI Symbol");
    else if (families.contains(QStringLiteral("Arial Unicode MS")))
        m_symbolFontFamily = QStringLiteral("Arial Unicode MS");
    else
        m_symbolFontFamily = m_uiFontFamily;

    // Load the exact logo from raw premultiplied ARGB data. This avoids
    // depending on a statically linked PNG imageformat plugin in the XP Qt build.
    QFile logoFile(QStringLiteral(":/assets/kadia_logo.argb"));
    if (logoFile.open(QIODevice::ReadOnly)) {
        const QByteArray raw = logoFile.readAll();
        if (raw.size() >= 8) {
            const uchar *bytes = reinterpret_cast<const uchar *>(raw.constData());
            const quint32 w = qFromLittleEndian<quint32>(bytes);
            const quint32 h = qFromLittleEndian<quint32>(bytes + 4);
            const qint64 required = 8 + static_cast<qint64>(w) * static_cast<qint64>(h) * 4;
            if (w > 0 && h > 0 && required <= raw.size()) {
                QImage wrapped(bytes + 8, static_cast<int>(w), static_cast<int>(h),
                               static_cast<int>(w) * 4, QImage::Format_ARGB32_Premultiplied);
                m_logo = wrapped.copy();
            }
        }
    }
    if (!m_logo.isNull()) {
        m_logoWhite = QImage(m_logo.size(), QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < m_logo.height(); ++y) {
            const QRgb *src = reinterpret_cast<const QRgb *>(m_logo.constScanLine(y));
            QRgb *dst = reinterpret_cast<QRgb *>(m_logoWhite.scanLine(y));
            for (int x = 0; x < m_logo.width(); ++x)
                dst[x] = qRgba(255, 248, 231, qAlpha(src[x]));
        }
    }
    resetStars();
}

QSize KadiaScene::logicalSize() const
{
    return QSize(kDesignWidth, kDesignHeight);
}

void KadiaScene::update(double dtSeconds)
{
    if (dtSeconds < 0.0)
        dtSeconds = 0.0;
    if (dtSeconds > 0.1)
        dtSeconds = 0.1;

    m_time += dtSeconds;
    m_tileChangeAge += dtSeconds;
    m_gameChangeAge += dtSeconds;
    m_categoryChangeAge += dtSeconds;
    updateStars(dtSeconds);
}

void KadiaScene::render(QImage &target)
{
    if (target.size() != logicalSize() || target.format() != QImage::Format_ARGB32_Premultiplied)
        target = QImage(logicalSize(), QImage::Format_ARGB32_Premultiplied);

    target.fill(QColor(0, 0, 0));
    QPainter p(&target);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    drawFrame(p);
}

void KadiaScene::handle(Action action)
{
    if (m_library) {
        if (action == MoveLeft || action == MoveRight) {
            const int count = kadiaGames().size();
            if (count <= 0)
                return;
            m_previousGame = m_game;
            m_game = (m_game + (action == MoveLeft ? -1 : 1) + count) % count;
            m_gameChangeAge = 0.0;
        } else if (action == Back) {
            m_library = false;
            m_tileChangeAge = 1.0;
        }
        return;
    }

    const QVector<KadiaSectionInfo> &sections = kadiaSections();
    if (sections.isEmpty())
        return;

    if (action == MoveUp || action == MoveDown) {
        const int delta = action == MoveUp ? -1 : 1;
        m_category = (m_category + delta + sections.size()) % sections.size();
        m_previousTile = 0;
        m_tile = 0;
        m_categoryChangeAge = 0.0;
        m_tileChangeAge = 0.0;
        return;
    }

    const QVector<KadiaTileInfo> &tiles = sections[m_category].tiles;
    if (tiles.isEmpty())
        return;

    if (action == MoveLeft || action == MoveRight) {
        const int delta = action == MoveLeft ? -1 : 1;
        m_previousTile = m_tile;
        m_tile = (m_tile + delta + tiles.size()) % tiles.size();
        m_tileChangeAge = 0.0;
        return;
    }

    if (action == Accept) {
        const QString section = sections[m_category].name;
        const QString selected = tiles[m_tile].label;
        const bool gameBrowse = section == QStringLiteral("Games") ||
                                section == QStringLiteral("Consoles") ||
                                section == QStringLiteral("Handhelds") ||
                                section == QStringLiteral("Arcade") ||
                                section == QStringLiteral("PC Games") ||
                                section == QStringLiteral("Collections") ||
                                section == QStringLiteral("Recent") ||
                                section == QStringLiteral("Favorites") ||
                                section == QStringLiteral("Achievements");
        const bool homeGame = section == QStringLiteral("Home") &&
                              (selected == QStringLiteral("Continue") ||
                               selected == QStringLiteral("All Games") ||
                               selected == QStringLiteral("Favorites") ||
                               selected == QStringLiteral("Search"));
        if (gameBrowse || homeGame) {
            m_libraryTitle = selected == QStringLiteral("Systems")
                           ? QStringLiteral("Game Library") : selected;
            m_library = true;
            m_gameChangeAge = 1.0;
        }
    }
}

void KadiaScene::cycleWordmarkFont()
{
    m_fontMode = (m_fontMode + 1) % 3;
}

void KadiaScene::setControllerConnected(bool connected)
{
    m_controllerConnected = connected;
}

bool KadiaScene::inLibrary() const
{
    return m_library;
}

QString KadiaScene::selectedSectionName() const
{
    const QVector<KadiaSectionInfo> &sections = kadiaSections();
    if (m_category < 0 || m_category >= sections.size())
        return QString();
    return sections[m_category].name;
}

QString KadiaScene::selectedTileName() const
{
    const QVector<KadiaSectionInfo> &sections = kadiaSections();
    if (m_category < 0 || m_category >= sections.size())
        return QString();
    if (m_tile < 0 || m_tile >= sections[m_category].tiles.size())
        return QString();
    return sections[m_category].tiles[m_tile].label;
}

void KadiaScene::resetStars()
{
    m_stars.clear();
    const int count = 230;
    m_stars.reserve(count);
    for (int i = 0; i < count; ++i)
        m_stars.push_back(makeStar(true));
}

KadiaScene::Star KadiaScene::makeStar(bool randomDepth)
{
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    Star s;
    s.x = (unit(m_rng) - 0.5f) * static_cast<float>(kDesignWidth) * 1.55f;
    s.y = (unit(m_rng) - 0.5f) * static_cast<float>(kDesignHeight) * 1.55f;
    s.z = randomDepth ? 55.0f + unit(m_rng) * (static_cast<float>(kDesignWidth) - 55.0f)
                      : static_cast<float>(kDesignWidth);
    s.previousZ = s.z;
    s.warmth = unit(m_rng);
    return s;
}

void KadiaScene::updateStars(double dtSeconds)
{
    const float delta = static_cast<float>(1.55 * dtSeconds * 60.0);
    for (int i = 0; i < m_stars.size(); ++i) {
        Star &s = m_stars[i];
        s.previousZ = s.z;
        s.z -= delta;
        if (s.z <= 8.0f)
            s = makeStar(false);
    }
}

void KadiaScene::drawFrame(QPainter &p)
{
    QPainterPath framePath;
    framePath.addRoundedRect(kFrameRect, 16.0, 16.0);

    p.save();
    p.setClipPath(framePath);

    QLinearGradient base(kFrameRect.topLeft(), kFrameRect.bottomRight());
    base.setColorAt(0.0, QColor(1, 2, 7));
    base.setColorAt(0.40, QColor(4, 7, 14));
    base.setColorAt(0.74, QColor(9, 13, 21));
    base.setColorAt(1.0, QColor(5, 7, 13));
    p.fillRect(kFrameRect, base);

    drawVistaBackground(p);
    drawStarfield(p);
    drawVignette(p);
    drawTopBar(p);
    if (m_library)
        drawLibrary(p);
    else
        drawHome(p);
    drawFooter(p);
    p.restore();

    p.save();
    p.setPen(QPen(QColor(255, 248, 231, 26), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(kFrameRect, 16.0, 16.0);
    p.restore();
}

void KadiaScene::drawVistaBackground(QPainter &p)
{
    // Literal C++ translation of the four CSS .vista-ribbon ellipses in the
    // reference HTML. The original CSS uses border-top on huge transformed
    // ellipses; here the same percentages, rotations, skew and slow alternate
    // motion are rendered as transformed D3D-presented QPainter arcs.
    const qreal W = kDesignWidth;
    const qreal H = kDesignHeight;

    const qreal hazeT = 0.5 + 0.5 * qSin(m_time * (2.0 * 3.14159265358979323846 / 36.0));
    const qreal hazeDx = (-0.012 + (0.016 + 0.012) * hazeT) * W;
    const qreal hazeDy = (0.0 + (-0.011) * hazeT) * H;
    drawEllipticGlow(p, QPointF(W * 0.80 + hazeDx, H * 0.84 + hazeDy),
                     W * 0.15, H * 0.15,
                     QColor(255, 244, 220, 18), QColor(255, 244, 220, 0));
    drawEllipticGlow(p, QPointF(W * 0.72 + hazeDx * 0.65, H * 0.77 + hazeDy * 0.65),
                     W * 0.28, H * 0.22,
                     QColor(113, 124, 164, 15), QColor(113, 124, 164, 0));
    drawEllipticGlow(p, QPointF(W * 0.46 + hazeDx * 0.25, H * 0.48 + hazeDy * 0.25),
                     W * 0.35, H * 0.27,
                     QColor(34, 42, 60, 20), QColor(34, 42, 60, 0));

    auto alt = [&](qreal seconds) -> qreal {
        return 0.5 + 0.5 * qSin(m_time * (2.0 * 3.14159265358979323846 / seconds));
    };

    auto drawRibbon = [&](qreal leftPct, qreal bottomPct, qreal widthPct, qreal heightPct,
                          qreal t, qreal dx0, qreal dx1, qreal dy0, qreal dy1,
                          qreal rot0, qreal rot1, qreal skew0, qreal skew1,
                          qreal sy0, qreal sy1, int borderR, int borderG, int borderB,
                          qreal alpha0, qreal alpha1, qreal glowAlpha) {
        QRectF r(leftPct * W,
                 H - bottomPct * H - heightPct * H,
                 widthPct * W,
                 heightPct * H);

        const qreal dx = (dx0 + (dx1 - dx0) * t) * W;
        const qreal dy = (dy0 + (dy1 - dy0) * t) * H;
        const qreal rotation = rot0 + (rot1 - rot0) * t;
        const qreal skewDeg = skew0 + (skew1 - skew0) * t;
        const qreal scaleY = sy0 + (sy1 - sy0) * t;
        const qreal opacity = alpha0 + (alpha1 - alpha0) * t;

        const QPointF c = r.center();
        QTransform tr;
        tr.translate(c.x() + dx, c.y() + dy);
        tr.rotate(rotation);
        tr.shear(qTan(qDegreesToRadians(skewDeg)), 0.0);
        tr.scale(1.0, scaleY);
        tr.translate(-c.x(), -c.y());

        p.save();
        p.setTransform(tr, true);
        p.setBrush(Qt::NoBrush);

        // Wide haze under the 1px CSS border simulates the low-alpha fill,
        // box-shadow and screen blending without introducing a blue cast.
        p.setPen(QPen(QColor(borderR, borderG, borderB,
                             qBound(0, static_cast<int>(glowAlpha * opacity * 255.0), 255)),
                      17.0, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(r, 0, 180 * 16);

        p.setPen(QPen(QColor(borderR, borderG, borderB,
                             qBound(0, static_cast<int>(opacity * 255.0), 255)),
                      1.0, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(r, 0, 180 * 16);
        p.restore();
    };

    // r1: left:-22%; bottom:-10%; width:144%; height:34%;
    drawRibbon(-0.22, -0.10, 1.44, 0.34, alt(32.0),
               -0.014, 0.012, 0.010, -0.012,
               -8.0, -6.6, -5.0, -3.0, 0.98, 1.04,
               255, 241, 214, 0.48 * 0.28, 0.66 * 0.28, 0.055);

    // r2: left:-30%; bottom:2%; width:156%; height:25%;
    drawRibbon(-0.30, 0.02, 1.56, 0.25, alt(40.0),
               0.012, -0.014, 0.0, -0.010,
               7.0, 5.5, 7.0, 5.0, 1.0, 1.05,
               170, 182, 220, 0.24 * 0.18, 0.40 * 0.18, 0.040);

    // r3: left:35%; bottom:13%; width:88%; height:40%;
    drawRibbon(0.35, 0.13, 0.88, 0.40, alt(44.0),
               0.0, -0.018, 0.010, -0.011,
               -30.0, -27.0, 0.0, 0.0, 1.0, 1.04,
               239, 229, 204, 0.18 * 0.14, 0.28 * 0.14, 0.032);

    // r4: left:10%; bottom:-1%; width:120%; height:19%;
    drawRibbon(0.10, -0.01, 1.20, 0.19, alt(36.0),
               -0.008, 0.014, 0.0, -0.007,
               -2.0, -1.0, 0.0, 0.0, 1.0, 1.0,
               249, 237, 211, 0.14 * 0.12, 0.22 * 0.12, 0.022);

    // CSS .vista-beam translated directly from right:-10%, bottom:9%, width:88%,
    // height:5px and rotate(-24deg) -> rotate(-21deg).
    const qreal beamT = alt(36.0);
    const qreal beamW = W * 0.88;
    const QRectF beamRect(W * 1.10 - beamW,
                          H - H * 0.09 - 2.5,
                          beamW,
                          5.0);
    const QPointF bc = beamRect.center();
    QTransform beamTransform;
    beamTransform.translate(bc.x() + (-0.015 + 0.030 * beamT) * W,
                            bc.y() + (0.005 - 0.013 * beamT) * H);
    beamTransform.rotate(-24.0 + 3.0 * beamT);
    beamTransform.scale(0.96 + 0.07 * beamT, 1.0);
    beamTransform.translate(-bc.x(), -bc.y());
    p.save();
    p.setTransform(beamTransform, true);
    QLinearGradient beam(beamRect.left(), 0.0, beamRect.right(), 0.0);
    beam.setColorAt(0.00, QColor(168,178,208,0));
    beam.setColorAt(0.14, QColor(168,178,208,5));
    beam.setColorAt(0.45, QColor(191,202,233,24));
    beam.setColorAt(0.54, QColor(255,246,222,54));
    beam.setColorAt(0.63, QColor(163,175,209,20));
    beam.setColorAt(1.00, QColor(163,175,209,0));
    p.setOpacity(0.28 + 0.20 * beamT);
    p.fillRect(beamRect, beam);
    p.restore();

    const qreal glowT = alt(24.0);
    const QPointF convergence(W * 0.90 + (-8.0 + 18.0 * glowT),
                              H * 0.88 + (8.0 - 18.0 * glowT));
    drawEllipticGlow(p, convergence,
                     W * 0.12 * (0.94 + 0.14 * glowT),
                     H * 0.12 * (0.94 + 0.14 * glowT),
                     QColor(255,248,236, static_cast<int>(28 + 18 * glowT)),
                     QColor(82,93,128,0));

    const qreal bloomT = alt(20.0);
    drawEllipticGlow(p,
                     QPointF(W * 0.87 + (-8.0 + 18.0 * bloomT),
                             H * 0.89 + (7.0 - 16.0 * bloomT)),
                     W * 0.045 * (0.92 + 0.14 * bloomT),
                     H * 0.045 * (0.92 + 0.14 * bloomT),
                     QColor(255,248,232, static_cast<int>(42 + 24 * bloomT)),
                     QColor(178,188,218,0));

    const qreal glintT = 0.5 + 0.5 * qSin(m_time * (2.0 * 3.14159265358979323846 / 6.8));
    const qreal glintW = 74.0 * (0.45 + 0.55 * glintT);
    const qreal glintX = W * (1.0 - 0.165) - glintW * 0.5;
    const qreal glintY = H * (1.0 - 0.127);
    QLinearGradient glint(glintX, glintY, glintX + glintW, glintY);
    glint.setColorAt(0.0, QColor(255,247,230,0));
    glint.setColorAt(0.5, QColor(255,247,230, static_cast<int>(20 + 70 * glintT)));
    glint.setColorAt(1.0, QColor(255,247,230,0));
    p.fillRect(QRectF(glintX, glintY, glintW, 1.0), glint);
}

void KadiaScene::drawStarfield(QPainter &p)
{
    const qreal cx = kDesignWidth * 0.5;
    const qreal cy = kDesignHeight * 0.5;
    const qreal focal = 205.0;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    for (int i = 0; i < m_stars.size(); ++i) {
        Star &s = m_stars[i];
        if (s.z <= 8.0f || s.previousZ <= 0.0f)
            continue;

        const qreal sx = (s.x / s.z) * focal + cx;
        const qreal sy = (s.y / s.z) * focal + cy;
        const qreal px = (s.x / s.previousZ) * focal + cx;
        const qreal py = (s.y / s.previousZ) * focal + cy;

        if (sx < -50.0 || sx > kDesignWidth + 50.0 ||
            sy < -50.0 || sy > kDesignHeight + 50.0) {
            s = makeStar(false);
            continue;
        }

        const qreal depth = qBound<qreal>(0.0, 1.0 - s.z / kDesignWidth, 1.0);
        const qreal radius = 0.72 + depth * 2.05;
        const int alpha = qBound(0, static_cast<int>((0.55 + depth * 0.45) * 255.0), 255);

        if (depth > 0.14) {
            const qreal trailScale = 2.0 + depth * 8.0;
            const qreal dx = sx - px;
            const qreal dy = sy - py;
            const QPointF trailStart(sx - dx * trailScale, sy - dy * trailScale);
            p.setPen(QPen(QColor(255, 248, 231,
                                 qMin(235, static_cast<int>((0.24 + depth * 0.68) * 255.0))),
                          0.65 + depth * 1.55, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(trailStart, QPointF(sx, sy));
        }

        const QColor starColor(255, s.warmth > 0.72f ? 242 : 248,
                                s.warmth > 0.72f ? 214 : 231, alpha);
        p.setPen(Qt::NoPen);
        p.setBrush(starColor);
        p.drawEllipse(QPointF(sx, sy), radius, radius);

        if (depth > 0.72) {
            QRadialGradient halo(QPointF(sx, sy), radius * 4.2);
            halo.setColorAt(0.0, QColor(255, 248, 231, 68));
            halo.setColorAt(0.35, QColor(255, 239, 205, 25));
            halo.setColorAt(1.0, QColor(255, 248, 231, 0));
            p.setBrush(halo);
            p.drawEllipse(QPointF(sx, sy), radius * 4.2, radius * 4.2);
        }
    }
    p.restore();
}

void KadiaScene::drawVignette(QPainter &p)
{
    p.save();
    QRadialGradient radial(QPointF(kDesignWidth * 0.5, kDesignHeight * 0.5), 760.0);
    radial.setColorAt(0.0, QColor(0, 0, 0, 0));
    radial.setColorAt(0.72, QColor(0, 0, 0, 4));
    radial.setColorAt(1.0, QColor(0, 0, 0, 36));
    p.fillRect(kFrameRect, radial);

    QLinearGradient lr(kFrameRect.left(), 0.0, kFrameRect.right(), 0.0);
    lr.setColorAt(0.0, QColor(0, 0, 0, 32));
    lr.setColorAt(0.10, QColor(0, 0, 0, 0));
    lr.setColorAt(0.86, QColor(0, 0, 0, 0));
    lr.setColorAt(1.0, QColor(0, 0, 0, 25));
    p.fillRect(kFrameRect, lr);
    p.restore();
}

void KadiaScene::drawTopBar(QPainter &p)
{
    drawLogo(p);

    p.save();
    p.setFont(fontForPixelSize(12, QFont::Normal));
    p.setPen(QColor(255, 248, 231, 185));
    const QString timeText = QDateTime::currentDateTime().toString(QStringLiteral("h:mm AP"));
    p.drawText(QRectF(kDesignWidth - kShellRight - 150.0, kShellTop + 8.0, 150.0, 28.0),
               Qt::AlignRight | Qt::AlignVCenter, timeText);
    p.restore();
}

void KadiaScene::drawLogo(QPainter &p)
{
    const QRectF baseRect(kShellLeft, kShellTop + 1.0, 64.0, 48.0);
    const qreal breath = 1.0 + qSin(m_time * (2.0 * 3.14159265358979323846 / 5.8)) * 0.012;
    QRectF logoRect(baseRect.center().x() - baseRect.width() * breath * 0.5,
                    baseRect.center().y() - baseRect.height() * breath * 0.5,
                    baseRect.width() * breath,
                    baseRect.height() * breath);

    drawEllipticGlow(p, logoRect.center(), 42.0, 31.0,
                     QColor(255, 245, 220, 25), QColor(121, 157, 244, 0));

    if (!m_logo.isNull()) {
        p.save();
        p.setOpacity(0.98);
        p.drawImage(logoRect, m_logo, QRectF(m_logo.rect()));
        p.restore();

        // Vista/7 specular sweep, masked by the exact logo alpha so there
        // is never a rectangular shine box around the mark.
        const qreal phase = std::fmod(m_time, 5.8) / 5.8;
        if (phase > 0.10 && phase < 0.68 && !m_logoWhite.isNull()) {
            const qreal local = (phase - 0.10) / 0.58;
            QImage shineLayer(128, 96, QImage::Format_ARGB32_Premultiplied);
            shineLayer.fill(Qt::transparent);
            QPainter sp(&shineLayer);
            const qreal shineX = -40.0 + local * 208.0;
            QLinearGradient shine(shineX - 24.0, 0.0, shineX + 24.0, 0.0);
            shine.setColorAt(0.0, QColor(255, 255, 255, 0));
            shine.setColorAt(0.5, QColor(255, 255, 255, 82));
            shine.setColorAt(1.0, QColor(255, 255, 255, 0));
            sp.fillRect(shineLayer.rect(), shine);
            sp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            sp.drawImage(QRectF(shineLayer.rect()), m_logoWhite, QRectF(m_logoWhite.rect()));
            sp.end();

            p.save();
            p.setCompositionMode(QPainter::CompositionMode_Screen);
            p.drawImage(logoRect, shineLayer, QRectF(shineLayer.rect()));
            p.restore();
        }

        // Short physical slice glitch every ~15.8 seconds, no RGB split.
        const qreal glitchPhase = std::fmod(m_time, 15.8);
        if (glitchPhase < 0.12) {
            const qreal shifts[3] = { 2.5, -2.0, 1.5 };
            const qreal yFractions[3][2] = { {0.18, 0.30}, {0.48, 0.59}, {0.72, 0.82} };
            for (int i = 0; i < 3; ++i) {
                QRectF target(logoRect.left() + shifts[i],
                              logoRect.top() + logoRect.height() * yFractions[i][0],
                              logoRect.width(),
                              logoRect.height() * (yFractions[i][1] - yFractions[i][0]));
                QRectF source(0.0,
                              m_logo.height() * yFractions[i][0],
                              m_logo.width(),
                              m_logo.height() * (yFractions[i][1] - yFractions[i][0]));
                p.drawImage(target, m_logo, source);
            }
        }
    }

    const qreal copyX = baseRect.right() + 10.0;
    p.save();
    QFont mathery = fontForPixelSize(10, QFont::DemiBold);
    mathery.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
    p.setFont(mathery);
    p.setPen(QColor(255, 248, 231, 145));
    p.drawText(QRectF(copyX, kShellTop + 5.0, 170.0, 14.0), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("MATHERY"));

    QString family;
    if (m_fontMode == 1)
        family = QStringLiteral("Corbel");
    else if (m_fontMode == 2)
        family = QStringLiteral("Trebuchet MS");
    else
        family = uiFontFamily();

    p.setFont(fontForPixelSize(22, QFont::Light, family));
    p.setPen(QColor(255, 248, 231, 235));
    p.drawText(QRectF(copyX, kShellTop + 19.0, 190.0, 30.0), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Kadia!"));
    p.restore();
}

void KadiaScene::drawHome(QPainter &p)
{
    drawCategoryRail(p);

    const QVector<KadiaSectionInfo> &sections = kadiaSections();
    if (m_category < 0 || m_category >= sections.size())
        return;
    const KadiaSectionInfo &section = sections[m_category];
    if (section.tiles.isEmpty())
        return;

    const qreal stripY = kHubTop + 318.0;
    const QVector<TileGeometry> geoms = tileGeometries(section.tiles.size(), m_tile, m_previousTile,
                                                       m_tileChangeAge, kStripX, stripY, kStripWidth);
    p.save();
    p.setClipRect(QRectF(kStripX, stripY - 18.0, kStripWidth, 178.0));
    for (int i = 0; i < geoms.size(); ++i) {
        if (geoms[i].rect.right() < kStripX - 8.0 || geoms[i].rect.left() > kStripX + kStripWidth + 8.0)
            continue;
        const qreal delay = qMin(i, 5) * 0.025;
        const qreal enterT = easeOutCubic(qBound<qreal>(0.0, (m_categoryChangeAge - delay) / 0.34, 1.0));
        QRectF tileRect = geoms[i].rect.translated(0.0, 8.0 * (1.0 - enterT));
        const qreal scale = 0.992 + 0.008 * enterT;
        const QPointF c = tileRect.center();
        tileRect.setSize(QSizeF(tileRect.width() * scale, tileRect.height() * scale));
        tileRect.moveCenter(c);
        p.save();
        p.setOpacity(0.78 + 0.22 * enterT);
        drawTile(p, tileRect, geoms[i].selection,
                 section.tiles[i].icon, section.tiles[i].label, i);
        p.restore();
    }
    p.restore();

    p.save();
    p.setFont(fontForPixelSize(14, QFont::Normal));
    p.setPen(QColor(255, 248, 231, 158));
    p.drawText(QRectF(kStripX, stripY + 164.0, kStripWidth, 24.0),
               Qt::AlignLeft | Qt::AlignVCenter, section.caption);
    p.restore();

    const KadiaTileInfo &info = section.tiles[qBound(0, m_tile, section.tiles.size() - 1)];
    const QRectF panel(kPanelX, kHubTop + 308.0, kPanelTotalWidth, 176.0 + 40.0);
    drawDescriptionPanel(p, panel, info.title, section.name, info.description, false);
}

void KadiaScene::drawLibrary(QPainter &p)
{
    const qreal railTop = kHubTop + 96.0;
    p.save();
    p.setFont(fontForPixelSize(24, QFont::Light));
    p.setPen(QColor(255, 248, 231, 88));
    p.drawText(QRectF(kHubLeft, railTop, 360.0, 34.0), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Library"));

    p.setFont(fontForPixelSize(44, QFont::Light));
    p.setPen(latte(255));
    drawTextShadow(p, QRectF(kHubLeft, railTop + 34.0, 500.0, 51.0),
                   Qt::AlignLeft | Qt::AlignVCenter, m_libraryTitle, latte(255));

    p.setFont(fontForPixelSize(24, QFont::Light));
    p.setPen(QColor(255, 248, 231, 88));
    p.drawText(QRectF(kHubLeft, railTop + 90.0, 360.0, 34.0), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("All Games"));
    p.drawText(QRectF(kHubLeft, railTop + 124.0, 360.0, 34.0), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Favorites"));
    p.drawText(QRectF(kHubLeft, railTop + 158.0, 360.0, 34.0), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Recently Played"));
    p.restore();

    const QVector<KadiaGameInfo> &games = kadiaGames();
    const qreal y = kHubTop + 310.0;
    const qreal gap = 15.0;
    const qreal viewportW = kStripWidth;
    const qreal t = easeOutCubic(qMin<qreal>(1.0, m_gameChangeAge / 0.18));

    QVector<qreal> widths;
    widths.reserve(games.size());
    for (int i = 0; i < games.size(); ++i) {
        qreal sel = 0.0;
        if (m_game == m_previousGame)
            sel = i == m_game ? 1.0 : 0.0;
        else if (i == m_game)
            sel = t;
        else if (i == m_previousGame)
            sel = 1.0 - t;
        widths.push_back(113.0 + (148.0 - 113.0) * sel);
    }

    qreal selectedCenter = 0.0;
    qreal x = 0.0;
    for (int i = 0; i < games.size(); ++i) {
        if (i == m_game)
            selectedCenter = x + widths[i] * 0.5;
        x += widths[i] + gap;
    }
    const qreal totalW = qMax<qreal>(0.0, x - gap);
    const qreal preferred = qMin(viewportW * 0.56, 530.0);
    const qreal scroll = qBound<qreal>(0.0, selectedCenter - preferred, qMax<qreal>(0.0, totalW - viewportW));

    x = kStripX - scroll;
    p.save();
    p.setClipRect(QRectF(kStripX, y - 14.0, viewportW, 230.0));
    for (int i = 0; i < games.size(); ++i) {
        qreal sel = 0.0;
        if (m_game == m_previousGame)
            sel = i == m_game ? 1.0 : 0.0;
        else if (i == m_game)
            sel = t;
        else if (i == m_previousGame)
            sel = 1.0 - t;

        const qreal h = 160.0 + (210.0 - 160.0) * sel;
        const qreal lift = -10.0 * sel;
        const QRectF card(x, y + lift, widths[i], h);
        if (card.right() >= kStripX - 4.0 && card.left() <= kStripX + viewportW + 4.0)
            drawGameCard(p, card, sel, i);
        x += widths[i] + gap;
    }
    p.restore();

    if (!games.isEmpty()) {
        const KadiaGameInfo &game = games[qBound(0, m_game, games.size() - 1)];
        const QRectF panel(kPanelX, kHubTop + 302.0, kPanelTotalWidth, 204.0);
        drawDescriptionPanel(p, panel, game.title, game.subtitle, game.description, true);
    }
}

void KadiaScene::drawFooter(QPainter &p)
{
    const qreal footerCenterY = 660.0; // frame + shell bottom padding + 52px footer
    drawControllerHints(p, kShellLeft, footerCenterY);
    drawMediaControls(p, kDesignWidth - kShellRight, footerCenterY);
}

void KadiaScene::drawCategoryRail(QPainter &p)
{
    const QVector<KadiaSectionInfo> &sections = kadiaSections();
    const int offsets[] = { -2, -1, 0, 1, 2, 3 };
    const qreal yPositions[] = { 0.0, 34.0, 68.0, 120.0, 154.0, 188.0 };
    const qreal top = kHubTop + 56.0;

    p.save();
    for (int pos = 0; pos < 6; ++pos) {
        const int offset = offsets[pos];
        const int index = m_category + offset;
        if (index < 0 || index >= sections.size())
            continue;

        int alpha = 105;
        int px = 24;
        qreal height = 34.0;
        if (offset == 0) {
            alpha = 255;
            px = 44;
            height = 52.0;
        } else if (qAbs(offset) == 1) {
            alpha = 150;
        } else {
            alpha = 70;
        }

        p.setFont(fontForPixelSize(px, QFont::Light));
        const QRectF r(kHubLeft, top + yPositions[pos], 430.0, height);
        const QColor color = QColor(255, 248, 231, alpha);
        drawTextShadow(p, r, Qt::AlignLeft | Qt::AlignVCenter, sections[index].name, color);
    }
    p.restore();
}

QVector<KadiaScene::TileGeometry> KadiaScene::tileGeometries(int count, int selected, int previous,
                                                             double changeAge, qreal startX, qreal y,
                                                             qreal viewportWidth) const
{
    QVector<TileGeometry> result;
    if (count <= 0)
        return result;

    result.resize(count);
    const qreal transition = easeOutCubic(qMin<qreal>(1.0, changeAge / 0.18));
    const qreal gap = 14.0;
    QVector<qreal> widths(count);
    QVector<qreal> heights(count);
    QVector<qreal> selections(count);

    for (int i = 0; i < count; ++i) {
        qreal sel = 0.0;
        if (selected == previous)
            sel = i == selected ? 1.0 : 0.0;
        else if (i == selected)
            sel = transition;
        else if (i == previous)
            sel = 1.0 - transition;
        selections[i] = sel;
        widths[i] = 144.0 + (198.0 - 144.0) * sel;
        heights[i] = 106.0 + (146.0 - 106.0) * sel;
    }

    qreal rawX = 0.0;
    qreal selectedCenter = 0.0;
    for (int i = 0; i < count; ++i) {
        if (i == selected)
            selectedCenter = rawX + widths[i] * 0.5;
        rawX += widths[i] + gap;
    }
    const qreal totalWidth = qMax<qreal>(0.0, rawX - gap);
    const qreal preferredCenter = qMin(viewportWidth * 0.56, 560.0);
    const qreal scroll = qBound<qreal>(0.0, selectedCenter - preferredCenter,
                                      qMax<qreal>(0.0, totalWidth - viewportWidth));

    qreal x = startX - scroll;
    for (int i = 0; i < count; ++i) {
        const qreal lift = -12.0 * selections[i];
        result[i].rect = QRectF(x, y + lift, widths[i], heights[i]);
        result[i].selection = static_cast<float>(selections[i]);
        x += widths[i] + gap;
    }
    return result;
}

void KadiaScene::drawTile(QPainter &p, const QRectF &rect, float selection,
                          const QString &icon, const QString &label, int index)
{
    Q_UNUSED(index);
    p.save();

    QLinearGradient bg(rect.topLeft(), rect.bottomRight());
    bg.setColorAt(0.0, QColor(76, 87, 121, 118));
    bg.setColorAt(0.62, QColor(12, 17, 28, 196));
    bg.setColorAt(1.0, QColor(59, 71, 116, 92));
    p.setBrush(bg);
    p.setPen(QPen(QColor(255, 248, 231, static_cast<int>(26 + 120 * selection)), 1.0));
    p.drawRoundedRect(rect, 3.0, 3.0);

    QLinearGradient spec(rect.left(), rect.top(), rect.left(), rect.top() + rect.height() * 0.42);
    spec.setColorAt(0.0, QColor(255, 255, 255, 36));
    spec.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(spec);
    p.drawRect(QRectF(rect.left() + 1.0, rect.top() + 1.0,
                      rect.width() - 2.0, rect.height() * 0.42));

    drawEllipticGlow(p, QPointF(rect.left() + rect.width() * 0.44,
                                rect.top() + rect.height() * 0.30),
                     rect.width() * 0.24, rect.height() * 0.28,
                     QColor(255, 240, 200, 34), QColor(255, 255, 255, 0));

    p.setFont(fontForPixelSize(static_cast<int>(38.0 + 8.0 * selection), QFont::Normal, symbolFontFamily()));
    p.setPen(QColor(255, 248, 231, 210));
    p.drawText(QRectF(rect.left(), rect.top() + 4.0, rect.width(), rect.height() - 30.0),
               Qt::AlignCenter, icon);

    const qreal labelH = 31.0 + 7.0 * selection;
    QLinearGradient labelBg(rect.left(), rect.bottom() - labelH, rect.left(), rect.bottom());
    labelBg.setColorAt(0.0, QColor(5, 8, 14, 35));
    labelBg.setColorAt(1.0, QColor(5, 8, 14, 245));
    p.fillRect(QRectF(rect.left() + 1.0, rect.bottom() - labelH,
                      rect.width() - 2.0, labelH - 1.0), labelBg);

    p.setFont(fontForPixelSize(static_cast<int>(12.0 + 2.0 * selection), QFont::Normal));
    p.setPen(QColor(255, 248, 231, 235));
    p.drawText(QRectF(rect.left() + 10.0 + 2.0 * selection,
                      rect.bottom() - labelH,
                      rect.width() - 20.0,
                      labelH),
               Qt::AlignLeft | Qt::AlignVCenter, label);

    if (selection > 0.02f) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 248, 231, static_cast<int>(45 + 100 * selection)), 1.0));
        p.drawRoundedRect(adjustedRect(rect, -1.0, -1.0, 1.0, 1.0), 3.0, 3.0);
    }
    p.restore();
}

void KadiaScene::drawDescriptionPanel(QPainter &p, const QRectF &rect,
                                      const QString &title, const QString &sub,
                                      const QString &description, bool withPlayHint)
{
    drawGlassPanel(p, rect, 8.0);
    p.save();
    const qreal x = rect.left() + 22.0;
    const qreal width = rect.width() - 44.0;

    p.setFont(fontForPixelSize(withPlayHint ? 27 : 25, QFont::Light));
    p.setPen(QColor(255, 248, 231, 235));
    p.drawText(QRectF(x, rect.top() + 20.0, width, 34.0), Qt::AlignLeft | Qt::AlignVCenter, title);

    p.setFont(fontForPixelSize(12, QFont::Normal));
    p.setPen(QColor(255, 248, 231, 108));
    p.drawText(QRectF(x, rect.top() + 53.0, width, 20.0), Qt::AlignLeft | Qt::AlignVCenter, sub);

    p.setFont(fontForPixelSize(13, QFont::Normal));
    p.setPen(QColor(255, 248, 231, 174));
    p.drawText(QRectF(x, rect.top() + 83.0, width, withPlayHint ? 62.0 : 86.0),
               Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, description);

    if (withPlayHint) {
        const qreal lineY = rect.bottom() - 50.0;
        p.setPen(QPen(QColor(255, 248, 231, 20), 1.0));
        p.drawLine(QPointF(x, lineY), QPointF(rect.right() - 22.0, lineY));
        p.setPen(QColor(255, 248, 231, 200));
        p.drawText(QRectF(x, lineY + 8.0, width, 24.0), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("Enter / A = Play · Esc / B = Back"));
    }
    p.restore();
}

void KadiaScene::drawGameCard(QPainter &p, const QRectF &rect, float selection, int index)
{
    static const QColor a1[] = {
        QColor(78,87,119), QColor(122,103,67), QColor(77,91,119),
        QColor(120,104,75), QColor(90,98,120), QColor(120,106,79)
    };
    static const QColor a2[] = {
        QColor(22,27,41), QColor(35,38,48), QColor(24,27,36),
        QColor(27,30,38), QColor(26,29,37), QColor(34,38,47)
    };
    static const QColor a3[] = {
        QColor(119,104,71), QColor(68,82,115), QColor(120,106,73),
        QColor(70,80,111), QColor(120,105,72), QColor(70,81,109)
    };

    p.save();
    QLinearGradient bg(rect.topLeft(), rect.bottomRight());
    bg.setColorAt(0.0, a1[index % 6]);
    bg.setColorAt(0.58, a2[index % 6]);
    bg.setColorAt(1.0, a3[index % 6]);
    p.setBrush(bg);
    p.setPen(QPen(QColor(255, 248, 231, static_cast<int>(26 + 120 * selection)), 1.0));
    p.drawRoundedRect(rect, 3.0, 3.0);

    QLinearGradient topSpec(rect.left(), rect.top(), rect.left(), rect.top() + rect.height() * 0.28);
    topSpec.setColorAt(0.0, QColor(255,255,255,28));
    topSpec.setColorAt(1.0, QColor(255,255,255,0));
    p.fillRect(QRectF(rect.left()+1.0, rect.top()+1.0, rect.width()-2.0, rect.height()*0.28), topSpec);

    const qreal labelH = 34.0 + 8.0 * selection;
    QLinearGradient labelGrad(rect.left(), rect.bottom() - labelH, rect.left(), rect.bottom());
    labelGrad.setColorAt(0.0, QColor(5,8,14,40));
    labelGrad.setColorAt(1.0, QColor(5,8,14,245));
    p.fillRect(QRectF(rect.left()+1.0, rect.bottom()-labelH, rect.width()-2.0, labelH-1.0), labelGrad);

    const QVector<KadiaGameInfo> &games = kadiaGames();
    if (index >= 0 && index < games.size()) {
        p.setFont(fontForPixelSize(static_cast<int>(11.0 + 2.0 * selection), QFont::Normal));
        p.setPen(QColor(255,248,231,238));
        p.drawText(QRectF(rect.left()+9.0, rect.bottom()-labelH, rect.width()-18.0, labelH),
                   Qt::AlignLeft | Qt::AlignVCenter, games[index].title);
    }
    p.restore();
}

void KadiaScene::drawGlassPanel(QPainter &p, const QRectF &rect, qreal radius)
{
    p.save();
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    QLinearGradient fill(rect.topLeft(), rect.bottomRight());
    fill.setColorAt(0.0, QColor(255, 255, 255, 10));
    fill.setColorAt(0.35, QColor(10, 14, 23, 205));
    fill.setColorAt(1.0, QColor(5, 8, 14, 225));
    p.setBrush(fill);
    p.setPen(QPen(QColor(255, 248, 231, 26), 1.0));
    p.drawPath(path);

    QLinearGradient top(rect.left(), rect.top(), rect.left(), rect.top() + 42.0);
    top.setColorAt(0.0, QColor(255,255,255,11));
    top.setColorAt(1.0, QColor(255,255,255,0));
    p.setClipPath(path);
    p.fillRect(QRectF(rect.left(), rect.top(), rect.width(), 45.0), top);
    p.restore();
}

void KadiaScene::drawMediaControls(QPainter &p, qreal right, qreal centerY)
{
    p.save();
    const qreal controlH = 42.0;
    const qreal play = 38.0;
    const qreal small = 24.0;
    const qreal gap = 6.0;
    const qreal dividerGap = 10.0;
    const qreal statusWidth = 238.0;
    const qreal mediaWidth = 410.0;
    const qreal mediaRight = right - statusWidth - 18.0;
    qreal x = mediaRight - mediaWidth;
    const qreal y = centerY - controlH * 0.5;
    Q_UNUSED(y);
    const QColor icon = QColor(255,248,231,185);

    auto drawMinus = [&](qreal cx) {
        p.setPen(QPen(icon, 1.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(cx-5, centerY), QPointF(cx+5, centerY));
    };
    auto drawPlus = [&](qreal cx) {
        p.setPen(QPen(icon, 1.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(cx-5, centerY), QPointF(cx+5, centerY));
        p.drawLine(QPointF(cx, centerY-5), QPointF(cx, centerY+5));
    };

    // Dot
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255,248,231,120));
    p.drawEllipse(QPointF(x+small/2, centerY), 3.2, 3.2);
    x += small + gap;

    // List
    p.setPen(QPen(icon, 1.2));
    for (int r = -1; r <= 1; ++r) {
        p.drawEllipse(QPointF(x+4, centerY+r*4), 0.8, 0.8);
        p.drawLine(QPointF(x+7, centerY+r*4), QPointF(x+16, centerY+r*4));
    }
    x += small + gap;
    drawMinus(x+small/2); x += small + gap;
    drawPlus(x+small/2); x += small + dividerGap;

    p.setPen(QPen(QColor(255,248,231,45), 1.0));
    p.drawLine(QPointF(x, centerY-11), QPointF(x, centerY+11));
    x += dividerGap;

    // Stop
    p.setBrush(icon); p.setPen(Qt::NoPen);
    p.drawRect(QRectF(x+8, centerY-4, 8, 8));
    x += small + gap;

    // Previous
    p.setPen(QPen(icon, 1.4));
    p.drawLine(QPointF(x+6, centerY-6), QPointF(x+6, centerY+6));
    QPainterPath prev; prev.moveTo(x+17,centerY-7); prev.lineTo(x+8,centerY); prev.lineTo(x+17,centerY+7); prev.closeSubpath();
    p.fillPath(prev, icon); x += small + gap;

    // Rewind
    QPainterPath rw; rw.moveTo(x+11,centerY-7); rw.lineTo(x+3,centerY); rw.lineTo(x+11,centerY+7); rw.closeSubpath();
    rw.moveTo(x+20,centerY-7); rw.lineTo(x+12,centerY); rw.lineTo(x+20,centerY+7); rw.closeSubpath();
    p.fillPath(rw, icon); x += small + gap;

    // Glass play button
    QRectF playRect(x, centerY-play/2, play, play);
    QRadialGradient playGrad(QPointF(playRect.left()+playRect.width()*0.34, playRect.top()+playRect.height()*0.28), playRect.width()*0.9);
    playGrad.setColorAt(0.0, QColor(255,255,255,110));
    playGrad.setColorAt(0.35, QColor(255,255,255,28));
    playGrad.setColorAt(1.0, QColor(40,46,61,190));
    p.setBrush(playGrad);
    p.setPen(QPen(QColor(255,248,231,72),1.0));
    p.drawEllipse(playRect);
    QPainterPath tri; tri.moveTo(playRect.left()+15,playRect.top()+11); tri.lineTo(playRect.left()+27,centerY); tri.lineTo(playRect.left()+15,playRect.bottom()-11); tri.closeSubpath();
    p.fillPath(tri, QColor(255,248,231,242));
    x += play + gap + 2.0;

    // Fast forward
    QPainterPath ff; ff.moveTo(x+4,centerY-7); ff.lineTo(x+12,centerY); ff.lineTo(x+4,centerY+7); ff.closeSubpath();
    ff.moveTo(x+13,centerY-7); ff.lineTo(x+21,centerY); ff.lineTo(x+13,centerY+7); ff.closeSubpath();
    p.fillPath(ff, icon); x += small + gap;

    // Next
    QPainterPath nx; nx.moveTo(x+5,centerY-7); nx.lineTo(x+14,centerY); nx.lineTo(x+5,centerY+7); nx.closeSubpath();
    p.fillPath(nx, icon); p.setPen(QPen(icon,1.4)); p.drawLine(QPointF(x+17,centerY-6),QPointF(x+17,centerY+6));
    x += small + dividerGap;

    p.setPen(QPen(QColor(255,248,231,45), 1.0));
    p.drawLine(QPointF(x, centerY-11), QPointF(x, centerY+11));
    x += dividerGap;

    // Speaker simplified
    p.setPen(Qt::NoPen); p.setBrush(icon);
    QPainterPath speaker; speaker.moveTo(x+3,centerY-4); speaker.lineTo(x+7,centerY-4); speaker.lineTo(x+12,centerY-8); speaker.lineTo(x+12,centerY+8); speaker.lineTo(x+7,centerY+4); speaker.lineTo(x+3,centerY+4); speaker.closeSubpath();
    p.fillPath(speaker, icon);
    p.setPen(QPen(icon,1.2));
    p.drawArc(QRectF(x+9,centerY-7,10,14), -55*16, 110*16);
    x += small + gap;
    drawMinus(x+small/2); x += small + gap;
    drawPlus(x+small/2);

    // Status text to the far right, matching the HTML footer.
    p.setFont(fontForPixelSize(11, QFont::Normal));
    p.setPen(QColor(255,248,231,115));
    const QString status = QStringLiteral("%1  Keyboard enabled  1080p")
        .arg(m_controllerConnected ? QStringLiteral("Controller connected") : QStringLiteral("Controller disconnected"));
    const QRectF statusRect(right - statusWidth, centerY - 9.0, statusWidth, 18.0);
    p.drawText(statusRect, Qt::AlignRight | Qt::AlignVCenter, status);
    p.restore();
}

void KadiaScene::drawControllerHints(QPainter &p, qreal x, qreal centerY)
{
    p.save();
    p.setFont(fontForPixelSize(11, QFont::Normal));
    p.setPen(QColor(255,248,231,120));

    struct Hint { QString key; QString text; };
    const Hint hints[] = {
        { QStringLiteral("A"), QStringLiteral("Select") },
        { QStringLiteral("B"), QStringLiteral("Back") },
        { QStringLiteral("↔"), QStringLiteral("Browse") },
        { QStringLiteral("▲▼"), QStringLiteral("Sections") }
    };

    qreal cursor = x;
    for (int i = 0; i < 4; ++i) {
        const qreal keyW = hints[i].key.size() > 1 ? 28.0 : 20.0;
        const QRectF keyRect(cursor, centerY - 10.0, keyW, 20.0);
        p.setBrush(QColor(255,255,255,15));
        p.setPen(QPen(QColor(255,248,231,35),1.0));
        p.drawRoundedRect(keyRect, 10.0, 10.0);
        p.setPen(QColor(255,248,231,165));
        p.drawText(keyRect, Qt::AlignCenter, hints[i].key);
        cursor += keyW + 5.0;
        p.setPen(QColor(255,248,231,118));
        p.drawText(QRectF(cursor, centerY-10.0, 62.0, 20.0), Qt::AlignLeft | Qt::AlignVCenter, hints[i].text);
        cursor += 68.0;
    }
    p.restore();
}

void KadiaScene::drawTextShadow(QPainter &p, const QRectF &rect, int flags,
                                const QString &text, const QColor &color,
                                const QPointF &shadowOffset)
{
    p.save();
    p.setPen(QColor(0,0,0,110));
    p.drawText(rect.translated(shadowOffset), flags, text);
    p.setPen(color);
    p.drawText(rect, flags, text);
    p.restore();
}

void KadiaScene::drawEllipticGlow(QPainter &p, const QPointF &center, qreal rx, qreal ry,
                                  const QColor &inner, const QColor &outer)
{
    p.save();
    p.translate(center);
    p.scale(rx, ry);
    QRadialGradient g(QPointF(0.0, 0.0), 1.0);
    g.setColorAt(0.0, inner);
    QColor mid(inner);
    mid.setAlpha(inner.alpha() / 3);
    g.setColorAt(0.45, mid);
    g.setColorAt(1.0, outer);
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(QRectF(-1.0, -1.0, 2.0, 2.0));
    p.restore();
}

QFont KadiaScene::fontForPixelSize(int pixels, int weight, const QString &family) const
{
    QFont font(family.isEmpty() ? uiFontFamily() : family);
    font.setPixelSize(pixels);
    if (weight >= 0)
        font.setWeight(weight);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

QString KadiaScene::uiFontFamily() const
{
    return m_uiFontFamily;
}

QString KadiaScene::symbolFontFamily() const
{
    return m_symbolFontFamily;
}

qreal KadiaScene::easeOutCubic(qreal t)
{
    t = qBound<qreal>(0.0, t, 1.0);
    const qreal u = 1.0 - t;
    return 1.0 - u*u*u;
}

QColor KadiaScene::latte(int alpha)
{
    return QColor(255, 248, 231, qBound(0, alpha, 255));
}
