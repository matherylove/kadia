#include "media_library_dialog.h"

#include <QAbstractItemView>
#include <QBoxLayout>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>

MediaLibraryScanThread::MediaLibraryScanThread(KadiaMediaKind kind, QObject *parent)
    : QThread(parent), m_kind(kind), m_cancel(0)
{
}

QVector<KadiaMediaItem> MediaLibraryScanThread::results() const
{
    return m_results;
}

void MediaLibraryScanThread::cancel()
{
    m_cancel.storeRelease(1);
}

void MediaLibraryScanThread::run()
{
    m_cancel.storeRelease(0);
    m_results = MediaLibrary::scan(m_kind, &m_cancel);
}

MediaLibraryDialog::MediaLibraryDialog(KadiaMediaKind kind, QWidget *parent, bool autoPlay)
    : QDialog(parent)
    , m_kind(kind)
    , m_scan(0)
    , m_status(new QLabel(this))
    , m_search(new QLineEdit(this))
    , m_list(new QListWidget(this))
    , m_open(new QPushButton(QStringLiteral("Open / Play"), this))
    , m_playAll(new QPushButton(QStringLiteral("Play All"), this))
    , m_rescan(new QPushButton(QStringLiteral("Rescan"), this))
    , m_autoPlay(autoPlay)
{
    setWindowTitle(QStringLiteral("Kadia - %1 Library").arg(MediaLibrary::kindLabel(kind)));
    resize(880, 600);
    m_search->setPlaceholderText(QStringLiteral("Search this media library..."));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_open->setEnabled(false);
    m_playAll->setVisible(kind != MediaPicture);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->addWidget(m_search);
    root->addWidget(m_list, 1);
    root->addWidget(m_status);
    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(m_open);
    buttons->addWidget(m_playAll);
    buttons->addStretch(1);
    buttons->addWidget(m_rescan);
    QPushButton *closeButton = new QPushButton(QStringLiteral("Close"), this);
    buttons->addWidget(closeButton);
    root->addLayout(buttons);

    connect(m_search, SIGNAL(textChanged(QString)), this, SLOT(applyFilter(QString)));
    connect(m_list, SIGNAL(itemSelectionChanged()), this, SLOT(selectionChanged()));
    connect(m_list, SIGNAL(itemDoubleClicked(QListWidgetItem*)), this, SLOT(openSelected()));
    connect(m_open, SIGNAL(clicked()), this, SLOT(openSelected()));
    connect(m_playAll, SIGNAL(clicked()), this, SLOT(playAll()));
    connect(m_rescan, SIGNAL(clicked()), this, SLOT(beginScan()));
    connect(closeButton, SIGNAL(clicked()), this, SLOT(reject()));

    beginScan();
}

MediaLibraryDialog::~MediaLibraryDialog()
{
    if (m_scan && m_scan->isRunning()) {
        m_scan->cancel();
        m_scan->wait();
    }
}

void MediaLibraryDialog::beginScan()
{
    if (m_scan && m_scan->isRunning())
        return;
    if (m_scan) {
        m_scan->deleteLater();
        m_scan = 0;
    }
    m_items.clear();
    m_list->clear();
    m_search->setEnabled(false);
    m_rescan->setEnabled(false);
    m_open->setEnabled(false);
    m_playAll->setEnabled(false);
    m_status->setText(QStringLiteral("Scanning configured %1 folders in the background...").arg(MediaLibrary::kindLabel(m_kind).toLower()));
    m_scan = new MediaLibraryScanThread(m_kind, this);
    connect(m_scan, SIGNAL(finished()), this, SLOT(scanFinished()));
    m_scan->start(QThread::LowPriority);
}

void MediaLibraryDialog::scanFinished()
{
    if (!m_scan)
        return;
    m_items = m_scan->results();
    m_search->setEnabled(true);
    m_rescan->setEnabled(true);
    m_playAll->setEnabled(!m_items.isEmpty() && m_kind != MediaPicture);
    rebuildList();
    m_status->setText(QStringLiteral("%1 media item(s) found. Scanning is on-demand and never runs during Kadia startup.").arg(m_items.size()));
    if (m_autoPlay && !m_items.isEmpty()) {
        m_autoPlay = false;
        playAll();
    }
}

void MediaLibraryDialog::rebuildList()
{
    const QString filter = m_search->text().trimmed();
    m_list->clear();
    for (int i = 0; i < m_items.size(); ++i) {
        const KadiaMediaItem &item = m_items.at(i);
        if (!filter.isEmpty() && !item.title.contains(filter, Qt::CaseInsensitive) &&
            !item.path.contains(filter, Qt::CaseInsensitive))
            continue;
        QListWidgetItem *row = new QListWidgetItem(
            QStringLiteral("%1    [%2]").arg(item.title, MediaLibrary::kindLabel(item.kind)), m_list);
        row->setData(Qt::UserRole, item.path);
        row->setToolTip(item.path);
    }
    selectionChanged();
}

void MediaLibraryDialog::applyFilter(const QString &text)
{
    Q_UNUSED(text);
    rebuildList();
}

void MediaLibraryDialog::selectionChanged()
{
    m_open->setEnabled(m_list->currentItem() != 0);
}

void MediaLibraryDialog::openSelected()
{
    QListWidgetItem *row = m_list->currentItem();
    if (!row)
        return;
    QString error;
    if (!MediaLibrary::openPath(row->data(Qt::UserRole).toString(), this, &error))
        QMessageBox::warning(this, QStringLiteral("Kadia Media"), error);
}

void MediaLibraryDialog::playAll()
{
    QString error;
    if (!MediaLibrary::playItems(m_items, this, &error))
        QMessageBox::warning(this, QStringLiteral("Kadia Media"), error);
}

MediaLibrarySettingsDialog::MediaLibrarySettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_tabs(new QTabWidget(this))
    , m_music(new QListWidget(this))
    , m_video(new QListWidget(this))
    , m_picture(new QListWidget(this))
    , m_recorded(new QListWidget(this))
{
    setWindowTitle(QStringLiteral("Kadia - Media Libraries"));
    resize(760, 500);
    m_tabs->addTab(m_music, QStringLiteral("Music"));
    m_tabs->addTab(m_video, QStringLiteral("Videos + Movies"));
    m_tabs->addTab(m_picture, QStringLiteral("Pictures"));
    m_tabs->addTab(m_recorded, QStringLiteral("Recorded TV"));
    populate(MediaMusic, m_music);
    populate(MediaVideo, m_video);
    populate(MediaPicture, m_picture);
    populate(MediaRecordedTV, m_recorded);

    QPushButton *add = new QPushButton(QStringLiteral("Add Folder..."), this);
    QPushButton *remove = new QPushButton(QStringLiteral("Remove"), this);
    QPushButton *defaults = new QPushButton(QStringLiteral("Restore Defaults"), this);
    QPushButton *save = new QPushButton(QStringLiteral("Save"), this);
    QPushButton *cancel = new QPushButton(QStringLiteral("Cancel"), this);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->addWidget(new QLabel(QStringLiteral("Choose the folders Kadia will index when a media library is opened. Nothing here is scanned during startup."), this));
    root->addWidget(m_tabs, 1);
    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addWidget(defaults);
    buttons->addStretch(1);
    buttons->addWidget(save);
    buttons->addWidget(cancel);
    root->addLayout(buttons);

    connect(add, SIGNAL(clicked()), this, SLOT(addFolder()));
    connect(remove, SIGNAL(clicked()), this, SLOT(removeFolder()));
    connect(defaults, SIGNAL(clicked()), this, SLOT(restoreDefaults()));
    connect(save, SIGNAL(clicked()), this, SLOT(saveAndAccept()));
    connect(cancel, SIGNAL(clicked()), this, SLOT(reject()));
}

void MediaLibrarySettingsDialog::populate(KadiaMediaKind kind, QListWidget *list)
{
    list->clear();
    const QStringList paths = MediaLibrary::folders(kind);
    for (int i = 0; i < paths.size(); ++i)
        list->addItem(paths.at(i));
}

KadiaMediaKind MediaLibrarySettingsDialog::currentKind() const
{
    switch (m_tabs->currentIndex()) {
    case 0: return MediaMusic;
    case 1: return MediaVideo;
    case 2: return MediaPicture;
    default: return MediaRecordedTV;
    }
}

QListWidget *MediaLibrarySettingsDialog::listForKind(KadiaMediaKind kind) const
{
    switch (kind) {
    case MediaMusic: return m_music;
    case MediaVideo: return m_video;
    case MediaPicture: return m_picture;
    default: return m_recorded;
    }
}

void MediaLibrarySettingsDialog::addFolder()
{
    QListWidget *list = listForKind(currentKind());
    const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("Add media folder"), QString(), QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
    if (folder.isEmpty())
        return;
    for (int i = 0; i < list->count(); ++i) {
        if (QDir::cleanPath(list->item(i)->text()).compare(QDir::cleanPath(folder), Qt::CaseInsensitive) == 0)
            return;
    }
    list->addItem(QDir::cleanPath(folder));
}

void MediaLibrarySettingsDialog::removeFolder()
{
    QListWidget *list = listForKind(currentKind());
    delete list->takeItem(list->currentRow());
}

void MediaLibrarySettingsDialog::restoreDefaults()
{
    QListWidget *list = listForKind(currentKind());
    list->clear();
    const QStringList defaults = MediaLibrary::defaultFolders(currentKind());
    for (int i = 0; i < defaults.size(); ++i)
        list->addItem(defaults.at(i));
}

void MediaLibrarySettingsDialog::saveAndAccept()
{
    const KadiaMediaKind kinds[] = {MediaMusic, MediaVideo, MediaPicture, MediaRecordedTV};
    for (int k = 0; k < 4; ++k) {
        QListWidget *list = listForKind(kinds[k]);
        QStringList paths;
        for (int i = 0; i < list->count(); ++i)
            paths << list->item(i)->text();
        MediaLibrary::setFolders(kinds[k], paths);
    }
    accept();
}
