#include "kadia_settings.h"
#include "background_settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

QString KadiaSettings::settingsPath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) base = QDir::homePath() + QStringLiteral("/.mathery-kadia");
    QDir().mkpath(base);
    return QDir(base).filePath(QStringLiteral("settings.ini"));
}

int KadiaSettings::defaultGallerySort()
{
    QSettings s(settingsPath(), QSettings::IniFormat);
    return qBound(0, s.value(QStringLiteral("gallery/defaultSort"), 0).toInt(), 4);
}

bool KadiaSettings::showClock()
{
    QSettings s(settingsPath(), QSettings::IniFormat);
    return s.value(QStringLiteral("interface/showClock"), true).toBool();
}

KadiaSettingsDialog::KadiaSettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_defaultSort(new QComboBox(this))
    , m_showClock(new QCheckBox(QStringLiteral("Show clock"), this))
    , m_backgroundPath(new QLineEdit(this))
{
    setWindowTitle(QStringLiteral("Kadia Settings"));
    setMinimumSize(560, 360);
    setModal(true);

    QLabel *title = new QLabel(QStringLiteral("Mathery Kadia! Settings"), this);
    QFont tf = title->font(); tf.setPixelSize(24); tf.setWeight(QFont::Light); title->setFont(tf);

    m_defaultSort->addItem(QStringLiteral("Alphabetical"));
    m_defaultSort->addItem(QStringLiteral("Release date"));
    m_defaultSort->addItem(QStringLiteral("Played / unplayed"));
    m_defaultSort->addItem(QStringLiteral("Hours played"));
    m_defaultSort->addItem(QStringLiteral("Date added"));

    QSettings s(KadiaSettings::settingsPath(), QSettings::IniFormat);
    m_defaultSort->setCurrentIndex(KadiaSettings::defaultGallerySort());
    m_showClock->setChecked(KadiaSettings::showClock());
    m_backgroundPath->setText(QDir::fromNativeSeparators(s.value(QStringLiteral("background/path")).toString()));

    QPushButton *browse = new QPushButton(QStringLiteral("Browse..."), this);
    connect(browse, SIGNAL(clicked()), this, SLOT(chooseBackground()));
    QHBoxLayout *bgRow = new QHBoxLayout;
    bgRow->addWidget(m_backgroundPath, 1);
    bgRow->addWidget(browse);

    QFormLayout *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->addRow(QStringLiteral("Default gallery sort:"), m_defaultSort);
    form->addRow(QString(), m_showClock);
    form->addRow(QStringLiteral("Background image:"), bgRow);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, SIGNAL(accepted()), this, SLOT(saveAndClose()));
    connect(buttons, SIGNAL(rejected()), this, SLOT(reject()));

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(18);
    layout->addWidget(title);
    layout->addLayout(form);
    layout->addStretch();
    layout->addWidget(buttons);

    setStyleSheet(QStringLiteral(
        "QDialog{background:#101623;color:#fff8e7;} QLabel,QCheckBox{color:#fff8e7;}"
        "QLineEdit,QComboBox{background:#182133;color:#fff8e7;border:1px solid #59657d;padding:6px;}"
        "QPushButton{background:#202b40;color:#fff8e7;border:1px solid #69758c;padding:7px 18px;border-radius:6px;}"
        "QPushButton:hover,QPushButton:focus{background:#33415c;border-color:#e8d6aa;}"));
}

void KadiaSettingsDialog::chooseBackground()
{
    ImageBrowserDialog browser(this);
    if (browser.exec() == QDialog::Accepted && !browser.selectedPath().isEmpty())
        m_backgroundPath->setText(browser.selectedPath());
}

void KadiaSettingsDialog::saveAndClose()
{
    QSettings s(KadiaSettings::settingsPath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("gallery/defaultSort"), m_defaultSort->currentIndex());
    s.setValue(QStringLiteral("interface/showClock"), m_showClock->isChecked());
    s.setValue(QStringLiteral("background/path"), QDir::toNativeSeparators(m_backgroundPath->text().trimmed()));
    s.sync();
    if (!m_backgroundPath->text().trimmed().isEmpty()) {
        BackgroundPreferences bg = BackgroundSettings::load();
        bg.mode = BackgroundPreferences::CustomImage;
        bg.customPath = m_backgroundPath->text().trimmed();
        BackgroundSettings::save(bg);
    }
    accept();
}
