#pragma once

#include <QDialog>
#include <QThread>

#include "input_manager.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

class WinDSProInstallWorker : public QThread
{
    Q_OBJECT
public:
    explicit WinDSProInstallWorker(QObject *parent = 0);
    ~WinDSProInstallWorker();
    void reportDownloadProgress(qint64 received, qint64 total);

signals:
    void stageChanged(const QString &stage);
    void downloadProgress(qint64 received, qint64 total);
    void verifying();
    void installing();
    void completed();
    void failed(const QString &message);

protected:
    void run() Q_DECL_OVERRIDE;
};

class WinDSProBootstrapDialog : public QDialog
{
    Q_OBJECT
public:
    explicit WinDSProBootstrapDialog(QWidget *parent = 0);
    ~WinDSProBootstrapDialog();

private slots:
    void startInstall();
    void skipInstall();
    void onStageChanged(const QString &stage);
    void onDownloadProgress(qint64 received, qint64 total);
    void onVerifying();
    void onInstalling();
    void onCompleted();
    void onFailed(const QString &message);
    void animateInstallProgress();
    void pollController();

private:
    void stopWorker();
    void updateButtonFocus();

    QLabel *m_title;
    QLabel *m_status;
    QLabel *m_detail;
    QProgressBar *m_progress;
    QPushButton *m_install;
    QPushButton *m_skip;
    QPushButton *m_retry;
    QPushButton *m_close;
    QTimer *m_installTimer;
    QTimer *m_inputTimer;
    WinDSProInstallWorker *m_worker;
    InputManager m_input;
    int m_installProgress;
    int m_focusIndex;
};

namespace WinDSProBootstrap
{
    bool isInstalled(QString *location = 0);
    bool wasOffered();
    void markOffered();
    void offerOnce(QWidget *parent);
}
