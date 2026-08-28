#include "kadia_scene.h"
#include "ui_model.h"
#include "game_stats.h"
#include "kadia_settings.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QImageReader>
#include <QPixmap>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QRunnable>
#include <QThreadPool>
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
// The HTML mockup uses fixed CSS pixel sizes and only viewport-relative widths.
// Keep those exact pixel dimensions and render directly at the monitor's native
// client size instead of drawing a 1280x720 image and upscaling it.
int gCanvasWidth = 1280;
int gCanvasHeight = 720;

const qreal kShellLeft = 54.0;  // frame inset 10 + shell padding 44
const qreal kShellTop = 38.0;   // frame inset 10 + shell padding 28
const qreal kShellRight = 54.0; // frame inset 10 + shell padding 44
const qreal kTopbarHeight = 74.0;
const qreal kHubLeft = kShellLeft + 74.0;
const qreal kHubTop = kShellTop + kTopbarHeight + 6.0;
const qreal kPanelTotalWidth = 344.0; // maximum panel width
const qreal kPanelRightMargin = 24.0;
const qreal kStripX = kHubLeft;

static qreal canvasWidth() { return qMax(320, gCanvasWidth); }
static qreal canvasHeight() { return qMax(240, gCanvasHeight); }
static QRectF frameRect()
{
    return QRectF(10.0, 10.0, qMax<qreal>(1.0, canvasWidth() - 20.0),
                  qMax<qreal>(1.0, canvasHeight() - 20.0));
}
static qreal hubWidth()
{
    // Never let the hub extend past the right shell margin.
    return qMax<qreal>(120.0, qMin<qreal>(1180.0, canvasWidth() - kHubLeft - kShellRight));
}
static bool detailsPanelVisible()
{
    return canvasWidth() >= 700.0;
}
static qreal panelWidth()
{
    // Keep the details panel from stealing the tile strip on 800/1024-wide
    // displays. On very narrow windows it is hidden entirely.
    if (!detailsPanelVisible())
        return 0.0;
    return qBound<qreal>(220.0, canvasWidth() * 0.27, kPanelTotalWidth);
}
static qreal panelRightMargin()
{
    return canvasWidth() < 900.0 ? 14.0 : kPanelRightMargin;
}
static qreal panelX()
{
    return kHubLeft + hubWidth() - panelRightMargin() - panelWidth();
}
static qreal stripWidth()
{
    if (!detailsPanelVisible())
        return qMax<qreal>(48.0, hubWidth());
    return qMax<qreal>(48.0, panelX() - kStripX - 18.0);
}
static qreal footerCenterY()
{
    return canvasHeight() - 60.0;
}
static QRectF settingsButtonRect()
{
    return QRectF(canvasWidth() - kShellRight - 270.0, kShellTop + 5.0, 82.0, 32.0);
}

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

struct CoverCacheEntry
{
    int state; // 0 = not requested, 1 = loading, 2 = complete (possibly empty)
    QImage image;
    CoverCacheEntry() : state(0) {}
};

static QHash<QString, CoverCacheEntry> &coverCache()
{
    static QHash<QString, CoverCacheEntry> cache;
    return cache;
}

static QMutex &coverCacheMutex()
{
    static QMutex mutex;
    return mutex;
}

static QImage decodeCoverThumbnail(const QString &key)
{
    if (!QFileInfo(key).exists())
        return QImage();

    QImageReader reader(key);
    const QSize sourceSize = reader.size();
    if (sourceSize.isValid()) {
        QSize wanted = sourceSize;
        wanted.scale(QSize(320, 420), Qt::KeepAspectRatio);
        if (wanted.isValid() && wanted != sourceSize)
            reader.setScaledSize(wanted);
    }
    return reader.read();
}

class CoverLoadTask : public QRunnable
{
public:
    explicit CoverLoadTask(const QString &key) : m_key(key) {}
    void run() Q_DECL_OVERRIDE
    {
        const QImage image = decodeCoverThumbnail(m_key);
        QMutexLocker locker(&coverCacheMutex());
        CoverCacheEntry &entry = coverCache()[m_key];
        entry.image = image;
        entry.state = 2;
    }
private:
    QString m_key;
};

static QImage coverImageForPath(const QString &path)
{
    const QString key = QDir::cleanPath(path);
    if (key.isEmpty())
        return QImage();

    bool queueLoad = false;
    QImage ready;
    {
        QMutexLocker locker(&coverCacheMutex());
        CoverCacheEntry &entry = coverCache()[key];
        if (entry.state == 2)
            return entry.image;
        if (entry.state == 0) {
            entry.state = 1;
            queueLoad = true;
        }
    }

    if (queueLoad)
        QThreadPool::globalInstance()->start(new CoverLoadTask(key), -1);
    return ready;
}

static QHash<QString, QImage> &brandIconCache()
{
    static QHash<QString, QImage> cache;
    return cache;
}

static const QImage *rawArgbIcon(const QString &resource)
{
    if (resource.isEmpty()) return 0;
    QHash<QString, QImage> &cache = brandIconCache();
    if (!cache.contains(resource)) {
        QImage image;
        QFile file(resource);
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray raw = file.readAll();
            if (raw.size() >= 8) {
                const uchar *bytes = reinterpret_cast<const uchar *>(raw.constData());
                const quint32 w = qFromLittleEndian<quint32>(bytes);
                const quint32 h = qFromLittleEndian<quint32>(bytes + 4);
                const qint64 required = 8 + static_cast<qint64>(w) * static_cast<qint64>(h) * 4;
                if (w > 0 && h > 0 && required <= raw.size()) {
                    QImage wrapped(bytes + 8, static_cast<int>(w), static_cast<int>(h),
                                   static_cast<int>(w) * 4, QImage::Format_ARGB32_Premultiplied);
                    image = wrapped.copy();
                }
            }
        }
        cache.insert(resource, image);
    }
    const QImage &image = cache[resource];
    return image.isNull() ? 0 : &image;
}

static QString consoleBrandResource(const QString &section, const QString &label)
{
    const QString sectionKey = section.toLower();
    const bool platformSection = sectionKey == QStringLiteral("consoles") ||
                                 sectionKey == QStringLiteral("handhelds") ||
                                 sectionKey == QStringLiteral("computers") ||
                                 sectionKey == QStringLiteral("arcade");
    if (!platformSection)
        return QString();

    const QString l = label.toLower();
    // Every concrete platform exposed by Kadia has a dedicated local icon
    // resource. There is deliberately no manufacturer-wide/generic console
    // fallback: selecting N64, Saturn, Lynx, etc. always shows that platform's
    // own mark/pictogram.
    if (l == QStringLiteral("nintendo entertainment system")) return QStringLiteral(":/assets/console_icons/nes.argb");
    if (l == QStringLiteral("super nintendo")) return QStringLiteral(":/assets/console_icons/snes.argb");
    if (l == QStringLiteral("nintendo 64")) return QStringLiteral(":/assets/console_icons/n64.argb");
    if (l == QStringLiteral("nintendo gamecube")) return QStringLiteral(":/assets/console_icons/gamecube.argb");
    if (l == QStringLiteral("nintendo wii")) return QStringLiteral(":/assets/console_icons/wii.argb");
    if (l == QStringLiteral("nintendo wii u")) return QStringLiteral(":/assets/console_icons/wiiu.argb");
    if (l == QStringLiteral("nintendo switch")) return QStringLiteral(":/assets/console_icons/switch.argb");
    if (l == QStringLiteral("sega master system")) return QStringLiteral(":/assets/console_icons/mastersystem.argb");
    if (l == QStringLiteral("sega genesis / mega drive")) return QStringLiteral(":/assets/console_icons/genesis.argb");
    if (l == QStringLiteral("sega saturn")) return QStringLiteral(":/assets/console_icons/saturn.argb");
    if (l == QStringLiteral("sega dreamcast")) return QStringLiteral(":/assets/console_icons/dreamcast.argb");
    if (l == QStringLiteral("playstation")) return QStringLiteral(":/assets/console_icons/playstation.argb");
    if (l == QStringLiteral("playstation 2")) return QStringLiteral(":/assets/console_icons/playstation2.argb");
    if (l == QStringLiteral("playstation 3")) return QStringLiteral(":/assets/console_icons/playstation3.argb");
    if (l == QStringLiteral("xbox")) return QStringLiteral(":/assets/console_icons/xbox.argb");
    if (l == QStringLiteral("xbox 360")) return QStringLiteral(":/assets/console_icons/xbox360.argb");
    if (l == QStringLiteral("atari 2600")) return QStringLiteral(":/assets/console_icons/atari2600.argb");
    if (l == QStringLiteral("atari 5200")) return QStringLiteral(":/assets/console_icons/atari5200.argb");
    if (l == QStringLiteral("atari 7800")) return QStringLiteral(":/assets/console_icons/atari7800.argb");
    if (l == QStringLiteral("pc engine / turbografx-16")) return QStringLiteral(":/assets/console_icons/pcengine.argb");
    if (l == QStringLiteral("neo geo")) return QStringLiteral(":/assets/console_icons/neogeo.argb");

    if (l == QStringLiteral("game boy")) return QStringLiteral(":/assets/console_icons/gameboy.argb");
    if (l == QStringLiteral("game boy color")) return QStringLiteral(":/assets/console_icons/gameboycolor.argb");
    if (l == QStringLiteral("game boy advance")) return QStringLiteral(":/assets/console_icons/gba.argb");
    if (l == QStringLiteral("nintendo ds")) return QStringLiteral(":/assets/console_icons/nds.argb");
    if (l == QStringLiteral("nintendo 3ds")) return QStringLiteral(":/assets/console_icons/n3ds.argb");
    if (l == QStringLiteral("playstation portable")) return QStringLiteral(":/assets/console_icons/psp.argb");
    if (l == QStringLiteral("playstation vita")) return QStringLiteral(":/assets/console_icons/psvita.argb");
    if (l == QStringLiteral("sega game gear")) return QStringLiteral(":/assets/console_icons/gamegear.argb");
    if (l == QStringLiteral("atari lynx")) return QStringLiteral(":/assets/console_icons/atarilynx.argb");
    if (l == QStringLiteral("neo geo pocket")) return QStringLiteral(":/assets/console_icons/ngp.argb");
    if (l == QStringLiteral("neo geo pocket color")) return QStringLiteral(":/assets/console_icons/ngpc.argb");
    if (l == QStringLiteral("wonderswan")) return QStringLiteral(":/assets/console_icons/wonderswan.argb");
    if (l == QStringLiteral("wonderswan color")) return QStringLiteral(":/assets/console_icons/wonderswancolor.argb");

    if (l == QStringLiteral("msx")) return QStringLiteral(":/assets/console_icons/msx.argb");
    if (l == QStringLiteral("commodore 64")) return QStringLiteral(":/assets/console_icons/c64.argb");
    if (l == QStringLiteral("amiga")) return QStringLiteral(":/assets/console_icons/amiga.argb");
    if (l == QStringLiteral("dos / pc")) return QStringLiteral(":/assets/console_icons/dospc.argb");

    if (l == QStringLiteral("arcade")) return QStringLiteral(":/assets/console_icons/arcade.argb");
    if (l == QStringLiteral("mame")) return QStringLiteral(":/assets/console_icons/mame.argb");
    if (l == QStringLiteral("fbneo")) return QStringLiteral(":/assets/console_icons/fbneo.argb");
    return QString();
}
static const qreal kCarouselBaseWidth = 118.0;
static const qreal kCarouselSelectedExtra = 54.0;

static qreal carouselSelection(int index, int selected, int previous, qreal transition)
{
    if (selected == previous)
        return index == selected ? 1.0 : 0.0;
    if (index == selected)
        return transition;
    if (index == previous)
        return 1.0 - transition;
    return 0.0;
}

static qreal carouselCardWidth(qreal selection)
{
    // Do not inspect every cover just to lay out the carousel. Loading cover
    // files here used to synchronously decode the entire library the first
    // time a collection was opened. Only visible cards load their artwork.
    return kCarouselBaseWidth + kCarouselSelectedExtra * qBound<qreal>(0.0, selection, 1.0);
}

static qreal carouselRawLeft(int index, int selected, int previous, qreal transition, qreal gap)
{
    qreal x = index * (kCarouselBaseWidth + gap);
    if (selected == previous) {
        if (selected < index)
            x += kCarouselSelectedExtra;
        return x;
    }
    if (selected < index)
        x += kCarouselSelectedExtra * transition;
    if (previous < index)
        x += kCarouselSelectedExtra * (1.0 - transition);
    return x;
}

static qreal carouselTotalWidth(int count, qreal gap)
{
    if (count <= 0)
        return 0.0;
    // There is always exactly one selected-width delta distributed between
    // the old and new selection during the animation.
    return count * kCarouselBaseWidth + kCarouselSelectedExtra + (count - 1) * gap;
}
}

KadiaScene::KadiaScene()
    : m_viewportSize(1280, 720)
    , m_backgroundOpacity(1.0)
    , m_pendingCommand(NoCommand)
    , m_rng(0x4B414449u)
    , m_category(0)
    , m_tile(0)
    , m_previousTile(0)
    , m_game(0)
    , m_previousGame(0)
    , m_library(false)
    , m_gallery(false)
    , m_controllerConnected(false)
    , m_fontMode(0)
    , m_libraryTitle(QStringLiteral("Super Nintendo"))
    , m_uiFontFamily(QStringLiteral("Tahoma"))
    , m_symbolFontFamily(QStringLiteral("Tahoma"))
    , m_time(0.0)
    , m_tileChangeAge(1.0)
    , m_gameChangeAge(1.0)
    , m_categoryChangeAge(1.0)
    , m_galleryBlend(0.0)
{
    setKadiaGameSort(static_cast<KadiaGameSort>(KadiaSettings::defaultGallerySort()));
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
    return QSize(1280, 720);
}

void KadiaScene::setViewportSize(const QSize &size)
{
    const QSize safe(qMax(320, size.width()), qMax(240, size.height()));
    if (safe == m_viewportSize)
        return;

    m_viewportSize = safe;
    gCanvasWidth = safe.width();
    gCanvasHeight = safe.height();
    resetStars();
}

QSize KadiaScene::viewportSize() const
{
    return m_viewportSize;
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

    // Sections can disappear after a library refresh when their last game is
    // removed/reclassified. Keep navigation indices valid immediately.
    const QVector<KadiaSectionInfo> &sections = kadiaSections();
    if (!sections.isEmpty()) {
        m_category = qBound(0, m_category, sections.size() - 1);
        const int tileCount = sections.at(m_category).tiles.size();
        m_tile = tileCount > 0 ? qBound(0, m_tile, tileCount - 1) : 0;
        m_previousTile = tileCount > 0 ? qBound(0, m_previousTile, tileCount - 1) : 0;
    } else {
        m_category = m_tile = m_previousTile = 0;
    }

    const double galleryTarget = m_gallery ? 1.0 : 0.0;
    const double speed = qMin(1.0, dtSeconds * 7.5);
    m_galleryBlend += (galleryTarget - m_galleryBlend) * speed;
    if (qAbs(m_galleryBlend - galleryTarget) < 0.002) m_galleryBlend = galleryTarget;
    updateStars(dtSeconds);
}

void KadiaScene::render(QImage &target, const QSize &renderSize)
{
    const QSize viewport = m_viewportSize.isValid() ? m_viewportSize : logicalSize();
    QSize targetSize = renderSize.isValid() ? renderSize : viewport;
    targetSize.setWidth(qMax(320, targetSize.width()));
    targetSize.setHeight(qMax(180, targetSize.height()));
    if (target.size() != targetSize || target.format() != QImage::Format_ARGB32_Premultiplied)
        target = QImage(targetSize, QImage::Format_ARGB32_Premultiplied);

    // Keep all interactive UI, text, covers and one-pixel decoration at the
    // native render size. Only the decorative moving backdrop is rasterized at
    // a capped resolution on large monitors. This preserves the performance
    // benefit of a smaller software QPainter workload without ever upscaling
    // the interface itself (the source of the blurry previous build).
    QSize backdropSize = targetSize;
    const QSize backdropCap(1920, 1080);
    if (backdropSize.width() > backdropCap.width() || backdropSize.height() > backdropCap.height())
        backdropSize.scale(backdropCap, Qt::KeepAspectRatio);
    backdropSize.setWidth(qMax(320, backdropSize.width()));
    backdropSize.setHeight(qMax(180, backdropSize.height()));

    if (m_backdropBuffer.size() != backdropSize ||
        m_backdropBuffer.format() != QImage::Format_ARGB32_Premultiplied)
        m_backdropBuffer = QImage(backdropSize, QImage::Format_ARGB32_Premultiplied);
    m_backdropBuffer.fill(QColor(0, 0, 0));

    {
        QPainter backgroundPainter(&m_backdropBuffer);
        backgroundPainter.setRenderHint(QPainter::Antialiasing, true);
        backgroundPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const qreal bx = static_cast<qreal>(backdropSize.width()) / qMax(1, viewport.width());
        const qreal by = static_cast<qreal>(backdropSize.height()) / qMax(1, viewport.height());
        backgroundPainter.scale(bx, by);
        drawBackdrop(backgroundPainter);
    }

    target.fill(QColor(0, 0, 0));
    QPainter p(&target);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(QRectF(0.0, 0.0, targetSize.width(), targetSize.height()), m_backdropBuffer);

    const qreal sx = static_cast<qreal>(targetSize.width()) / qMax(1, viewport.width());
    const qreal sy = static_cast<qreal>(targetSize.height()) / qMax(1, viewport.height());
    p.scale(sx, sy);
    drawForeground(p);
}

void KadiaScene::handle(Action action)
{
    if (m_library) {
        const int count = kadiaGames().size();
        if (action == ToggleGallery) {
            m_gallery = !m_gallery;
            m_gameChangeAge = 0.0;
            return;
        }
        if (action == CycleSort) {
            setKadiaGameSort(static_cast<KadiaGameSort>((static_cast<int>(kadiaGameSort()) + 1) % 5));
            m_game = 0;
            m_previousGame = 0;
            m_gameChangeAge = 0.0;
            return;
        }
        if (action == Back) {
            if (m_gallery) {
                m_gallery = false;
                return;
            }
            m_library = false;
            m_tileChangeAge = 1.0;
            return;
        }
        if (action == Accept) {
            if (count > 0)
                m_pendingCommand = LaunchSelectedGame;
            return;
        }
        if (count <= 0)
            return;

        int delta = 0;
        if (m_gallery) {
            const int columns = galleryColumns();
            if (action == MoveLeft) delta = -1;
            else if (action == MoveRight) delta = 1;
            else if (action == MoveUp) delta = -columns;
            else if (action == MoveDown) delta = columns;
        } else {
            if (action == MoveLeft) delta = -1;
            else if (action == MoveRight) delta = 1;
            else if (action == MoveUp) {
                setKadiaGameSort(static_cast<KadiaGameSort>((static_cast<int>(kadiaGameSort()) + 4) % 5));
                m_game = 0; m_previousGame = 0; return;
            } else if (action == MoveDown) {
                setKadiaGameSort(static_cast<KadiaGameSort>((static_cast<int>(kadiaGameSort()) + 1) % 5));
                m_game = 0; m_previousGame = 0; return;
            }
        }
        if (delta != 0) {
            m_previousGame = m_game;
            m_game = qBound(0, m_game + delta, count - 1);
            m_gameChangeAge = 0.0;
        }
        return;
    }

    const QVector<KadiaSectionInfo> &sections = kadiaSections();
    if (sections.isEmpty())
        return;

    if (action == ToggleGallery) {
        const QString section = sections[m_category].name;
        const bool browseSection = section == QStringLiteral("Home") || section == QStringLiteral("Games") ||
                                   section == QStringLiteral("Consoles") || section == QStringLiteral("Handhelds") ||
                                   section == QStringLiteral("Computers") || section == QStringLiteral("Arcade") || section == QStringLiteral("PC Games") ||
                                   section == QStringLiteral("Collections") || section == QStringLiteral("Recent") ||
                                   section == QStringLiteral("Favorites") || section == QStringLiteral("Achievements");
        if (browseSection) {
            QString filter = selectedTileName();
            if (filter.isEmpty() || section == QStringLiteral("Home")) filter = QStringLiteral("All Games");
            m_libraryTitle = filter;
            setKadiaActiveGameFilter(filter);
            m_game = m_previousGame = 0;
            m_library = true;
            m_gallery = true;
            m_gameChangeAge = 1.0;
        }
        return;
    }

    if (action == CycleSort) {
        setKadiaGameSort(static_cast<KadiaGameSort>((static_cast<int>(kadiaGameSort()) + 1) % 5));
        return;
    }

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
        if (selected == QStringLiteral("Background")) {
            m_pendingCommand = OpenBackgroundSettings;
            return;
        }
        if ((section == QStringLiteral("System Settings") && selected == QStringLiteral("Information")) ||
            (section == QStringLiteral("Tasks") && selected == QStringLiteral("Settings"))) {
            m_pendingCommand = OpenKadiaSettings;
            return;
        }
        if (section == QStringLiteral("Games") && selected == QStringLiteral("Sort")) {
            handle(CycleSort); return;
        }
        if (section == QStringLiteral("Games") && selected == QStringLiteral("View Style")) {
            m_libraryTitle = QStringLiteral("All Games"); setKadiaActiveGameFilter(m_libraryTitle);
            m_game = m_previousGame = 0; m_library = true; m_gallery = true; m_gameChangeAge = 1.0; return;
        }
        if (section == QStringLiteral("Games") && selected == QStringLiteral("Random Game")) {
            m_libraryTitle = QStringLiteral("All Games"); setKadiaActiveGameFilter(m_libraryTitle);
            const int count = kadiaGames().size();
            m_game = count > 0 ? static_cast<int>(m_rng() % static_cast<unsigned int>(count)) : 0;
            m_previousGame = m_game; m_library = true; m_gallery = false; m_gameChangeAge = 1.0; return;
        }
        const bool gameBrowse = section == QStringLiteral("Games") || section == QStringLiteral("Consoles") ||
                                section == QStringLiteral("Handhelds") || section == QStringLiteral("Computers") ||
                                section == QStringLiteral("Arcade") ||
                                section == QStringLiteral("PC Games") || section == QStringLiteral("Collections") ||
                                section == QStringLiteral("Recent") || section == QStringLiteral("Favorites") ||
                                section == QStringLiteral("Achievements");
        const bool homeGame = section == QStringLiteral("Home") &&
                              (selected == QStringLiteral("Continue") || selected == QStringLiteral("All Games") ||
                               selected == QStringLiteral("Favorites") || selected == QStringLiteral("Search"));
        if (gameBrowse || homeGame) {
            m_libraryTitle = selected == QStringLiteral("Systems") ? QStringLiteral("Game Library") : selected;
            setKadiaActiveGameFilter(m_libraryTitle);
            m_game = m_previousGame = 0;
            m_library = true;
            m_gallery = false;
            m_gameChangeAge = 1.0;
        } else {
            m_pendingCommand = RunTileAction;
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

void KadiaScene::setBackgroundImage(const QImage &image)
{
    m_backgroundImage = image;
}

void KadiaScene::setBackgroundOpacity(qreal opacity)
{
    m_backgroundOpacity = qBound<qreal>(0.0, opacity, 1.0);
}

KadiaScene::Command KadiaScene::takePendingCommand()
{
    const Command command = m_pendingCommand;
    m_pendingCommand = NoCommand;
    return command;
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

QString KadiaScene::selectedGamePath() const
{
    const QVector<KadiaGameInfo> &games = kadiaGames();
    if (m_game < 0 || m_game >= games.size()) return QString();
    return games.at(m_game).path;
}

QString KadiaScene::selectedGameSystem() const
{
    const QVector<KadiaGameInfo> &games = kadiaGames();
    if (m_game < 0 || m_game >= games.size()) return QString();
    return games.at(m_game).system;
}

bool KadiaScene::galleryMode() const
{
    return m_gallery;
}

bool KadiaScene::hoverAt(const QPointF &point)
{
    if (m_library) {
        const QVector<KadiaGameInfo> &games = kadiaGames();
        if (games.isEmpty())
            return false;

        if (m_gallery || m_galleryBlend > 0.45) {
            const int columns = galleryColumns();
            const int selectedRow = qMax(0, m_game / columns);
            const int firstRow = qMax(0, selectedRow - 1);
            const int first = firstRow * columns;
            const int last = qMin(games.size() - 1, (firstRow + 2) * columns - 1);
            for (int i = first; i <= last; ++i) {
                const QRectF card = galleryCardRect(i, games.size());
                if (card.isValid() && card.contains(point)) {
                    if (m_game != i) {
                        m_previousGame = m_game;
                        m_game = i;
                        m_gameChangeAge = 0.0;
                    }
                    return true;
                }
            }
            return false;
        }

        const qreal y = kHubTop + 310.0;
        const qreal gap = 15.0;
        const qreal viewportW = stripWidth();
        const qreal t = easeOutCubic(qMin<qreal>(1.0, m_gameChangeAge / 0.18));

        const qreal selectedSel = carouselSelection(m_game, m_game, m_previousGame, t);
        const qreal selectedCenter = carouselRawLeft(m_game, m_game, m_previousGame, t, gap) +
                                     carouselCardWidth(selectedSel) * 0.5;
        const qreal totalW = carouselTotalWidth(games.size(), gap);
        const qreal preferred = qMin(viewportW * 0.56, 530.0);
        const qreal scroll = qBound<qreal>(0.0, selectedCenter - preferred,
                                          qMax<qreal>(0.0, totalW - viewportW));

        const int first = qMax(0, static_cast<int>(scroll / (kCarouselBaseWidth + gap)) - 2);
        const int last = qMin(games.size() - 1,
                              static_cast<int>((scroll + viewportW) / (kCarouselBaseWidth + gap)) + 3);
        for (int i = first; i <= last; ++i) {
            const qreal sel = carouselSelection(i, m_game, m_previousGame, t);
            const qreal width = carouselCardWidth(sel);
            const qreal x = kStripX - scroll + carouselRawLeft(i, m_game, m_previousGame, t, gap);
            const qreal h = 160.0 + (210.0 - 160.0) * sel;
            const qreal lift = -10.0 * sel;
            const QRectF card(x, y + lift, width, h);
            if (card.contains(point)) {
                if (m_game != i) {
                    m_previousGame = m_game;
                    m_game = i;
                    m_gameChangeAge = 0.0;
                }
                return true;
            }
        }
        return false;
    }

    if (settingsButtonRect().contains(point))
        return true;

    const QVector<KadiaSectionInfo> &sections = kadiaSections();
    if (sections.isEmpty())
        return false;

    // Category rail hit testing mirrors drawCategoryRail().
    const int offsets[] = { -2, -1, 0, 1, 2, 3 };
    const qreal yPositions[] = { 0.0, 34.0, 68.0, 120.0, 154.0, 188.0 };
    const qreal railTop = kHubTop + 56.0;
    for (int pos = 0; pos < 6; ++pos) {
        const int index = m_category + offsets[pos];
        if (index < 0 || index >= sections.size())
            continue;
        const qreal h = offsets[pos] == 0 ? 52.0 : 34.0;
        const QRectF r(kHubLeft, railTop + yPositions[pos], 430.0, h);
        if (r.contains(point)) {
            if (index != m_category) {
                m_category = index;
                m_previousTile = 0;
                m_tile = 0;
                m_categoryChangeAge = 0.0;
                m_tileChangeAge = 0.0;
            }
            return true;
        }
    }

    if (m_category < 0 || m_category >= sections.size())
        return false;
    const KadiaSectionInfo &section = sections[m_category];
    if (section.tiles.isEmpty())
        return false;

    const qreal stripY = kHubTop + 318.0;
    const QVector<TileGeometry> geoms = tileGeometries(section.tiles.size(), m_tile, m_previousTile,
                                                       m_tileChangeAge, kStripX, stripY, stripWidth());
    for (int i = 0; i < geoms.size(); ++i) {
        if (geoms[i].rect.contains(point)) {
            if (m_tile != i) {
                m_previousTile = m_tile;
                m_tile = i;
                m_tileChangeAge = 0.0;
            }
            return true;
        }
    }
    return false;
}

bool KadiaScene::clickAt(const QPointF &point)
{
    if (!m_library && settingsButtonRect().contains(point)) {
        m_pendingCommand = OpenKadiaSettings;
        return true;
    }
    if (!hoverAt(point))
        return false;

    if (!m_library) {
        // A mouse click on the tile row behaves like A/Enter. Category clicks
        // only change the active vertical section.
        const qreal stripTop = kHubTop + 286.0;
        const qreal stripBottom = kHubTop + 492.0;
        if (point.y() >= stripTop && point.y() <= stripBottom)
            handle(Accept);
    }
    return true;
}

bool KadiaScene::doubleClickAt(const QPointF &point)
{
    if (!hoverAt(point))
        return false;
    if (m_library)
        handle(Accept);
    return true;
}

void KadiaScene::wheelAt(const QPointF &point, int delta)
{
    if (delta == 0)
        return;

    if (m_library) {
        handle(delta > 0 ? (m_gallery ? MoveUp : MoveLeft) : (m_gallery ? MoveDown : MoveRight));
        return;
    }

    const qreal stripTop = kHubTop + 285.0;
    const qreal stripBottom = kHubTop + 510.0;
    if (point.y() >= stripTop && point.y() <= stripBottom)
        handle(delta > 0 ? MoveLeft : MoveRight);
    else
        handle(delta > 0 ? MoveUp : MoveDown);
}

void KadiaScene::resetStars()
{
    m_stars.clear();
    const int count = qBound(230, static_cast<int>((canvasWidth() * canvasHeight()) / 5200.0), 720);
    m_stars.reserve(count);
    for (int i = 0; i < count; ++i)
        m_stars.push_back(makeStar(true));
}

KadiaScene::Star KadiaScene::makeStar(bool randomDepth)
{
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    Star s;
    s.x = (unit(m_rng) - 0.5f) * static_cast<float>(canvasWidth()) * 1.55f;
    s.y = (unit(m_rng) - 0.5f) * static_cast<float>(canvasHeight()) * 1.55f;
    const float nearPlane = 110.0f;
    s.z = randomDepth ? nearPlane + unit(m_rng) * (static_cast<float>(canvasWidth()) - nearPlane)
                      : static_cast<float>(canvasWidth());
    s.previousZ = s.z;
    s.warmth = unit(m_rng);
    return s;
}

void KadiaScene::updateStars(double dtSeconds)
{
    const float delta = static_cast<float>(0.95 * dtSeconds * 60.0);
    for (int i = 0; i < m_stars.size(); ++i) {
        Star &s = m_stars[i];
        s.previousZ = s.z;
        s.z -= delta;
        if (s.z <= 28.0f)
            s = makeStar(false);
    }
}

void KadiaScene::drawBackdrop(QPainter &p)
{
    QPainterPath framePath;
    framePath.addRoundedRect(frameRect(), 16.0, 16.0);

    p.save();
    p.setClipPath(framePath);

    QLinearGradient base(frameRect().topLeft(), frameRect().bottomRight());
    base.setColorAt(0.0, QColor(1, 2, 7));
    base.setColorAt(0.40, QColor(4, 7, 14));
    base.setColorAt(0.74, QColor(9, 13, 21));
    base.setColorAt(1.0, QColor(5, 7, 13));
    p.fillRect(frameRect(), base);

    if (!m_backgroundImage.isNull() && m_backgroundOpacity > 0.001) {
        const QRectF target = frameRect();
        const QSizeF imageSize = m_backgroundImage.size();
        const qreal sx = target.width() / imageSize.width();
        const qreal sy = target.height() / imageSize.height();
        const qreal scale = qMax(sx, sy);
        const qreal sourceW = target.width() / scale;
        const qreal sourceH = target.height() / scale;
        const QRectF source((imageSize.width() - sourceW) * 0.5,
                            (imageSize.height() - sourceH) * 0.5,
                            sourceW, sourceH);
        p.save();
        p.setOpacity(m_backgroundOpacity);
        p.drawImage(target, m_backgroundImage, source);
        p.restore();
    }

    drawVistaBackground(p);
    drawStarfield(p);
    drawVignette(p);
    p.restore();
}

void KadiaScene::drawForeground(QPainter &p)
{
    QPainterPath framePath;
    framePath.addRoundedRect(frameRect(), 16.0, 16.0);

    p.save();
    p.setClipPath(framePath);
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
    p.drawRoundedRect(frameRect(), 16.0, 16.0);
    p.restore();
}

void KadiaScene::drawVistaBackground(QPainter &p)
{
    // Literal C++ translation of the four CSS .vista-ribbon ellipses in the
    // reference HTML. The original CSS uses border-top on huge transformed
    // ellipses; here the same percentages, rotations, skew and slow alternate
    // motion are rendered as transformed D3D-presented QPainter arcs.
    const qreal W = canvasWidth();
    const qreal H = canvasHeight();

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
    const qreal cx = canvasWidth() * 0.5;
    const qreal cy = canvasHeight() * 0.5;
    const qreal focal = 205.0;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    for (int i = 0; i < m_stars.size(); ++i) {
        Star &s = m_stars[i];
        if (s.z <= 28.0f || s.previousZ <= 0.0f)
            continue;

        const qreal sx = (s.x / s.z) * focal + cx;
        const qreal sy = (s.y / s.z) * focal + cy;
        const qreal px = (s.x / s.previousZ) * focal + cx;
        const qreal py = (s.y / s.previousZ) * focal + cy;

        if (!std::isfinite(static_cast<double>(sx)) || !std::isfinite(static_cast<double>(sy)) ||
            !std::isfinite(static_cast<double>(px)) || !std::isfinite(static_cast<double>(py)) ||
            sx < -40.0 || sx > canvasWidth() + 40.0 ||
            sy < -40.0 || sy > canvasHeight() + 40.0) {
            s = makeStar(false);
            continue;
        }

        const qreal depth = qBound<qreal>(0.0, 1.0 - s.z / canvasWidth(), 1.0);
        const qreal radius = 0.50 + depth * 1.45;
        const int alpha = qBound(0, static_cast<int>((0.48 + depth * 0.46) * 255.0), 255);

        // Windows 98-style streaks, but clamp their physical length. The old
        // code extrapolated by up to 10x and could create full-screen lines
        // when a star was close to the camera.
        const qreal dx = sx - px;
        const qreal dy = sy - py;
        const qreal motion = qSqrt(dx * dx + dy * dy);
        if (depth > 0.14 && motion > 0.02) {
            const qreal maxTrail = 6.0 + depth * 30.0;
            const qreal trailLength = qMin(maxTrail, motion * (2.4 + depth * 1.6));
            const qreal inv = 1.0 / motion;
            const QPointF trailStart(sx - dx * inv * trailLength,
                                     sy - dy * inv * trailLength);
            p.setPen(QPen(QColor(255, 248, 231,
                                 qMin(215, static_cast<int>((0.18 + depth * 0.58) * 255.0))),
                          0.48 + depth * 1.05, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(trailStart, QPointF(sx, sy));
        }

        const QColor starColor(255, s.warmth > 0.72f ? 242 : 248,
                                s.warmth > 0.72f ? 214 : 231, alpha);
        p.setPen(Qt::NoPen);
        p.setBrush(starColor);
        p.drawEllipse(QPointF(sx, sy), radius, radius);

        if (depth > 0.78) {
            QRadialGradient halo(QPointF(sx, sy), radius * 3.1);
            halo.setColorAt(0.0, QColor(255, 248, 231, 48));
            halo.setColorAt(0.40, QColor(255, 239, 205, 16));
            halo.setColorAt(1.0, QColor(255, 248, 231, 0));
            p.setBrush(halo);
            p.drawEllipse(QPointF(sx, sy), radius * 3.1, radius * 3.1);
        }
    }
    p.restore();
}

void KadiaScene::drawVignette(QPainter &p)
{
    p.save();
    QRadialGradient radial(QPointF(canvasWidth() * 0.5, canvasHeight() * 0.5),
                            qMax(canvasWidth(), canvasHeight()) * 0.62);
    radial.setColorAt(0.0, QColor(0, 0, 0, 0));
    radial.setColorAt(0.72, QColor(0, 0, 0, 4));
    radial.setColorAt(1.0, QColor(0, 0, 0, 36));
    p.fillRect(frameRect(), radial);

    QLinearGradient lr(frameRect().left(), 0.0, frameRect().right(), 0.0);
    lr.setColorAt(0.0, QColor(0, 0, 0, 32));
    lr.setColorAt(0.10, QColor(0, 0, 0, 0));
    lr.setColorAt(0.86, QColor(0, 0, 0, 0));
    lr.setColorAt(1.0, QColor(0, 0, 0, 25));
    p.fillRect(frameRect(), lr);
    p.restore();
}

void KadiaScene::drawTopBar(QPainter &p)
{
    drawLogo(p);

    p.save();
    p.setFont(fontForPixelSize(12, QFont::Normal));
    p.setPen(QColor(255, 248, 231, 185));
    const QString timeText = QDateTime::currentDateTime().toString(QStringLiteral("h:mm AP"));
    if (KadiaSettings::showClock())
        p.drawText(QRectF(canvasWidth() - kShellRight - 150.0, kShellTop + 8.0, 150.0, 28.0),
                   Qt::AlignRight | Qt::AlignVCenter, timeText);
    const QRectF sr = settingsButtonRect();
    p.setPen(QColor(255, 248, 231, 150));
    p.setFont(fontForPixelSize(11, QFont::Normal));
    p.drawText(sr, Qt::AlignCenter, QStringLiteral("Settings"));
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
                                                       m_tileChangeAge, kStripX, stripY, stripWidth());
    p.save();
    p.setClipRect(QRectF(kStripX, stripY - 18.0, stripWidth(), 178.0));
    for (int i = 0; i < geoms.size(); ++i) {
        if (geoms[i].rect.right() < kStripX - 8.0 || geoms[i].rect.left() > kStripX + stripWidth() + 8.0)
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
    p.drawText(QRectF(kStripX, stripY + 164.0, stripWidth(), 24.0),
               Qt::AlignLeft | Qt::AlignVCenter, section.caption);
    p.restore();

    if (detailsPanelVisible()) {
        const KadiaTileInfo &info = section.tiles[qBound(0, m_tile, section.tiles.size() - 1)];
        const QRectF panel(panelX(), kHubTop + 308.0, panelWidth(), 176.0 + 40.0);
        drawDescriptionPanel(p, panel, info.title, section.name, info.description, false);
    }
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
    const QRectF libraryTitleRect(kHubLeft, railTop + 34.0,
                                  qMin<qreal>(500.0, hubWidth()), 51.0);
    drawMarqueeLine(p, libraryTitleRect, m_libraryTitle, latte(255), true, QPointF(0.0, 2.0));

    p.setFont(fontForPixelSize(18, QFont::Light));
    p.setPen(QColor(255, 248, 231, 118));
    p.drawText(QRectF(kHubLeft, railTop + 90.0, 520.0, 34.0), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("%1 view  |  Sort: %2").arg(m_gallery ? QStringLiteral("Gallery") : QStringLiteral("Carousel"), kadiaGameSortLabel()));
    p.drawText(QRectF(kHubLeft, railTop + 124.0, 360.0, 34.0), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Favorites"));
    p.drawText(QRectF(kHubLeft, railTop + 158.0, 360.0, 34.0), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Recently Played"));
    p.restore();

    const QVector<KadiaGameInfo> &games = kadiaGames();
    const qreal y = kHubTop + 310.0;
    const qreal gap = 15.0;
    const qreal viewportW = stripWidth();

    if (games.isEmpty()) {
        p.save();
        p.setFont(fontForPixelSize(28, QFont::Light));
        p.setPen(QColor(255, 248, 231, 218));
        p.drawText(QRectF(kStripX, y + 18.0, viewportW, 42.0),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("No games detected"));
        p.setFont(fontForPixelSize(13, QFont::Normal));
        p.setPen(QColor(255, 248, 231, 118));
        p.drawText(QRectF(kStripX, y + 62.0, viewportW, 52.0),
                   Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   QStringLiteral("Kadia did not detect any recognized ROMs for this library."));
        p.restore();
        return;
    }

    if (m_game < 0 || m_game >= games.size()) {
        m_game = 0;
        m_previousGame = 0;
    }

    if (m_galleryBlend > 0.001)
        drawGallery(p, games, m_galleryBlend);
    if (m_galleryBlend >= 0.995)
        return;

    p.save();
    p.setOpacity(1.0 - m_galleryBlend);
    const qreal t = easeOutCubic(qMin<qreal>(1.0, m_gameChangeAge / 0.18));

    const qreal selectedSel = carouselSelection(m_game, m_game, m_previousGame, t);
    const qreal selectedCenter = carouselRawLeft(m_game, m_game, m_previousGame, t, gap) +
                                 carouselCardWidth(selectedSel) * 0.5;
    const qreal totalW = carouselTotalWidth(games.size(), gap);
    const qreal preferred = qMin(viewportW * 0.56, 530.0);
    const qreal scroll = qBound<qreal>(0.0, selectedCenter - preferred, qMax<qreal>(0.0, totalW - viewportW));

    const int first = qMax(0, static_cast<int>(scroll / (kCarouselBaseWidth + gap)) - 2);
    const int last = qMin(games.size() - 1,
                          static_cast<int>((scroll + viewportW) / (kCarouselBaseWidth + gap)) + 3);
    p.save();
    p.setClipRect(QRectF(kStripX, y - 14.0, viewportW, 230.0));
    for (int i = first; i <= last; ++i) {
        const qreal sel = carouselSelection(i, m_game, m_previousGame, t);
        const qreal width = carouselCardWidth(sel);
        const qreal x = kStripX - scroll + carouselRawLeft(i, m_game, m_previousGame, t, gap);
        const qreal h = 160.0 + (210.0 - 160.0) * sel;
        const qreal lift = -10.0 * sel;
        const QRectF card(x, y + lift, width, h);
        drawGameCard(p, card, sel, i);
    }
    p.restore();

    if (!games.isEmpty() && detailsPanelVisible()) {
        const KadiaGameInfo &game = games[qBound(0, m_game, games.size() - 1)];
        const QRectF panel(panelX(), kHubTop + 302.0, panelWidth(), 204.0);
        QString detail = game.description;
        QStringList stats;
        if (!game.releaseYear.isEmpty()) stats << QStringLiteral("Released %1").arg(game.releaseYear);
        stats << GameStats::humanPlayTime(game.playSeconds);
        if (game.dateAdded.isValid()) stats << QStringLiteral("Added %1").arg(game.dateAdded.date().toString(Qt::ISODate));
        if (!stats.isEmpty()) detail = stats.join(QStringLiteral("  |  ")) + QStringLiteral("\n") + detail;
        drawDescriptionPanel(p, panel, game.title, game.subtitle, detail, true);
    }
    p.restore();
}

int KadiaScene::galleryColumns() const
{
    const qreal available = qMax<qreal>(48.0, stripWidth());
    return qBound(1, static_cast<int>(available / 135.0), 7);
}

QRectF KadiaScene::galleryCardRect(int index, int count) const
{
    if (index < 0 || index >= count) return QRectF();
    const int columns = galleryColumns();
    const qreal gap = 13.0;
    const qreal areaW = stripWidth();
    const qreal cardW = qMin<qreal>(150.0, (areaW - gap * (columns - 1)) / columns);
    const qreal cardH = 176.0;
    const int row = index / columns;
    const int col = index % columns;
    const int selectedRow = qMax(0, m_game / columns);
    const int visibleRows = 2;
    const int firstRow = qMax(0, selectedRow - (visibleRows - 1));
    const qreal top = kHubTop + 292.0;
    const qreal x = kStripX + col * (cardW + gap);
    const qreal y = top + (row - firstRow) * (cardH + gap);
    if (row < firstRow || row >= firstRow + visibleRows) return QRectF();
    return QRectF(x, y, cardW, cardH);
}

void KadiaScene::drawGallery(QPainter &p, const QVector<KadiaGameInfo> &games, qreal opacity)
{
    if (games.isEmpty() || opacity <= 0.0) return;
    p.save();
    p.setOpacity(qBound<qreal>(0.0, opacity, 1.0));
    p.setClipRect(QRectF(kStripX, kHubTop + 276.0, stripWidth(), 390.0));
    const qreal transition = easeOutCubic(qMin<qreal>(1.0, m_gameChangeAge / 0.16));
    const int columns = galleryColumns();
    const int selectedRow = qMax(0, m_game / columns);
    const int firstRow = qMax(0, selectedRow - 1);
    const int first = firstRow * columns;
    const int last = qMin(games.size() - 1, (firstRow + 2) * columns - 1);
    for (int i = first; i <= last; ++i) {
        QRectF card = galleryCardRect(i, games.size());
        if (!card.isValid()) continue;
        qreal sel = i == m_game ? transition : (i == m_previousGame ? 1.0 - transition : 0.0);
        if (m_game == m_previousGame) sel = i == m_game ? 1.0 : 0.0;
        const qreal zoom = 1.0 + 0.055 * sel;
        const QPointF c = card.center();
        card = QRectF(c.x() - card.width() * zoom * 0.5,
                      c.y() - card.height() * zoom * 0.5 - 5.0 * sel,
                      card.width() * zoom, card.height() * zoom);
        drawGameCard(p, card, static_cast<float>(sel), i);
    }
    p.restore();

    if (m_game >= 0 && m_game < games.size() && detailsPanelVisible()) {
        const KadiaGameInfo &game = games.at(m_game);
        const QRectF panel(panelX(), kHubTop + 292.0, panelWidth(), 218.0);
        QStringList stats;
        if (!game.releaseYear.isEmpty()) stats << QStringLiteral("Released %1").arg(game.releaseYear);
        stats << GameStats::humanPlayTime(game.playSeconds);
        if (game.lastPlayed.isValid()) stats << QStringLiteral("Last played %1").arg(game.lastPlayed.date().toString(Qt::ISODate));
        QString detail = stats.join(QStringLiteral("  |  "));
        if (!detail.isEmpty()) detail += QStringLiteral("\n");
        detail += game.description;
        p.save(); p.setOpacity(opacity);
        drawDescriptionPanel(p, panel, game.title, game.subtitle, detail, true);
        p.restore();
    }
}

void KadiaScene::drawFooter(QPainter &p)
{
    const qreal cy = footerCenterY();
    drawControllerHints(p, kShellLeft, cy);
    drawMediaControls(p, canvasWidth() - kShellRight, cy);
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
        const QRectF r(kHubLeft, top + yPositions[pos], qMin<qreal>(430.0, hubWidth()), height);
        const QColor color = QColor(255, 248, 231, alpha);
        drawMarqueeLine(p, r, sections[index].name, color, offset == 0, QPointF(0.0, 2.0));
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

    drawTileIcon(p, QRectF(rect.left(), rect.top() + 4.0, rect.width(), rect.height() - 30.0),
                 icon, label, selection);

    const qreal labelH = 31.0 + 7.0 * selection;
    QLinearGradient labelBg(rect.left(), rect.bottom() - labelH, rect.left(), rect.bottom());
    labelBg.setColorAt(0.0, QColor(5, 8, 14, 35));
    labelBg.setColorAt(1.0, QColor(5, 8, 14, 245));
    p.fillRect(QRectF(rect.left() + 1.0, rect.bottom() - labelH,
                      rect.width() - 2.0, labelH - 1.0), labelBg);

    p.setFont(fontForPixelSize(static_cast<int>(12.0 + 2.0 * selection), QFont::Normal));
    drawMarqueeLine(p, QRectF(rect.left() + 10.0 + 2.0 * selection,
                              rect.bottom() - labelH,
                              rect.width() - 20.0,
                              labelH),
                     label, QColor(255, 248, 231, 235), selection > 0.50f);

    if (selection > 0.02f) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 248, 231, static_cast<int>(45 + 100 * selection)), 1.0));
        p.drawRoundedRect(adjustedRect(rect, -1.0, -1.0, 1.0, 1.0), 3.0, 3.0);
    }
    p.restore();
}


void KadiaScene::drawTileIcon(QPainter &p, const QRectF &rect, const QString &icon,
                              const QString &label, float selection)
{
    Q_UNUSED(icon);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor c(255, 248, 231, 220);
    const QColor accent(215, 229, 255, static_cast<int>(132 + 45 * selection));
    const QColor warm(255, 232, 188, static_cast<int>(118 + 55 * selection));
    const qreal pi = 3.14159265358979323846;
    const QPointF center = rect.center();
    const qreal scale = 1.0 + 0.08 * selection;
    const qreal base = qMin(rect.width(), rect.height()) * 0.19 * scale;

    QString sectionKey;
    const QVector<KadiaSectionInfo> &sections = kadiaSections();
    if (m_category >= 0 && m_category < sections.size())
        sectionKey = sections[m_category].name.toLower();
    const QString key = (sectionKey + QStringLiteral(" ") + label + QStringLiteral(" ") + icon).toLower();
    const uint signature = qHash(sectionKey + QStringLiteral("|") + label + QStringLiteral("|") + icon);

    const QString brandResource = consoleBrandResource(sectionKey, label);
    if (!brandResource.isEmpty()) {
        const QImage *brand = rawArgbIcon(brandResource);
        if (brand && !brand->isNull()) {
            // Real platform packs contain both compact emblems and wide wordmarks.
            // Fit the native aspect ratio instead of forcing every asset into a
            // square (which made authentic NES/N64/etc. marks look tiny or warped).
            const qreal maxW = rect.width() * 0.68 * (1.0 + 0.08 * selection);
            const qreal maxH = rect.height() * 0.64 * (1.0 + 0.08 * selection);
            const qreal imageW = qMax(1, brand->width());
            const qreal imageH = qMax(1, brand->height());
            const qreal fit = qMin(maxW / imageW, maxH / imageH);
            const qreal drawW = imageW * fit;
            const qreal drawH = imageH * fit;
            QRectF target(center.x() - drawW * 0.5, center.y() - drawH * 0.5, drawW, drawH);
            p.save();
            p.setOpacity(0.82 + 0.18 * selection);
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.drawImage(target, *brand, QRectF(brand->rect()));
            p.restore();
            p.restore();
            return;
        }
    }

    // Concrete platforms above never fall through to a generic hardware badge.
    // Non-platform action tiles continue through the semantic icon renderer below.

    auto has = [&](const char *needle) -> bool {
        return key.contains(QString::fromLatin1(needle));
    };
    auto linePen = [&](qreal width, QColor color = QColor(255, 248, 231, 220)) {
        return QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    };
    auto fillStroke = [&](const QPainterPath &path, bool filled, qreal width = 1.9) {
        if (filled) {
            p.setPen(Qt::NoPen);
            p.setBrush(c);
        } else {
            p.setPen(linePen(width));
            p.setBrush(Qt::NoBrush);
        }
        p.drawPath(path);
    };

    auto drawDiamond = [&](bool filled) {
        QPainterPath path;
        path.moveTo(center.x(), center.y() - base);
        path.lineTo(center.x() + base, center.y());
        path.lineTo(center.x(), center.y() + base);
        path.lineTo(center.x() - base, center.y());
        path.closeSubpath();
        fillStroke(path, filled, 1.8);
    };

    auto drawEightPointStar = [&](bool filled) {
        QPainterPath path;
        for (int i = 0; i < 8; ++i) {
            const qreal a = -pi * 0.5 + i * pi / 4.0;
            const qreal r = (i % 2 == 0) ? base : base * 0.44;
            const QPointF pt(center.x() + qCos(a) * r, center.y() + qSin(a) * r);
            if (i == 0) path.moveTo(pt); else path.lineTo(pt);
        }
        path.closeSubpath();
        fillStroke(path, filled, 1.8);
    };

    auto drawGrid = [&](int cols, int rows) {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        const qreal cell = base * 0.42;
        const qreal gap = base * 0.19;
        const qreal totalW = cols * cell + (cols - 1) * gap;
        const qreal totalH = rows * cell + (rows - 1) * gap;
        const qreal x0 = center.x() - totalW * 0.5;
        const qreal y0 = center.y() - totalH * 0.5;
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x < cols; ++x)
                p.drawRoundedRect(QRectF(x0 + x * (cell + gap), y0 + y * (cell + gap), cell, cell), 1.15, 1.15);
    };

    auto drawSearch = [&]() {
        p.setBrush(Qt::NoBrush);
        p.setPen(linePen(2.35));
        const qreal r = base * 0.68;
        p.drawEllipse(QPointF(center.x() - base * 0.18, center.y() - base * 0.12), r, r);
        p.drawLine(QPointF(center.x() + base * 0.28, center.y() + base * 0.34),
                   QPointF(center.x() + base * 0.94, center.y() + base * 1.00));
    };

    auto drawPlay = [&]() {
        QPainterPath path;
        path.moveTo(center.x() - base * 0.58, center.y() - base);
        path.lineTo(center.x() + base, center.y());
        path.lineTo(center.x() - base * 0.58, center.y() + base);
        path.closeSubpath();
        p.fillPath(path, c);
    };

    auto drawScreen = [&](bool withStand) {
        p.setBrush(Qt::NoBrush);
        p.setPen(linePen(1.95));
        QRectF screen(center.x() - base, center.y() - base * 0.68, base * 2.0, base * 1.36);
        p.drawRoundedRect(screen, 2.0, 2.0);
        if (withStand) {
            p.drawLine(QPointF(center.x() - base * 0.30, screen.bottom() + base * 0.30),
                       QPointF(center.x() + base * 0.30, screen.bottom() + base * 0.30));
            p.drawLine(QPointF(center.x(), screen.bottom()), QPointF(center.x(), screen.bottom() + base * 0.30));
        }
    };

    auto drawMusic = [&]() {
        p.setPen(linePen(2.1));
        p.setBrush(c);
        const qreal stemX = center.x() + base * 0.38;
        p.drawLine(QPointF(stemX, center.y() - base * 0.90), QPointF(stemX, center.y() + base * 0.45));
        p.drawLine(QPointF(stemX, center.y() - base * 0.90), QPointF(center.x() - base * 0.38, center.y() - base * 0.68));
        p.drawEllipse(QPointF(center.x() - base * 0.48, center.y() + base * 0.55), base * 0.34, base * 0.26);
        p.drawEllipse(QPointF(stemX - base * 0.08, center.y() + base * 0.48), base * 0.34, base * 0.26);
    };

    auto drawList = [&](int count) {
        p.setPen(linePen(1.85));
        for (int i = 0; i < count; ++i) {
            const qreal y = center.y() + (i - (count - 1) * 0.5) * base * 0.50;
            p.drawEllipse(QPointF(center.x() - base * 0.82, y), 1.4, 1.4);
            p.drawLine(QPointF(center.x() - base * 0.48, y), QPointF(center.x() + base * 0.88, y));
        }
    };

    auto drawController = [&]() {
        QPainterPath body;
        body.moveTo(center.x() - base * 0.95, center.y() + base * 0.62);
        body.cubicTo(center.x() - base * 1.12, center.y() - base * 0.10,
                     center.x() - base * 0.72, center.y() - base * 0.62,
                     center.x() - base * 0.28, center.y() - base * 0.50);
        body.lineTo(center.x() + base * 0.28, center.y() - base * 0.50);
        body.cubicTo(center.x() + base * 0.72, center.y() - base * 0.62,
                     center.x() + base * 1.12, center.y() - base * 0.10,
                     center.x() + base * 0.95, center.y() + base * 0.62);
        body.cubicTo(center.x() + base * 0.78, center.y() + base * 0.92,
                     center.x() + base * 0.50, center.y() + base * 0.64,
                     center.x() + base * 0.27, center.y() + base * 0.30);
        body.lineTo(center.x() - base * 0.27, center.y() + base * 0.30);
        body.cubicTo(center.x() - base * 0.50, center.y() + base * 0.64,
                     center.x() - base * 0.78, center.y() + base * 0.92,
                     center.x() - base * 0.95, center.y() + base * 0.62);
        body.closeSubpath();
        p.setPen(linePen(1.8));
        p.setBrush(Qt::NoBrush);
        p.drawPath(body);
        p.drawLine(QPointF(center.x() - base * 0.55, center.y() - base * 0.10),
                   QPointF(center.x() - base * 0.17, center.y() - base * 0.10));
        p.drawLine(QPointF(center.x() - base * 0.36, center.y() - base * 0.29),
                   QPointF(center.x() - base * 0.36, center.y() + base * 0.09));
        p.setBrush(c); p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(center.x() + base * 0.45, center.y() - base * 0.18), base * 0.11, base * 0.11);
        p.drawEllipse(QPointF(center.x() + base * 0.68, center.y() + base * 0.02), base * 0.11, base * 0.11);
    };

    auto drawGear = [&]() {
        p.setPen(linePen(1.95));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(center, base * 0.50, base * 0.50);
        p.drawEllipse(center, base * 0.18, base * 0.18);
        for (int i = 0; i < 8; ++i) {
            const qreal a = i * pi / 4.0;
            p.drawLine(QPointF(center.x() + qCos(a) * base * 0.62, center.y() + qSin(a) * base * 0.62),
                       QPointF(center.x() + qCos(a) * base * 0.93, center.y() + qSin(a) * base * 0.93));
        }
    };

    auto drawPower = [&]() {
        p.setPen(linePen(2.25));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(center.x() - base, center.y() - base, base * 2, base * 2), 40 * 16, 280 * 16);
        p.drawLine(QPointF(center.x(), center.y() - base * 1.05), QPointF(center.x(), center.y() - base * 0.15));
    };

    auto drawRefresh = [&]() {
        p.setPen(linePen(1.95));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(center.x() - base, center.y() - base, base * 2, base * 2), 25 * 16, 290 * 16);
        QPainterPath arrow;
        arrow.moveTo(center.x() + base * 0.92, center.y() - base * 0.32);
        arrow.lineTo(center.x() + base * 0.92, center.y() + base * 0.20);
        arrow.lineTo(center.x() + base * 0.45, center.y() - base * 0.02);
        arrow.closeSubpath();
        p.fillPath(arrow, c);
    };

    auto drawHandheld = [&]() {
        p.setPen(linePen(1.95));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(center.x() - base * 1.05, center.y() - base * 0.62,
                                 base * 2.10, base * 1.24), base * 0.18, base * 0.18);
        p.drawRect(QRectF(center.x() - base * 0.38, center.y() - base * 0.28,
                          base * 0.76, base * 0.56));
        p.setBrush(c); p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(center.x() + base * 0.62, center.y()), base * 0.10, base * 0.10);
    };

    auto drawFolder = [&]() {
        p.setBrush(Qt::NoBrush);
        p.setPen(linePen(1.95));
        QPainterPath path;
        path.moveTo(center.x() - base * 1.00, center.y() - base * 0.42);
        path.lineTo(center.x() - base * 0.34, center.y() - base * 0.42);
        path.lineTo(center.x() - base * 0.08, center.y() - base * 0.72);
        path.lineTo(center.x() + base * 1.00, center.y() - base * 0.72);
        path.lineTo(center.x() + base * 1.00, center.y() + base * 0.72);
        path.lineTo(center.x() - base * 1.00, center.y() + base * 0.72);
        path.closeSubpath();
        p.drawPath(path);
    };

    auto drawPicture = [&]() {
        p.setBrush(Qt::NoBrush);
        p.setPen(linePen(1.9));
        QRectF r(center.x() - base, center.y() - base * 0.72, base * 2.0, base * 1.44);
        p.drawRoundedRect(r, 2.0, 2.0);
        p.drawEllipse(QPointF(r.right() - base * 0.38, r.top() + base * 0.30), base * 0.12, base * 0.12);
        QPainterPath hill;
        hill.moveTo(r.left() + base * 0.22, r.bottom() - base * 0.20);
        hill.lineTo(r.left() + base * 0.70, r.top() + base * 0.15);
        hill.lineTo(r.left() + base * 1.15, r.bottom() - base * 0.18);
        hill.lineTo(r.left() + base * 1.55, r.top() + base * 0.34);
        hill.lineTo(r.right() - base * 0.18, r.bottom() - base * 0.18);
        p.drawPath(hill);
    };

    auto drawClock = [&]() {
        p.setBrush(Qt::NoBrush);
        p.setPen(linePen(1.95));
        p.drawEllipse(center, base * 0.92, base * 0.92);
        p.drawLine(center, QPointF(center.x(), center.y() - base * 0.44));
        p.drawLine(center, QPointF(center.x() + base * 0.36, center.y() + base * 0.14));
    };

    auto drawDownload = [&]() {
        p.setPen(linePen(1.95));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(center.x(), center.y() - base * 0.86), QPointF(center.x(), center.y() + base * 0.36));
        p.drawLine(QPointF(center.x() - base * 0.36, center.y() + base * 0.02), QPointF(center.x(), center.y() + base * 0.38));
        p.drawLine(QPointF(center.x() + base * 0.36, center.y() + base * 0.02), QPointF(center.x(), center.y() + base * 0.38));
        p.drawLine(QPointF(center.x() - base * 0.86, center.y() + base * 0.70), QPointF(center.x() + base * 0.86, center.y() + base * 0.70));
    };

    auto drawBars = [&]() {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        const qreal w = base * 0.36;
        for (int i = 0; i < 4; ++i) {
            const qreal h = base * (0.46 + i * 0.16);
            const qreal x = center.x() - base * 0.88 + i * (w + base * 0.15);
            p.drawRoundedRect(QRectF(x, center.y() + base * 0.78 - h, w, h), 1.0, 1.0);
        }
    };

    auto drawBook = [&]() {
        p.setBrush(Qt::NoBrush);
        p.setPen(linePen(1.9));
        QRectF left(center.x() - base * 1.02, center.y() - base * 0.72, base * 0.94, base * 1.44);
        QRectF right(center.x() + base * 0.08, center.y() - base * 0.72, base * 0.94, base * 1.44);
        p.drawRoundedRect(left, 1.5, 1.5);
        p.drawRoundedRect(right, 1.5, 1.5);
        p.drawLine(QPointF(center.x(), left.top()), QPointF(center.x(), left.bottom()));
    };

    auto drawGlobe = [&]() {
        p.setBrush(Qt::NoBrush);
        p.setPen(linePen(1.85));
        p.drawEllipse(center, base * 0.95, base * 0.95);
        p.drawEllipse(center, base * 0.42, base * 0.95);
        p.drawLine(QPointF(center.x() - base * 0.95, center.y()), QPointF(center.x() + base * 0.95, center.y()));
        p.drawArc(QRectF(center.x() - base * 0.95, center.y() - base * 0.42, base * 1.9, base * 0.84), 0, 180 * 16);
        p.drawArc(QRectF(center.x() - base * 0.95, center.y() - base * 0.42, base * 1.9, base * 0.84), 180 * 16, 180 * 16);
    };

    auto drawFilm = [&]() {
        p.setBrush(Qt::NoBrush);
        p.setPen(linePen(1.9));
        QRectF r(center.x() - base * 1.00, center.y() - base * 0.68, base * 2.0, base * 1.36);
        p.drawRoundedRect(r, 1.8, 1.8);
        for (int i = 0; i < 4; ++i) {
            p.drawRect(QRectF(r.left() - 0.2, r.top() + base * 0.10 + i * base * 0.34, base * 0.22, base * 0.16));
            p.drawRect(QRectF(r.right() - base * 0.22 + 0.2, r.top() + base * 0.10 + i * base * 0.34, base * 0.22, base * 0.16));
        }
    };

    auto drawDisc = [&]() {
        p.setBrush(Qt::NoBrush);
        p.setPen(linePen(1.9));
        p.drawEllipse(center, base * 0.95, base * 0.95);
        p.drawEllipse(center, base * 0.24, base * 0.24);
        p.drawLine(QPointF(center.x() + base * 0.46, center.y() - base * 0.28),
                   QPointF(center.x() + base * 0.72, center.y() - base * 0.52));
    };

    auto drawWand = [&]() {
        p.setPen(linePen(1.95));
        p.drawLine(QPointF(center.x() - base * 0.72, center.y() + base * 0.72),
                   QPointF(center.x() + base * 0.62, center.y() - base * 0.62));
        p.setBrush(c); p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(center.x() + base * 0.74, center.y() - base * 0.74), base * 0.14, base * 0.14);
        p.drawEllipse(QPointF(center.x() + base * 0.52, center.y() - base * 0.96), base * 0.08, base * 0.08);
        p.drawEllipse(QPointF(center.x() + base * 0.96, center.y() - base * 0.52), base * 0.08, base * 0.08);
    };

    auto drawArrows = [&]() {
        p.setPen(linePen(1.9));
        p.drawLine(QPointF(center.x() - base * 0.88, center.y()), QPointF(center.x() + base * 0.88, center.y()));
        p.drawLine(QPointF(center.x() - base * 0.88, center.y()), QPointF(center.x() - base * 0.46, center.y() - base * 0.42));
        p.drawLine(QPointF(center.x() - base * 0.88, center.y()), QPointF(center.x() - base * 0.46, center.y() + base * 0.42));
        p.drawLine(QPointF(center.x() + base * 0.88, center.y()), QPointF(center.x() + base * 0.46, center.y() - base * 0.42));
        p.drawLine(QPointF(center.x() + base * 0.88, center.y()), QPointF(center.x() + base * 0.46, center.y() + base * 0.42));
    };

    auto drawSliders = [&]() {
        p.setPen(linePen(1.85));
        const qreal y1 = center.y() - base * 0.46;
        const qreal y2 = center.y();
        const qreal y3 = center.y() + base * 0.46;
        p.drawLine(QPointF(center.x() - base, y1), QPointF(center.x() + base, y1));
        p.drawLine(QPointF(center.x() - base, y2), QPointF(center.x() + base, y2));
        p.drawLine(QPointF(center.x() - base, y3), QPointF(center.x() + base, y3));
        p.setBrush(c); p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(center.x() - base * 0.32, y1), base * 0.14, base * 0.14);
        p.drawEllipse(QPointF(center.x() + base * 0.28, y2), base * 0.14, base * 0.14);
        p.drawEllipse(QPointF(center.x() - base * 0.05, y3), base * 0.14, base * 0.14);
    };

    auto drawSpeaker = [&]() {
        p.setPen(Qt::NoPen); p.setBrush(c);
        QPainterPath sp;
        sp.moveTo(center.x() - base * 0.95, center.y() - base * 0.36);
        sp.lineTo(center.x() - base * 0.48, center.y() - base * 0.36);
        sp.lineTo(center.x() - base * 0.08, center.y() - base * 0.74);
        sp.lineTo(center.x() - base * 0.08, center.y() + base * 0.74);
        sp.lineTo(center.x() - base * 0.48, center.y() + base * 0.36);
        sp.lineTo(center.x() - base * 0.95, center.y() + base * 0.36);
        sp.closeSubpath();
        p.fillPath(sp, c);
        p.setPen(linePen(1.6)); p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(center.x() - base * 0.10, center.y() - base * 0.52, base * 1.10, base * 1.04), -45 * 16, 90 * 16);
        p.drawArc(QRectF(center.x() + base * 0.06, center.y() - base * 0.76, base * 1.34, base * 1.52), -45 * 16, 90 * 16);
    };

    auto drawRocket = [&]() {
        p.setPen(linePen(1.85));
        p.setBrush(Qt::NoBrush);
        QPainterPath rocket;
        rocket.moveTo(center.x(), center.y() - base * 1.04);
        rocket.cubicTo(center.x() + base * 0.76, center.y() - base * 0.62,
                       center.x() + base * 0.64, center.y() + base * 0.32,
                       center.x(), center.y() + base * 0.95);
        rocket.cubicTo(center.x() - base * 0.64, center.y() + base * 0.32,
                       center.x() - base * 0.76, center.y() - base * 0.62,
                       center.x(), center.y() - base * 1.04);
        rocket.closeSubpath();
        p.drawPath(rocket);
        p.drawEllipse(QPointF(center.x(), center.y() - base * 0.20), base * 0.18, base * 0.18);
        p.drawLine(QPointF(center.x() - base * 0.20, center.y() + base * 0.52), QPointF(center.x() - base * 0.56, center.y() + base * 0.82));
        p.drawLine(QPointF(center.x() + base * 0.20, center.y() + base * 0.52), QPointF(center.x() + base * 0.56, center.y() + base * 0.82));
    };

    enum IconFamily {
        FamilyGrid, FamilySearch, FamilyPlay, FamilyScreen, FamilyMusic,
        FamilyStar, FamilyController, FamilyGear, FamilyPower, FamilyRefresh,
        FamilyHandheld, FamilyFolder, FamilyPicture, FamilyClock, FamilyDownload,
        FamilyBars, FamilyBook, FamilyGlobe, FamilyFilm, FamilyDisc,
        FamilyWand, FamilyArrows, FamilySliders, FamilySpeaker, FamilyRocket,
        FamilyDiamond, FamilyRing
    };

    IconFamily family = FamilyDiamond;
    bool filledVariant = false;

    if (has("search")) family = FamilySearch;
    else if (has("music") || has("song") || has("radio") || has("album") || has("artist")) family = FamilyMusic;
    else if (has("favorite") || has("achievement") || has("rating")) family = FamilyStar;
    else if (has("controller") || has("player") || has("pad") || has("input")) family = FamilyController;
    else if (has("setting") || has("config") || has("driver") || has("setup") || has("tool")) family = FamilyGear;
    else if (has("power") || has("shut") || has("sleep") || has("quit") || has("exit")) family = FamilyPower;
    else if (has("update") || has("refresh") || has("automatic") || has("restart")) family = FamilyRefresh;
    else if (has("video") || has("movie") || has("tv") || has("dvd") || has("display")) family = FamilyScreen;
    else if (has("play") || has("continue") || has("resume") || has("now playing")) family = FamilyPlay;
    else if (has("picture") || has("screenshot") || has("fanart") || has("box art") || has("photo") || has("slide show")) family = FamilyPicture;
    else if (has("guide") || has("playlist") || has("list") || has("filter") || has("sort")) family = FamilyBars;
    else if (key == QStringLiteral("consoles nintendo") || key.contains(QStringLiteral(" nintendo"))) { family = FamilyDiamond; filledVariant = true; }
    else if (key.contains(QStringLiteral("super nintendo"))) { family = FamilyStar; filledVariant = true; }
    else if (key.contains(QStringLiteral("sega"))) family = FamilyRing;
    else if (key.contains(QStringLiteral("playstation")) || key.contains(QStringLiteral("epic"))) family = FamilyDiamond;
    else if (key.contains(QStringLiteral("xbox")) || key.contains(QStringLiteral("windows"))) family = FamilyGrid;
    else if (key.contains(QStringLiteral("atari"))) family = FamilyRocket;
    else if (key.contains(QStringLiteral("nec"))) family = FamilyDisc;
    else if (key.contains(QStringLiteral("game boy")) || key.contains(QStringLiteral("psp")) || key.contains(QStringLiteral("vita")) || has("handheld")) family = FamilyHandheld;
    else if (has("all games") || has("library") || has("system") || has("collection") || has("content")) family = FamilyGrid;
    else if (has("download") || has("install") || has("scrape")) family = FamilyDownload;
    else if (has("information") || has("manual") || has("language")) family = FamilyBook;
    else if (has("clock") || has("hour") || has("time") || has("recent") || has("last played")) family = FamilyClock;
    else if (has("volume") || has("mute") || has("audio") || has("sound")) family = FamilySpeaker;
    else if (has("theme") || has("interface") || has("screen saver") || has("screensaver")) family = FamilyWand;
    else if (has("sync") || has("netplay") || has("online") || has("internet")) family = FamilyGlobe;
    else if (has("burn") || has("disc") || has("cd ") || has("dvd")) family = FamilyDisc;
    else if (has("movie") || has("recorded") || has("recording") || has("trailer")) family = FamilyFilm;
    else if (has("latency") || has("optimization") || has("performance") || has("statistics") || has("score")) family = FamilyBars;
    else if (has("shader") || has("aspect") || has("transition") || has("mode") || has("display options")) family = FamilySliders;
    else if (has("folder") || has("library")) family = FamilyFolder;
    else if (has("random") || has("free games") || has("services") || has("extras")) family = FamilyRocket;
    else if (has("move") || has("jump") || has("browse") || has("extender")) family = FamilyArrows;

    switch (family) {
    case FamilyGrid:      drawGrid(3, 3); break;
    case FamilySearch:    drawSearch(); break;
    case FamilyPlay:      drawPlay(); break;
    case FamilyScreen:    drawScreen(true); break;
    case FamilyMusic:     drawMusic(); break;
    case FamilyStar:      drawEightPointStar(filledVariant); break;
    case FamilyController: drawController(); break;
    case FamilyGear:      drawGear(); break;
    case FamilyPower:     drawPower(); break;
    case FamilyRefresh:   drawRefresh(); break;
    case FamilyHandheld:  drawHandheld(); break;
    case FamilyFolder:    drawFolder(); break;
    case FamilyPicture:   drawPicture(); break;
    case FamilyClock:     drawClock(); break;
    case FamilyDownload:  drawDownload(); break;
    case FamilyBars:      drawBars(); break;
    case FamilyBook:      drawBook(); break;
    case FamilyGlobe:     drawGlobe(); break;
    case FamilyFilm:      drawFilm(); break;
    case FamilyDisc:      drawDisc(); break;
    case FamilyWand:      drawWand(); break;
    case FamilyArrows:    drawArrows(); break;
    case FamilySliders:   drawSliders(); break;
    case FamilySpeaker:   drawSpeaker(); break;
    case FamilyRocket:    drawRocket(); break;
    case FamilyDiamond:   drawDiamond(filledVariant); break;
    case FamilyRing:
        p.setBrush(Qt::NoBrush); p.setPen(linePen(2.1));
        p.drawEllipse(center, base * 0.92, base * 0.92); p.drawEllipse(center, base * 0.48, base * 0.48);
        break;
    }

    // Unique per-option identity marks. These sit around the main glyph and are
    // derived from section + label, so repeated menu items such as "Search" and
    // "Favorites" no longer share identical icons.
    const int cornerShape = signature % 7;
    const int orbitShape = (signature / 7u) % 6u;
    const int footerPattern = (signature / 43u) % 8u;
    const int sideMark = (signature / 349u) % 5u;

    const QPointF corner(center.x() + base * 1.02, center.y() - base * 0.96);
    p.setBrush(Qt::NoBrush);
    p.setPen(linePen(1.25, accent));
    switch (cornerShape) {
    case 0: p.drawEllipse(corner, base * 0.15, base * 0.15); break;
    case 1: p.drawRect(QRectF(corner.x() - base * 0.14, corner.y() - base * 0.14, base * 0.28, base * 0.28)); break;
    case 2: {
        QPainterPath d; d.moveTo(corner.x(), corner.y() - base * 0.17); d.lineTo(corner.x() + base * 0.17, corner.y()); d.lineTo(corner.x(), corner.y() + base * 0.17); d.lineTo(corner.x() - base * 0.17, corner.y()); d.closeSubpath(); p.drawPath(d); break; }
    case 3: p.drawLine(QPointF(corner.x() - base * 0.18, corner.y()), QPointF(corner.x() + base * 0.18, corner.y())); p.drawLine(QPointF(corner.x(), corner.y() - base * 0.18), QPointF(corner.x(), corner.y() + base * 0.18)); break;
    case 4: p.drawArc(QRectF(corner.x() - base * 0.20, corner.y() - base * 0.20, base * 0.40, base * 0.40), 20 * 16, 260 * 16); break;
    case 5: p.drawLine(QPointF(corner.x() - base * 0.16, corner.y() - base * 0.16), QPointF(corner.x() + base * 0.16, corner.y() + base * 0.16)); p.drawLine(QPointF(corner.x() - base * 0.16, corner.y() + base * 0.16), QPointF(corner.x() + base * 0.16, corner.y() - base * 0.16)); break;
    default: p.setPen(Qt::NoPen); p.setBrush(accent); p.drawEllipse(corner, base * 0.10, base * 0.10); break;
    }

    p.setPen(linePen(1.15, warm));
    switch (orbitShape) {
    case 0: p.drawArc(QRectF(center.x() - base * 1.22, center.y() - base * 1.00, base * 0.62, base * 0.62), 240 * 16, 110 * 16); break;
    case 1: p.drawLine(QPointF(center.x() - base * 1.18, center.y() + base * 0.90), QPointF(center.x() - base * 0.66, center.y() + base * 0.56)); break;
    case 2: p.drawArc(QRectF(center.x() + base * 0.58, center.y() + base * 0.56, base * 0.54, base * 0.54), 60 * 16, 160 * 16); break;
    case 3: p.drawLine(QPointF(center.x() + base * 0.76, center.y() - base * 1.10), QPointF(center.x() + base * 1.08, center.y() - base * 0.78)); break;
    case 4: p.drawEllipse(QPointF(center.x() - base * 1.00, center.y() - base * 0.86), base * 0.08, base * 0.08); break;
    default: p.drawEllipse(QPointF(center.x() + base * 0.96, center.y() + base * 0.98), base * 0.08, base * 0.08); break;
    }

    const qreal footY = center.y() + base * 1.18;
    p.setPen(linePen(1.10, QColor(255, 248, 231, 110)));
    switch (footerPattern) {
    case 0: p.drawLine(QPointF(center.x() - base * 0.56, footY), QPointF(center.x() + base * 0.56, footY)); break;
    case 1: p.drawLine(QPointF(center.x() - base * 0.56, footY), QPointF(center.x() - base * 0.10, footY)); p.drawLine(QPointF(center.x() + base * 0.10, footY), QPointF(center.x() + base * 0.56, footY)); break;
    case 2: p.drawEllipse(QPointF(center.x() - base * 0.28, footY), base * 0.08, base * 0.08); p.drawEllipse(QPointF(center.x() + base * 0.28, footY), base * 0.08, base * 0.08); break;
    case 3: p.drawLine(QPointF(center.x() - base * 0.34, footY), QPointF(center.x(), footY)); p.drawLine(QPointF(center.x(), footY), QPointF(center.x(), footY + base * 0.14)); p.drawLine(QPointF(center.x(), footY), QPointF(center.x() + base * 0.34, footY)); break;
    case 4: p.drawArc(QRectF(center.x() - base * 0.42, footY - base * 0.16, base * 0.84, base * 0.32), 200 * 16, 140 * 16); break;
    case 5: p.drawLine(QPointF(center.x() - base * 0.48, footY - base * 0.08), QPointF(center.x() - base * 0.12, footY + base * 0.08)); p.drawLine(QPointF(center.x() + base * 0.12, footY + base * 0.08), QPointF(center.x() + base * 0.48, footY - base * 0.08)); break;
    case 6: p.drawRect(QRectF(center.x() - base * 0.10, footY - base * 0.10, base * 0.20, base * 0.20)); break;
    default: p.drawEllipse(QPointF(center.x(), footY), base * 0.10, base * 0.10); break;
    }

    p.setPen(linePen(1.0, QColor(255,248,231,98)));
    switch (sideMark) {
    case 0: p.drawLine(QPointF(center.x() - base * 1.18, center.y() - base * 0.12), QPointF(center.x() - base * 0.88, center.y() - base * 0.12)); break;
    case 1: p.drawLine(QPointF(center.x() + base * 0.88, center.y() + base * 0.14), QPointF(center.x() + base * 1.18, center.y() + base * 0.14)); break;
    case 2: p.drawArc(QRectF(center.x() - base * 1.28, center.y() - base * 0.34, base * 0.30, base * 0.30), 270 * 16, 140 * 16); break;
    case 3: p.drawArc(QRectF(center.x() + base * 0.98, center.y() - base * 0.24, base * 0.28, base * 0.28), 90 * 16, 140 * 16); break;
    default: p.drawEllipse(QPointF(center.x() - base * 1.02, center.y() + base * 0.54), base * 0.07, base * 0.07); break;
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
    drawMarqueeLine(p, QRectF(x, rect.top() + 20.0, width, 34.0),
                     title, QColor(255, 248, 231, 235), true);

    p.setFont(fontForPixelSize(12, QFont::Normal));
    drawMarqueeLine(p, QRectF(x, rect.top() + 53.0, width, 20.0),
                     sub, QColor(255, 248, 231, 108), true);

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
                   QStringLiteral("Enter / A = Play  |  Esc / B = Back"));
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
    const qreal radius = 3.5;
    QPainterPath clipPath;
    clipPath.addRoundedRect(rect, radius, radius);
    p.setClipPath(clipPath);

    QLinearGradient shell(rect.topLeft(), rect.bottomRight());
    shell.setColorAt(0.0, QColor(255,255,255,12));
    shell.setColorAt(0.22, QColor(19,26,40,238));
    shell.setColorAt(1.0, QColor(7,11,19,245));
    p.fillPath(clipPath, shell);

    const qreal labelH = 34.0 + 8.0 * selection;
    const QRectF artArea(rect.left() + 4.0, rect.top() + 4.0,
                         rect.width() - 8.0, rect.height() - labelH - 8.0);

    const QVector<KadiaGameInfo> &games = kadiaGames();
    const KadiaGameInfo *game = (index >= 0 && index < games.size()) ? &games[index] : 0;
    const QImage cover = game ? coverImageForPath(game->coverPath) : QImage();

    if (!cover.isNull()) {
        QSize scaled = cover.size();
        scaled.scale(artArea.size().toSize(), Qt::KeepAspectRatio);
        const QRectF target(artArea.left() + (artArea.width() - scaled.width()) * 0.5,
                            artArea.top() + (artArea.height() - scaled.height()) * 0.5,
                            scaled.width(), scaled.height());
        p.drawImage(target, cover, QRectF(0.0, 0.0, cover.width(), cover.height()));
        p.fillRect(artArea, QColor(255,255,255,10));
    } else {
        QLinearGradient bg(rect.topLeft(), rect.bottomRight());
        bg.setColorAt(0.0, a1[index % 6]);
        bg.setColorAt(0.58, a2[index % 6]);
        bg.setColorAt(1.0, a3[index % 6]);
        p.fillRect(artArea, bg);
    }

    QLinearGradient topSpec(rect.left(), rect.top(), rect.left(), rect.top() + rect.height() * 0.28);
    topSpec.setColorAt(0.0, QColor(255,255,255,24));
    topSpec.setColorAt(1.0, QColor(255,255,255,0));
    p.fillRect(QRectF(rect.left()+1.0, rect.top()+1.0, rect.width()-2.0, rect.height()*0.22), topSpec);

    p.setClipping(false);
    p.setPen(QPen(QColor(255, 248, 231, static_cast<int>(28 + 120 * selection)), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(rect, radius, radius);

    QLinearGradient labelGrad(rect.left(), rect.bottom() - labelH, rect.left(), rect.bottom());
    labelGrad.setColorAt(0.0, QColor(5,8,14,60));
    labelGrad.setColorAt(1.0, QColor(5,8,14,248));
    p.fillRect(QRectF(rect.left()+1.0, rect.bottom()-labelH, rect.width()-2.0, labelH-1.0), labelGrad);

    if (game) {
        p.setFont(fontForPixelSize(static_cast<int>(11.0 + 2.0 * selection), QFont::Normal));
        drawMarqueeLine(p, QRectF(rect.left()+9.0, rect.bottom()-labelH, rect.width()-18.0, labelH),
                         game->title, QColor(255,248,231,238), selection > 0.50f);
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
    const QString status = QStringLiteral("Controller %1  Keyboard  %2x%3")
        .arg(m_controllerConnected ? QStringLiteral("on") : QStringLiteral("off"))
        .arg(static_cast<int>(canvasWidth())).arg(static_cast<int>(canvasHeight()));
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
        { QStringLiteral("LR"), QStringLiteral("Browse") },
        { QStringLiteral("UD"), QStringLiteral("Sections") }
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

void KadiaScene::drawMarqueeLine(QPainter &p, const QRectF &rect, const QString &text,
                                  const QColor &color, bool animate,
                                  const QPointF &shadowOffset)
{
    if (rect.isEmpty() || text.isEmpty())
        return;

    const QFontMetricsF fm(p.font());
    const qreal textWidth = fm.width(text);
    if (textWidth <= rect.width() + 0.5) {
        if (!shadowOffset.isNull()) {
            p.setPen(QColor(0, 0, 0, qMin(180, color.alpha())));
            p.drawText(rect.translated(shadowOffset), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text);
        }
        p.setPen(color);
        p.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text);
        return;
    }

    if (!animate) {
        const QString shown = QFontMetrics(p.font()).elidedText(text, Qt::ElideRight,
                                                                qMax(12, static_cast<int>(rect.width())));
        p.setPen(color);
        p.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, shown);
        return;
    }

    // Phone-style marquee: wait, glide to the far end, wait, glide back.
    // Everything is derived from m_time so speed is independent of monitor Hz.
    const qreal overflow = qMax<qreal>(0.0, textWidth - rect.width());
    const qreal pause = 1.05;
    const qreal pixelsPerSecond = 52.0;
    const qreal travel = qMax<qreal>(0.20, overflow / pixelsPerSecond);
    const qreal period = pause + travel + pause + travel;
    qreal phase = std::fmod(qMax(0.0, m_time), static_cast<double>(period));
    qreal offset = 0.0;
    if (phase < pause) {
        offset = 0.0;
    } else if ((phase -= pause) < travel) {
        const qreal t = qBound<qreal>(0.0, phase / travel, 1.0);
        offset = overflow * (t * t * (3.0 - 2.0 * t));
    } else if ((phase -= travel) < pause) {
        offset = overflow;
    } else {
        phase -= pause;
        const qreal t = qBound<qreal>(0.0, phase / travel, 1.0);
        offset = overflow * (1.0 - t * t * (3.0 - 2.0 * t));
    }

    p.save();
    p.setClipRect(rect);
    const qreal baseline = rect.center().y() + (fm.ascent() - fm.descent()) * 0.5;
    const QPointF point(rect.left() - offset, baseline);
    if (!shadowOffset.isNull()) {
        p.setPen(QColor(0, 0, 0, qMin(180, color.alpha())));
        p.drawText(point + shadowOffset, text);
    }
    p.setPen(color);
    p.drawText(point, text);
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
