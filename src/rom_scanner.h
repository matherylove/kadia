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
    QString classification(const QString &path);
    QString internalTitle(const QString &path);
    QStringList systems();
    QStringList pathsForClassification(const QString &classification);
}

class RomScanner : public QThread
{
    Q_OBJECT
public:
    explicit RomScanner(QObject *parent = 0);
    ~RomScanner();
    void requestStop();

signals:
    void romDiscovered(const QString &path, const QString &hint);
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
