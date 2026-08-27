#include "background_settings.h"
#include "kadia_scene.h"

#include <QApplication>
#include <QDesktopWidget>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QList>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
// WIN32_LEAN_AND_MEAN is enabled globally for the XP build.  GDI+ headers
// require COM stream/property declarations which windows.h omits in lean mode.
// Include the COM definitions explicitly before gdiplus.h; otherwise VS2017
// with the Windows 10 SDK reports IStream / PROPID / IImageBytes as unknown.
#  include <windows.h>
#  include <objidl.h>
#  include <propidl.h>
#  include <gdiplus.h>
#endif

namespace {

static QString dialogStyle()
{
    return QStringLiteral(
        "QDialog { background:#070b12; color:#fff8e7; border:1px solid rgba(255,248,231,42); }"
        "QLabel { color:#fff8e7; background:transparent; }"
        "QListWidget { background:#0b1019; color:#fff8e7; border:1px solid rgba(255,248,231,45); outline:none; }"
        "QListWidget::item { padding:8px 10px; }"
        "QListWidget::item:selected { background:#343842; color:#fff8e7; border:1px solid #9d978a; }"
        "QPushButton { color:#fff8e7; background:#151b26; border:1px solid rgba(255,248,231,60); border-radius:4px; padding:7px 18px; }"
        "QPushButton:focus { border:1px solid #fff0c8; background:#252a34; }"
        "QSlider::groove:horizontal { height:6px; background:#171d27; border:1px solid #343b47; }"
        "QSlider::handle:horizontal { width:18px; margin:-6px 0; border-radius:9px; background:#fff0c8; border:1px solid #8e897d; }"
        "QSlider:focus { background:transparent; }");
}

static void centerDialog(QDialog *dialog, QWidget *parent)
{
    QRect target;
    if (parent)
        target = parent->frameGeometry();
    else {
        QDesktopWidget *desktop = QApplication::desktop();
        target = desktop ? desktop->screenGeometry(desktop->primaryScreen()) : QRect(0, 0, 1280, 720);
    }
    dialog->move(target.center() - QPoint(dialog->width() / 2, dialog->height() / 2));
}

static bool isImageExtension(const QString &suffix)
{
    const QString s = suffix.toLower();
    return s == QStringLiteral("png") || s == QStringLiteral("jpg") || s == QStringLiteral("jpeg") ||
           s == QStringLiteral("bmp") || s == QStringLiteral("gif") || s == QStringLiteral("tif") ||
           s == QStringLiteral("tiff") || s == QStringLiteral("webp");
}

#ifdef Q_OS_WIN
static QImage loadWithGdiPlus(const QString &path)
{
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&token, &input, 0) != Gdiplus::Ok)
        return QImage();

    QImage result;

    // Every GDI+ object must be destroyed before GdiplusShutdown().  In the
    // previous implementation `bitmap` lived until function exit, so its
    // destructor called GdipDisposeImage() after GDI+ had already been shut
    // down.  On Windows this is an INVALID_POINTER_READ in GdiPlus.dll during
    // startup whenever a desktop/custom wallpaper is restored.
    {
        Gdiplus::Bitmap bitmap(reinterpret_cast<const WCHAR *>(path.utf16()));
        if (bitmap.GetLastStatus() == Gdiplus::Ok && bitmap.GetWidth() > 0 && bitmap.GetHeight() > 0) {
            const UINT w = bitmap.GetWidth();
            const UINT h = bitmap.GetHeight();
            Gdiplus::Rect rect(0, 0, static_cast<INT>(w), static_cast<INT>(h));
            Gdiplus::BitmapData data;
            if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) == Gdiplus::Ok) {
                QImage wrapped(static_cast<uchar *>(data.Scan0), static_cast<int>(w), static_cast<int>(h),
                               data.Stride, QImage::Format_ARGB32);
                result = wrapped.copy();
                bitmap.UnlockBits(&data);
            }
        }
    }

    Gdiplus::GdiplusShutdown(token);
    return result;
}
#endif

}

namespace BackgroundSettings {

BackgroundPreferences load()
{
    QSettings s;
    BackgroundPreferences p;
    p.mode = s.value(QStringLiteral("background/mode"), static_cast<int>(BackgroundPreferences::KadiaDefault)).toInt();
    p.customPath = s.value(QStringLiteral("background/customPath")).toString();
    p.opacity = qBound(0, s.value(QStringLiteral("background/opacity"), 100).toInt(), 100);
    return p;
}

void save(const BackgroundPreferences &p)
{
    QSettings s;
    s.setValue(QStringLiteral("background/mode"), p.mode);
    s.setValue(QStringLiteral("background/customPath"), p.customPath);
    s.setValue(QStringLiteral("background/opacity"), qBound(0, p.opacity, 100));
    s.sync();
}

QString desktopWallpaperPath()
{
#ifdef Q_OS_WIN
    QSettings desktop(QStringLiteral("HKEY_CURRENT_USER\\Control Panel\\Desktop"), QSettings::NativeFormat);
    return QDir::fromNativeSeparators(desktop.value(QStringLiteral("Wallpaper")).toString());
#else
    return QString();
#endif
}

QImage loadImage(const QString &path)
{
    if (path.isEmpty() || !QFileInfo(path).exists())
        return QImage();
#ifdef Q_OS_WIN
    QImage gdi = loadWithGdiPlus(path);
    if (!gdi.isNull())
        return gdi;
#endif
    QImageReader reader(path);
    reader.setAutoTransform(true);
    return reader.read();
}

void applyToScene(KadiaScene *scene, const BackgroundPreferences &p)
{
    if (!scene)
        return;

    QImage image;
    if (p.mode == BackgroundPreferences::DesktopWallpaper)
        image = loadImage(desktopWallpaperPath());
    else if (p.mode == BackgroundPreferences::CustomImage)
        image = loadImage(p.customPath);

    scene->setBackgroundImage(image);
    scene->setBackgroundOpacity(static_cast<qreal>(qBound(0, p.opacity, 100)) / 100.0);
}

}

ImageBrowserDialog::ImageBrowserDialog(QWidget *parent)
    : QDialog(parent)
    , m_pathLabel(new QLabel(this))
    , m_list(new QListWidget(this))
    , m_select(new QPushButton(QStringLiteral("Open / Select"), this))
    , m_cancel(new QPushButton(QStringLiteral("Cancel"), this))
    , m_input(this)
    , m_inputTimer(new QTimer(this))
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setModal(true);
    resize(690, 500);
    setStyleSheet(dialogStyle());

    QLabel *title = new QLabel(QStringLiteral("Choose background image"), this);
    QFont tf = title->font(); tf.setPixelSize(28); tf.setWeight(QFont::Light); title->setFont(tf);
    m_pathLabel->setWordWrap(true);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addStretch(); buttons->addWidget(m_select); buttons->addWidget(m_cancel);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(26, 22, 26, 22);
    layout->setSpacing(10);
    layout->addWidget(title); layout->addWidget(m_pathLabel); layout->addWidget(m_list, 1); layout->addLayout(buttons);

    connect(m_select, SIGNAL(clicked()), this, SLOT(activateCurrent()));
    connect(m_cancel, SIGNAL(clicked()), this, SLOT(reject()));
    connect(m_list, SIGNAL(itemDoubleClicked(QListWidgetItem*)), this, SLOT(itemDoubleClicked()));
    m_input.initialize();
    connect(m_inputTimer, SIGNAL(timeout()), this, SLOT(pollController()));
    m_inputTimer->start(16);

    showDrives();
    centerDialog(this, parent);
}

QString ImageBrowserDialog::selectedPath() const { return m_selectedPath; }

void ImageBrowserDialog::showDrives()
{
    m_currentPath.clear();
    m_pathLabel->setText(QStringLiteral("Computer"));
    m_list->clear();
    const QFileInfoList drives = QDir::drives();
    for (int i = 0; i < drives.size(); ++i) {
        QListWidgetItem *item = new QListWidgetItem(QStringLiteral("[Drive] %1").arg(QDir::toNativeSeparators(drives[i].absoluteFilePath())), m_list);
        item->setData(Qt::UserRole, drives[i].absoluteFilePath());
        item->setData(Qt::UserRole + 1, 1);
    }
    if (m_list->count() > 0) m_list->setCurrentRow(0);
}

void ImageBrowserDialog::showLocation(const QString &path)
{
    QDir dir(path);
    if (!dir.exists()) return;
    m_currentPath = dir.absolutePath();
    m_pathLabel->setText(QDir::toNativeSeparators(m_currentPath));
    m_list->clear();

    const QFileInfoList dirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (int i = 0; i < dirs.size(); ++i) {
        QListWidgetItem *item = new QListWidgetItem(QStringLiteral("[Folder] %1").arg(dirs[i].fileName()), m_list);
        item->setData(Qt::UserRole, dirs[i].absoluteFilePath());
        item->setData(Qt::UserRole + 1, 1);
    }
    const QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (int i = 0; i < files.size(); ++i) {
        if (!isImageExtension(files[i].suffix())) continue;
        QListWidgetItem *item = new QListWidgetItem(files[i].fileName(), m_list);
        item->setData(Qt::UserRole, files[i].absoluteFilePath());
        item->setData(Qt::UserRole + 1, 0);
    }
    if (m_list->count() > 0) m_list->setCurrentRow(0);
}

void ImageBrowserDialog::activateCurrent()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item) return;
    const QString path = item->data(Qt::UserRole).toString();
    const bool directory = item->data(Qt::UserRole + 1).toInt() != 0;
    if (directory) showLocation(path);
    else { m_selectedPath = path; accept(); }
}

void ImageBrowserDialog::goBack()
{
    if (m_currentPath.isEmpty()) { reject(); return; }
    QDir dir(m_currentPath);
    if (!dir.cdUp()) showDrives();
    else {
        const QString parentPath = dir.absolutePath();
        const QFileInfoList drives = QDir::drives();
        bool isDriveRoot = false;
        for (int i = 0; i < drives.size(); ++i)
            if (QDir(drives[i].absoluteFilePath()).absolutePath() == QDir(m_currentPath).absolutePath()) isDriveRoot = true;
        if (isDriveRoot) showDrives(); else showLocation(parentPath);
    }
}

void ImageBrowserDialog::pollController()
{
    const InputManager::Action a = m_input.poll();
    if (a == InputManager::Up) m_list->setCurrentRow(qMax(0, m_list->currentRow() - 1));
    else if (a == InputManager::Down) m_list->setCurrentRow(qMin(m_list->count() - 1, m_list->currentRow() + 1));
    else if (a == InputManager::Accept) activateCurrent();
    else if (a == InputManager::Back) goBack();
}

void ImageBrowserDialog::itemDoubleClicked() { activateCurrent(); }

BackgroundSettingsDialog::BackgroundSettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_modes(new QListWidget(this))
    , m_opacity(new QSlider(Qt::Horizontal, this))
    , m_opacityLabel(new QLabel(this))
    , m_pathLabel(new QLabel(this))
    , m_browse(new QPushButton(QStringLiteral("Browse image"), this))
    , m_apply(new QPushButton(QStringLiteral("Apply"), this))
    , m_cancel(new QPushButton(QStringLiteral("Cancel"), this))
    , m_preferences(BackgroundSettings::load())
    , m_input(this)
    , m_inputTimer(new QTimer(this))
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setModal(true);
    resize(620, 430);
    setStyleSheet(dialogStyle());

    QLabel *title = new QLabel(QStringLiteral("Kadia background"), this);
    QFont tf = title->font(); tf.setPixelSize(30); tf.setWeight(QFont::Light); title->setFont(tf);
    QLabel *sub = new QLabel(QStringLiteral("The Aero ribbons and Win98 starfield remain active above every background mode."), this);
    sub->setWordWrap(true);

    m_modes->addItem(QStringLiteral("Kadia default"));
    m_modes->addItem(QStringLiteral("Desktop wallpaper (translucent)"));
    m_modes->addItem(QStringLiteral("Custom image"));
    m_modes->setFixedHeight(125);
    m_modes->setCurrentRow(qBound(0, m_preferences.mode, 2));

    m_opacity->setRange(0, 100);
    m_opacity->setValue(m_preferences.opacity);
    m_pathLabel->setWordWrap(true);

    QHBoxLayout *opacityRow = new QHBoxLayout;
    opacityRow->addWidget(new QLabel(QStringLiteral("Background opacity"), this));
    opacityRow->addWidget(m_opacity, 1);
    opacityRow->addWidget(m_opacityLabel);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(m_browse); buttons->addStretch(); buttons->addWidget(m_apply); buttons->addWidget(m_cancel);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24); layout->setSpacing(12);
    layout->addWidget(title); layout->addWidget(sub); layout->addWidget(m_modes); layout->addLayout(opacityRow); layout->addWidget(m_pathLabel); layout->addStretch(); layout->addLayout(buttons);

    connect(m_modes, SIGNAL(currentRowChanged(int)), this, SLOT(updateUi()));
    connect(m_opacity, SIGNAL(valueChanged(int)), this, SLOT(updateUi()));
    connect(m_browse, SIGNAL(clicked()), this, SLOT(browseImage()));
    connect(m_apply, SIGNAL(clicked()), this, SLOT(applyAndAccept()));
    connect(m_cancel, SIGNAL(clicked()), this, SLOT(reject()));

    m_input.initialize();
    connect(m_inputTimer, SIGNAL(timeout()), this, SLOT(pollController()));
    m_inputTimer->start(16);
    updateUi();
    centerDialog(this, parent);
}

BackgroundPreferences BackgroundSettingsDialog::preferences() const { return m_preferences; }

void BackgroundSettingsDialog::browseImage()
{
    ImageBrowserDialog browser(this);
    if (browser.exec() == QDialog::Accepted && !browser.selectedPath().isEmpty()) {
        m_preferences.customPath = browser.selectedPath();
        m_modes->setCurrentRow(BackgroundPreferences::CustomImage);
        updateUi();
    }
}

void BackgroundSettingsDialog::updateUi()
{
    m_preferences.mode = m_modes->currentRow();
    m_preferences.opacity = m_opacity->value();
    m_opacityLabel->setText(QStringLiteral("%1%").arg(m_preferences.opacity));
    if (m_preferences.mode == BackgroundPreferences::DesktopWallpaper)
        m_pathLabel->setText(QStringLiteral("Desktop wallpaper: %1").arg(QDir::toNativeSeparators(BackgroundSettings::desktopWallpaperPath())));
    else if (m_preferences.mode == BackgroundPreferences::CustomImage)
        m_pathLabel->setText(m_preferences.customPath.isEmpty() ? QStringLiteral("No custom image selected") : QDir::toNativeSeparators(m_preferences.customPath));
    else
        m_pathLabel->setText(QStringLiteral("Original Kadia background"));
    m_browse->setEnabled(m_preferences.mode == BackgroundPreferences::CustomImage);
}

void BackgroundSettingsDialog::applyAndAccept()
{
    updateUi();
    if (m_preferences.mode == BackgroundPreferences::CustomImage && m_preferences.customPath.isEmpty())
        return;
    BackgroundSettings::save(m_preferences);
    accept();
}

void BackgroundSettingsDialog::pollController()
{
    const InputManager::Action a = m_input.poll();
    if (a == InputManager::None) return;

    QWidget *f = focusWidget();
    if (!f) { m_modes->setFocus(); return; }
    if (a == InputManager::Back) { reject(); return; }

    if (f == m_modes) {
        if (a == InputManager::Up) m_modes->setCurrentRow(qMax(0, m_modes->currentRow() - 1));
        else if (a == InputManager::Down) m_modes->setCurrentRow(qMin(m_modes->count() - 1, m_modes->currentRow() + 1));
        else if (a == InputManager::Right || a == InputManager::Accept) m_opacity->setFocus();
        return;
    }

    if (f == m_opacity) {
        if (a == InputManager::Left) m_opacity->setValue(qMax(0, m_opacity->value() - 5));
        else if (a == InputManager::Right) m_opacity->setValue(qMin(100, m_opacity->value() + 5));
        else if (a == InputManager::Up) m_modes->setFocus();
        else if (a == InputManager::Down || a == InputManager::Accept) {
            if (m_browse->isEnabled()) m_browse->setFocus(); else m_apply->setFocus();
        }
        return;
    }

    QList<QPushButton *> buttons;
    if (m_browse->isEnabled()) buttons << m_browse;
    buttons << m_apply << m_cancel;
    int index = buttons.indexOf(qobject_cast<QPushButton *>(f));
    if (index < 0) index = 0;
    if (a == InputManager::Left) index = (index - 1 + buttons.size()) % buttons.size();
    else if (a == InputManager::Right) index = (index + 1) % buttons.size();
    else if (a == InputManager::Up) { m_opacity->setFocus(); return; }
    else if (a == InputManager::Accept) { buttons[index]->click(); return; }
    else if (a == InputManager::Down) index = (index + 1) % buttons.size();
    buttons[index]->setFocus();
}
