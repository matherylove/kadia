#pragma once

#include <QDialog>
#include <QThread>
#include <QVector>
#include "media_library.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTabWidget;

class MediaLibraryScanThread : public QThread
{
    Q_OBJECT
public:
    explicit MediaLibraryScanThread(KadiaMediaKind kind, QObject *parent = 0);
    QVector<KadiaMediaItem> results() const;
    void cancel();
protected:
    void run() Q_DECL_OVERRIDE;
private:
    KadiaMediaKind m_kind;
    QVector<KadiaMediaItem> m_results;
    QAtomicInteger<int> m_cancel;
};

class MediaLibraryDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MediaLibraryDialog(KadiaMediaKind kind, QWidget *parent = 0, bool autoPlay = false);
    ~MediaLibraryDialog();

private slots:
    void beginScan();
    void scanFinished();
    void applyFilter(const QString &text);
    void openSelected();
    void playAll();
    void selectionChanged();

private:
    void rebuildList();
    KadiaMediaKind m_kind;
    MediaLibraryScanThread *m_scan;
    QVector<KadiaMediaItem> m_items;
    QLabel *m_status;
    QLineEdit *m_search;
    QListWidget *m_list;
    QPushButton *m_open;
    QPushButton *m_playAll;
    QPushButton *m_rescan;
    bool m_autoPlay;
};

class MediaLibrarySettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MediaLibrarySettingsDialog(QWidget *parent = 0);

private slots:
    void addFolder();
    void removeFolder();
    void restoreDefaults();
    void saveAndAccept();

private:
    QListWidget *listForKind(KadiaMediaKind kind) const;
    KadiaMediaKind currentKind() const;
    void populate(KadiaMediaKind kind, QListWidget *list);

    QTabWidget *m_tabs;
    QListWidget *m_music;
    QListWidget *m_video;
    QListWidget *m_picture;
    QListWidget *m_recorded;
};
