#pragma once

#include <QByteArray>
#include <QDialog>
#include <QString>
#include <QThread>

#include "input_manager.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

struct KLitePackageInfo
{
    QString version;
    QString fileName;
    QString url;
    QByteArray sha256;
};

class KLiteInstallWorker : public QThread
{
    Q_OBJECT
public:
    explicit KLiteInstallWorker(const KLitePackageInfo &package, QObject *parent = 0);
    ~KLiteInstallWorker();
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

private:
    KLitePackageInfo m_package;
};

class KLiteBootstrapDialog : public QDialog
{
    Q_OBJECT
public:
    explicit KLiteBootstrapDialog(const KLitePackageInfo &package, QWidget *parent = 0);
    ~KLiteBootstrapDialog();

private slots:
    void startInstall();
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

    KLitePackageInfo m_package;
    QLabel *m_title;
    QLabel *m_status;
    QLabel *m_detail;
    QProgressBar *m_progress;
    QPushButton *m_retry;
    QPushButton *m_exit;
    QTimer *m_installTimer;
    QTimer *m_inputTimer;
    KLiteInstallWorker *m_worker;
    InputManager m_input;
    int m_installProgress;
};

namespace KLiteBootstrap
{
    bool isFullInstalled(QString *displayName = 0);
    KLitePackageInfo packageForCurrentWindows();
    bool ensureInstalled(QWidget *parent = 0);
}
