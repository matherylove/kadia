#pragma once

#include <QDialog>
#include <QImage>
#include <QString>

#include "input_manager.h"

class KadiaScene;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QTimer;

struct BackgroundPreferences
{
    enum Mode {
        KadiaDefault = 0,
        DesktopWallpaper = 1,
        CustomImage = 2
    };

    int mode;
    QString customPath;
    int opacity;
};

namespace BackgroundSettings
{
    BackgroundPreferences load();
    void save(const BackgroundPreferences &preferences);
    QString desktopWallpaperPath();
    QImage loadImage(const QString &path);
    void applyToScene(KadiaScene *scene, const BackgroundPreferences &preferences);
}

class ImageBrowserDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ImageBrowserDialog(QWidget *parent = 0);
    QString selectedPath() const;

private slots:
    void activateCurrent();
    void goBack();
    void pollController();
    void itemDoubleClicked();

private:
    void showLocation(const QString &path);
    void showDrives();

    QLabel *m_pathLabel;
    QListWidget *m_list;
    QPushButton *m_select;
    QPushButton *m_cancel;
    QString m_currentPath;
    QString m_selectedPath;
    InputManager m_input;
    QTimer *m_inputTimer;
};

class BackgroundSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BackgroundSettingsDialog(QWidget *parent = 0);
    BackgroundPreferences preferences() const;

private slots:
    void browseImage();
    void updateUi();
    void applyAndAccept();
    void pollController();

private:
    QListWidget *m_modes;
    QSlider *m_opacity;
    QLabel *m_opacityLabel;
    QLabel *m_pathLabel;
    QPushButton *m_browse;
    QPushButton *m_apply;
    QPushButton *m_cancel;
    BackgroundPreferences m_preferences;
    InputManager m_input;
    QTimer *m_inputTimer;
};
