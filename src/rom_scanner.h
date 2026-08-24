#pragma once

#include <QDialog>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QThread>

#include "input_manager.h"

class QLabel;
class QListWidget;
class QPushButton;
class QTimer;

namespace RomCatalog
{
    bool isKnown(const QString &path);
    void saveClassification(const QString &path, const QString &system);
    void saveDetectedRom(const QString &path, const QString &system, const QString &title,
                         const QString &internalId, const QString &format, int confidence);
    void removeEntry(const QString &path);
    QString classification(const QString &path);
    QString internalTitle(const QString &path);
    QString displayTitle(const QString &path);
    QString internalId(const QString &path);
    QString format(const QString &path);
    QString description(const QString &path);
    QString coverArtPath(const QString &path);
    bool hasScreenScraperMetadata(const QString &path);
    void saveScreenScraperMetadata(const QString &path, const QString &title,
                                   const QString &description, const QString &coverPath,
                                   const QString &source);
    bool isAutomaticDetection(const QString &path);
    QStringList systems();
    QStringList pathsForClassification(const QString &classification);
    QStringList recognizedPaths();
}

class RomScanner : public QThread
{
    Q_OBJECT
public:
    explicit RomScanner(QObject *parent = 0);
    ~RomScanner();
    void requestStop();

signals:
    // Emitted only when the internal structure is ROM-like but Kadia cannot
    // determine the console reliably.  Only these files require a dialog.
    void romDiscovered(const QString &path, const QString &hint);
    // Reliable header detections are catalogued automatically and sent here so
    // the GUI can refresh immediately without interrupting the user.
    void romRecognized(const QString &path, const QString &system, const QString &title);
    void scanStatus(const QString &text);
    void scanFinished();

protected:
    void run() Q_DECL_OVERRIDE;

private:
    volatile bool m_stop;
};

class RomClassificationDialog : public QDialog
{
    Q_OBJECT
public:
    RomClassificationDialog(const QString &path, const QString &hint, QWidget *parent = 0);
    QString selectedSystem() const;

private slots:
    void confirmSelection();
    void deferSelection();
    void pollController();

private:
    QString m_path;
    QString m_selectedSystem;
    QLabel *m_pathLabel;
    QLabel *m_hintLabel;
    QListWidget *m_systems;
    QPushButton *m_confirm;
    QPushButton *m_later;
    InputManager m_input;
    QTimer *m_inputTimer;
};
