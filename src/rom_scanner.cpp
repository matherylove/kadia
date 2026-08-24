#include "rom_scanner.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDesktopWidget>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

namespace {

static QString catalogPath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) base = QDir::homePath() + QStringLiteral("/.mathery-kadia");
    QDir().mkpath(base);
    return QDir(base).filePath(QStringLiteral("rom-catalog.ini"));
}

static QString keyForPath(const QString &path)
{
    return QString::fromLatin1(QCryptographicHash::hash(QDir::cleanPath(path).toLower().toUtf8(), QCryptographicHash::Sha1).toHex());
}

static QString hintForSuffix(const QString &suffix)
{
    const QString s = suffix.toLower();
    if (s == QStringLiteral("nes")) return QStringLiteral("Nintendo Entertainment System");
    if (s == QStringLiteral("sfc") || s == QStringLiteral("smc")) return QStringLiteral("Super Nintendo");
    if (s == QStringLiteral("n64") || s == QStringLiteral("z64") || s == QStringLiteral("v64")) return QStringLiteral("Nintendo 64");
    if (s == QStringLiteral("gb")) return QStringLiteral("Game Boy");
    if (s == QStringLiteral("gbc")) return QStringLiteral("Game Boy Color");
    if (s == QStringLiteral("gba")) return QStringLiteral("Game Boy Advance");
    if (s == QStringLiteral("nds")) return QStringLiteral("Nintendo DS");
    if (s == QStringLiteral("3ds") || s == QStringLiteral("cia")) return QStringLiteral("Nintendo 3DS");
    if (s == QStringLiteral("md") || s == QStringLiteral("gen") || s == QStringLiteral("smd")) return QStringLiteral("Sega Genesis / Mega Drive");
    if (s == QStringLiteral("sms")) return QStringLiteral("Sega Master System");
    if (s == QStringLiteral("gg")) return QStringLiteral("Sega Game Gear");
    if (s == QStringLiteral("gdi") || s == QStringLiteral("cdi")) return QStringLiteral("Sega Dreamcast");
    if (s == QStringLiteral("pce")) return QStringLiteral("PC Engine / TurboGrafx-16");
    if (s == QStringLiteral("ws")) return QStringLiteral("WonderSwan");
    if (s == QStringLiteral("wsc")) return QStringLiteral("WonderSwan Color");
    if (s == QStringLiteral("ngp")) return QStringLiteral("Neo Geo Pocket");
    if (s == QStringLiteral("ngc")) return QStringLiteral("Neo Geo Pocket Color");
    if (s == QStringLiteral("a26")) return QStringLiteral("Atari 2600");
    if (s == QStringLiteral("a52")) return QStringLiteral("Atari 5200");
    if (s == QStringLiteral("a78")) return QStringLiteral("Atari 7800");
    if (s == QStringLiteral("lnx") || s == QStringLiteral("lynx")) return QStringLiteral("Atari Lynx");
    if (s == QStringLiteral("cso")) return QStringLiteral("PlayStation Portable");
    if (s == QStringLiteral("xci") || s == QStringLiteral("nsp")) return QStringLiteral("Nintendo Switch");
    if (s == QStringLiteral("wad")) return QStringLiteral("Nintendo Wii");
    if (s == QStringLiteral("wbfs") || s == QStringLiteral("rvz")) return QStringLiteral("Nintendo Wii / GameCube");
    if (s == QStringLiteral("iso") || s == QStringLiteral("bin") || s == QStringLiteral("cue") ||
        s == QStringLiteral("rom") || s == QStringLiteral("chd") || s == QStringLiteral("img") ||
        s == QStringLiteral("mdf") || s == QStringLiteral("pbp")) return QStringLiteral("Unknown");
    return QString();
}

static bool candidateSuffix(const QString &suffix)
{
    return !hintForSuffix(suffix).isEmpty();
}

static QString styleSheet()
{
    return QStringLiteral(
        "QDialog { background:#070b12; color:#fff8e7; border:1px solid rgba(255,248,231,42); }"
        "QLabel { color:#fff8e7; background:transparent; }"
        "QListWidget { background:#0b1019; color:#fff8e7; border:1px solid rgba(255,248,231,45); outline:none; }"
        "QListWidget::item { padding:7px 10px; }"
        "QListWidget::item:selected { background:#343842; color:#fff8e7; border:1px solid #9d978a; }"
        "QPushButton { color:#fff8e7; background:#151b26; border:1px solid rgba(255,248,231,60); border-radius:4px; padding:7px 18px; }"
        "QPushButton:focus { background:#252a34; border:1px solid #fff0c8; }");
}

}

namespace RomCatalog {

bool isKnown(const QString &path)
{
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files/%1").arg(keyForPath(path)));
    const bool known = s.contains(QStringLiteral("classification"));
    s.endGroup();
    return known;
}

void saveClassification(const QString &path, const QString &system)
{
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files/%1").arg(keyForPath(path)));
    s.setValue(QStringLiteral("path"), QDir::toNativeSeparators(path));
    s.setValue(QStringLiteral("classification"), system);
    s.endGroup(); s.sync();
}

QString classification(const QString &path)
{
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files/%1").arg(keyForPath(path)));
    const QString c = s.value(QStringLiteral("classification")).toString();
    s.endGroup(); return c;
}

QStringList systems()
{
    return QStringList()
        << QStringLiteral("None (ignore)")
        << QStringLiteral("Unknown")
        << QStringLiteral("Nintendo Entertainment System")
        << QStringLiteral("Super Nintendo")
        << QStringLiteral("Nintendo 64")
        << QStringLiteral("Game Boy")
        << QStringLiteral("Game Boy Color")
        << QStringLiteral("Game Boy Advance")
        << QStringLiteral("Nintendo DS")
        << QStringLiteral("Nintendo 3DS")
        << QStringLiteral("Nintendo GameCube")
        << QStringLiteral("Nintendo Wii")
        << QStringLiteral("Nintendo Wii U")
        << QStringLiteral("Nintendo Switch")
        << QStringLiteral("Sega Master System")
        << QStringLiteral("Sega Genesis / Mega Drive")
        << QStringLiteral("Sega Game Gear")
        << QStringLiteral("Sega Saturn")
        << QStringLiteral("Sega Dreamcast")
        << QStringLiteral("PlayStation")
        << QStringLiteral("PlayStation 2")
        << QStringLiteral("PlayStation 3")
        << QStringLiteral("PlayStation Portable")
        << QStringLiteral("PlayStation Vita")
        << QStringLiteral("Xbox")
        << QStringLiteral("Xbox 360")
        << QStringLiteral("Atari 2600")
        << QStringLiteral("Atari 5200")
        << QStringLiteral("Atari 7800")
        << QStringLiteral("Atari Lynx")
        << QStringLiteral("PC Engine / TurboGrafx-16")
        << QStringLiteral("Neo Geo")
        << QStringLiteral("Neo Geo Pocket")
        << QStringLiteral("Neo Geo Pocket Color")
        << QStringLiteral("WonderSwan")
        << QStringLiteral("WonderSwan Color")
        << QStringLiteral("MSX")
        << QStringLiteral("Commodore 64")
        << QStringLiteral("Amiga")
        << QStringLiteral("DOS / PC")
        << QStringLiteral("Arcade");
}

QStringList pathsForClassification(const QString &wanted)
{
    QStringList paths;
    QSettings s(catalogPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("files"));
    const QStringList groups = s.childGroups();
    for (int i = 0; i < groups.size(); ++i) {
        s.beginGroup(groups[i]);
        const QString classification = s.value(QStringLiteral("classification")).toString();
        const QString path = QDir::fromNativeSeparators(s.value(QStringLiteral("path")).toString());
        s.endGroup();
        if (classification.compare(wanted, Qt::CaseInsensitive) == 0 && !path.isEmpty() && QFileInfo(path).exists())
            paths << path;
    }
    s.endGroup();
    paths.removeDuplicates();
    paths.sort(Qt::CaseInsensitive);
    return paths;
}

}

RomScanner::RomScanner(QObject *parent) : QThread(parent), m_stop(false) {}
RomScanner::~RomScanner(){ requestStop(); wait(); }
void RomScanner::requestStop(){ m_stop = true; }

void RomScanner::run()
{
    const QFileInfoList drives = QDir::drives();
    for (int d = 0; d < drives.size() && !m_stop; ++d) {
        const QString root = drives[d].absoluteFilePath();
        emit scanStatus(QStringLiteral("Scanning %1 for ROM files...").arg(QDir::toNativeSeparators(root)));
        QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot | QDir::Readable, QDirIterator::Subdirectories);
        while (it.hasNext() && !m_stop) {
            const QString path = it.next();
            const QFileInfo fi = it.fileInfo();
            if (!candidateSuffix(fi.suffix())) continue;
            if (RomCatalog::isKnown(path)) continue;
            emit romDiscovered(path, hintForSuffix(fi.suffix()));
        }
    }
    emit scanFinished();
}

RomClassificationDialog::RomClassificationDialog(const QString &path, const QString &hint, QWidget *parent)
    : QDialog(parent), m_path(path), m_pathLabel(new QLabel(this)), m_hintLabel(new QLabel(this)),
      m_systems(new QListWidget(this)), m_confirm(new QPushButton(QStringLiteral("Confirm"), this)),
      m_later(new QPushButton(QStringLiteral("Later"), this)), m_input(this), m_inputTimer(new QTimer(this))
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint); setModal(true); resize(720, 520); setStyleSheet(styleSheet());
    QLabel *title = new QLabel(QStringLiteral("New ROM detected"), this); QFont tf=title->font(); tf.setPixelSize(28); tf.setWeight(QFont::Light); title->setFont(tf);
    m_pathLabel->setText(QDir::toNativeSeparators(path)); m_pathLabel->setWordWrap(true);
    m_hintLabel->setText(QStringLiteral("Detected hint: %1. Confirm the console, choose Unknown for ambiguous images, or None to discard this file permanently.").arg(hint)); m_hintLabel->setWordWrap(true);
    m_systems->addItems(RomCatalog::systems());
    int row=m_systems->findItems(hint,Qt::MatchFixedString).isEmpty()?1:m_systems->row(m_systems->findItems(hint,Qt::MatchFixedString).first()); m_systems->setCurrentRow(row);
    QHBoxLayout *buttons=new QHBoxLayout; buttons->addStretch(); buttons->addWidget(m_confirm); buttons->addWidget(m_later);
    QVBoxLayout *layout=new QVBoxLayout(this); layout->setContentsMargins(28,22,28,22); layout->setSpacing(10); layout->addWidget(title); layout->addWidget(m_pathLabel); layout->addWidget(m_hintLabel); layout->addWidget(m_systems,1); layout->addLayout(buttons);
    connect(m_confirm,SIGNAL(clicked()),this,SLOT(confirmSelection())); connect(m_later,SIGNAL(clicked()),this,SLOT(deferSelection()));
    m_input.initialize(); connect(m_inputTimer,SIGNAL(timeout()),this,SLOT(pollController())); m_inputTimer->start(16);
    QRect target=parent?parent->frameGeometry():QApplication::desktop()->screenGeometry(QApplication::desktop()->primaryScreen()); move(target.center()-rect().center()); m_systems->setFocus();
}

QString RomClassificationDialog::selectedSystem() const { return m_selectedSystem; }
void RomClassificationDialog::confirmSelection(){QListWidgetItem *item=m_systems->currentItem();if(!item)return;m_selectedSystem=item->text();accept();}
void RomClassificationDialog::deferSelection(){reject();}
void RomClassificationDialog::pollController(){const InputManager::Action a=m_input.poll();if(a==InputManager::None)return;if(a==InputManager::Up)m_systems->setCurrentRow(qMax(0,m_systems->currentRow()-1));else if(a==InputManager::Down)m_systems->setCurrentRow(qMin(m_systems->count()-1,m_systems->currentRow()+1));else if(a==InputManager::Accept)confirmSelection();else if(a==InputManager::Back)deferSelection();}
