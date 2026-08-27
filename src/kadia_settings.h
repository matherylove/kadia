#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;

class KadiaSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit KadiaSettingsDialog(QWidget *parent = 0);

private slots:
    void saveAndClose();
    void chooseBackground();

private:
    QComboBox *m_defaultSort;
    QCheckBox *m_showClock;
    QLineEdit *m_backgroundPath;
};

namespace KadiaSettings
{
    QString settingsPath();
    int defaultGallerySort();
    bool showClock();
}
