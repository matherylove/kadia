#include "klite_bootstrap.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDesktopWidget>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <wininet.h>
#  include <shellapi.h>
#endif

namespace {

static QString humanBytes(qint64 bytes)
{
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    return QString::number(mb, 'f', 1) + QStringLiteral(" MB");
}

static QString tempInstallerPath(const QString &fileName)
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (dir.isEmpty())
        dir = QDir::tempPath();
    return QDir(dir).filePath(fileName);
}

static bool sha256Matches(const QString &fileName, const QByteArray &expected, QString *error)
{
    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Unable to open the downloaded installer for verification.");
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!f.atEnd()) {
        const QByteArray block = f.read(1024 * 1024);
        if (block.isEmpty() && f.error() != QFile::NoError) {
            if (error) *error = QStringLiteral("Unable to read the downloaded installer.");
            return false;
        }
        hash.addData(block);
    }

    const QByteArray actual = hash.result().toHex().toLower();
    if (actual != expected.toLower()) {
        if (error) {
            *error = QStringLiteral("The downloaded K-Lite installer failed SHA-256 verification.\n\nExpected: %1\nActual: %2")
                    .arg(QString::fromLatin1(expected), QString::fromLatin1(actual));
        }
        return false;
    }
    return true;
}

#ifdef Q_OS_WIN
static QString winInetErrorMessage(DWORD code)
{
    wchar_t *buffer = 0;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD len = FormatMessageW(flags, 0, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, 0);
    QString text;
    if (len && buffer)
        text = QString::fromWCharArray(buffer).trimmed();
    if (buffer)
        LocalFree(buffer);
    if (text.isEmpty())
        text = QStringLiteral("Windows error %1").arg(code);
    return text;
}

static bool downloadWithWinInet(const QString &url, const QString &destination,
                                KLiteInstallWorker *worker, QString *error)
{
    HINTERNET internet = InternetOpenW(L"Mathery Kadia!/1.0",
                                       INTERNET_OPEN_TYPE_PRECONFIG, 0, 0, 0);
    if (!internet) {
        if (error) *error = winInetErrorMessage(GetLastError());
        return false;
    }

    const DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                        INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_KEEP_CONNECTION |
                        INTERNET_FLAG_SECURE;

    HINTERNET request = InternetOpenUrlW(internet,
                                         reinterpret_cast<LPCWSTR>(url.utf16()),
                                         0, 0, flags, 0);
    if (!request) {
        const DWORD code = GetLastError();
        InternetCloseHandle(internet);
        if (error) {
            *error = QStringLiteral("Unable to download K-Lite Codec Pack from Codec Guide.\n%1")
                     .arg(winInetErrorMessage(code));
        }
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (HttpQueryInfoW(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                       &status, &statusSize, 0) && (status < 200 || status >= 300)) {
        InternetCloseHandle(request);
        InternetCloseHandle(internet);
        if (error) *error = QStringLiteral("Codec Guide returned HTTP status %1.").arg(status);
        return false;
    }

    qint64 total = 0;
    wchar_t lengthBuffer[64] = {0};
    DWORD lengthSize = sizeof(lengthBuffer);
    if (HttpQueryInfoW(request, HTTP_QUERY_CONTENT_LENGTH, lengthBuffer, &lengthSize, 0))
        total = QString::fromWCharArray(lengthBuffer).trimmed().toLongLong();

    QFile out(destination);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        InternetCloseHandle(request);
        InternetCloseHandle(internet);
        if (error) *error = QStringLiteral("Unable to create the temporary K-Lite installer file.");
        return false;
    }

    QByteArray buffer(128 * 1024, '\0');
    qint64 received = 0;
    bool ok = true;
    for (;;) {
        DWORD bytesRead = 0;
        if (!InternetReadFile(request, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead)) {
            if (error) *error = winInetErrorMessage(GetLastError());
            ok = false;
            break;
        }
        if (bytesRead == 0)
            break;
        if (out.write(buffer.constData(), static_cast<qint64>(bytesRead)) != static_cast<qint64>(bytesRead)) {
            if (error) *error = QStringLiteral("Unable to write the K-Lite installer to the temporary folder.");
            ok = false;
            break;
        }
        received += bytesRead;
        worker->reportDownloadProgress(received, total);
    }

    out.close();
    InternetCloseHandle(request);
    InternetCloseHandle(internet);

    if (!ok)
        QFile::remove(destination);
    return ok;
}

static bool runInstallerAndWait(const QString &installerPath, QString *error)
{
    SHELLEXECUTEINFOW sei;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;

    OSVERSIONINFOEXW os;
    ZeroMemory(&os, sizeof(os));
    os.dwOSVersionInfoSize = sizeof(os);
    GetVersionExW(reinterpret_cast<OSVERSIONINFOW *>(&os));

    const bool vistaOrNewer = os.dwMajorVersion >= 6;
    sei.lpVerb = vistaOrNewer ? L"runas" : L"open";
    sei.lpFile = reinterpret_cast<LPCWSTR>(installerPath.utf16());
    const QString params = QStringLiteral("/verysilent /norestart");
    sei.lpParameters = reinterpret_cast<LPCWSTR>(params.utf16());
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei)) {
        const DWORD code = GetLastError();
        if (error) {
            if (code == ERROR_CANCELLED)
                *error = QStringLiteral("Administrator authorization was cancelled. K-Lite Codec Pack Full is required to run Mathery Kadia!.");
            else
                *error = QStringLiteral("Unable to start the K-Lite installer.\n%1").arg(winInetErrorMessage(code));
        }
        return false;
    }

    if (!sei.hProcess) {
        if (error) *error = QStringLiteral("The K-Lite installer process could not be monitored.");
        return false;
    }

    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(sei.hProcess, &exitCode);
    CloseHandle(sei.hProcess);

    if (exitCode != 0) {
        if (error) *error = QStringLiteral("K-Lite Codec Pack setup exited with code %1.").arg(exitCode);
        return false;
    }
    return true;
}
#endif

} // namespace

KLiteInstallWorker::KLiteInstallWorker(const KLitePackageInfo &package, QObject *parent)
    : QThread(parent), m_package(package)
{
}

KLiteInstallWorker::~KLiteInstallWorker()
{
    wait();
}

void KLiteInstallWorker::reportDownloadProgress(qint64 received, qint64 total)
{
    emit downloadProgress(received, total);
}

void KLiteInstallWorker::run()
{
#ifndef Q_OS_WIN
    emit failed(QStringLiteral("Automatic K-Lite installation is only available on Windows."));
    return;
#else
    const QString installerPath = tempInstallerPath(m_package.fileName);
    QFile::remove(installerPath);

    emit stageChanged(QStringLiteral("Downloading K-Lite Codec Pack Full %1...").arg(m_package.version));

    QString error;
    if (!downloadWithWinInet(m_package.url, installerPath, this, &error)) {
        QFile::remove(installerPath);
        const QString mirror = QString(m_package.url).replace(QStringLiteral("files2.codecguide.com"),
                                                               QStringLiteral("files3.codecguide.com"));
        emit stageChanged(QStringLiteral("Primary Codec Guide server unavailable; trying mirror..."));
        if (mirror == m_package.url || !downloadWithWinInet(mirror, installerPath, this, &error)) {
            emit failed(error);
            return;
        }
    }

    emit verifying();
    if (!sha256Matches(installerPath, m_package.sha256, &error)) {
        QFile::remove(installerPath);
        emit failed(error);
        return;
    }

    emit installing();
    if (!runInstallerAndWait(installerPath, &error)) {
        QFile::remove(installerPath);
        emit failed(error);
        return;
    }

    QFile::remove(installerPath);

    QString installedName;
    if (!KLiteBootstrap::isFullInstalled(&installedName)) {
        emit failed(QStringLiteral("K-Lite setup completed, but the Full edition was not detected in Windows afterwards."));
        return;
    }

    emit completed();
#endif
}

KLiteBootstrapDialog::KLiteBootstrapDialog(const KLitePackageInfo &package, QWidget *parent)
    : QDialog(parent)
    , m_package(package)
    , m_title(new QLabel(this))
    , m_status(new QLabel(this))
    , m_detail(new QLabel(this))
    , m_progress(new QProgressBar(this))
    , m_retry(new QPushButton(QStringLiteral("Retry"), this))
    , m_exit(new QPushButton(QStringLiteral("Exit"), this))
    , m_installTimer(new QTimer(this))
    , m_inputTimer(new QTimer(this))
    , m_worker(0)
    , m_input(this)
    , m_installProgress(0)
{
    setWindowTitle(QStringLiteral("Mathery Kadia! - Required media components"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setModal(true);
    setFixedSize(560, 230);
    setAttribute(Qt::WA_TranslucentBackground, false);

    m_title->setText(QStringLiteral("MATHERY  Kadia!"));
    QFont titleFont = m_title->font();
    titleFont.setPixelSize(24);
    titleFont.setWeight(QFont::Light);
    m_title->setFont(titleFont);

    m_status->setText(QStringLiteral("K-Lite Codec Pack Full is required"));
    QFont statusFont = m_status->font();
    statusFont.setPixelSize(17);
    m_status->setFont(statusFont);

    m_detail->setText(QStringLiteral("The required codec pack was not found. Kadia will download and install the compatible Full edition automatically."));
    m_detail->setWordWrap(true);

    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    m_progress->setFormat(QStringLiteral("%p%"));

    m_retry->hide();
    m_exit->hide();

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_retry);
    buttons->addWidget(m_exit);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 22, 28, 22);
    layout->setSpacing(10);
    layout->addWidget(m_title);
    layout->addWidget(m_status);
    layout->addWidget(m_detail);
    layout->addSpacing(5);
    layout->addWidget(m_progress);
    layout->addLayout(buttons);

    setStyleSheet(QStringLiteral(
        "QDialog { background:#070b12; color:#fff8e7; border:1px solid rgba(255,248,231,38); }"
        "QLabel { color:#fff8e7; background:transparent; }"
        "QProgressBar { border:1px solid rgba(255,248,231,55); border-radius:5px; background:#10151f; color:#fff8e7; text-align:center; min-height:18px; }"
        "QProgressBar::chunk { border-radius:4px; background:qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #746f65, stop:0.55 #b8b09e, stop:1 #fff0c8); }"
        "QPushButton { color:#fff8e7; background:#151b26; border:1px solid rgba(255,248,231,60); border-radius:4px; padding:6px 18px; }"
        "QPushButton:hover { background:#202838; }"));

    connect(m_retry, SIGNAL(clicked()), this, SLOT(startInstall()));
    connect(m_exit, SIGNAL(clicked()), this, SLOT(reject()));
    connect(m_installTimer, SIGNAL(timeout()), this, SLOT(animateInstallProgress()));
    m_installTimer->setInterval(280);
    m_input.initialize();
    connect(m_inputTimer, SIGNAL(timeout()), this, SLOT(pollController()));
    m_inputTimer->start(16);

    const QRect screen = QApplication::desktop()->screenGeometry(QApplication::desktop()->primaryScreen());
    move(screen.center() - rect().center());

    QTimer::singleShot(100, this, SLOT(startInstall()));
}

KLiteBootstrapDialog::~KLiteBootstrapDialog()
{
    stopWorker();
}

void KLiteBootstrapDialog::stopWorker()
{
    if (!m_worker)
        return;
    if (m_worker->isRunning())
        m_worker->wait();
    delete m_worker;
    m_worker = 0;
}

void KLiteBootstrapDialog::startInstall()
{
    stopWorker();
    m_retry->hide();
    m_exit->hide();
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_installProgress = 0;
    m_status->setText(QStringLiteral("Preparing required media components"));
    m_detail->setText(QStringLiteral("K-Lite Codec Pack Full %1 will be downloaded from Codec Guide and verified before installation.").arg(m_package.version));

    m_worker = new KLiteInstallWorker(m_package, this);
    connect(m_worker, SIGNAL(stageChanged(QString)), this, SLOT(onStageChanged(QString)));
    connect(m_worker, SIGNAL(downloadProgress(qint64,qint64)), this, SLOT(onDownloadProgress(qint64,qint64)));
    connect(m_worker, SIGNAL(verifying()), this, SLOT(onVerifying()));
    connect(m_worker, SIGNAL(installing()), this, SLOT(onInstalling()));
    connect(m_worker, SIGNAL(completed()), this, SLOT(onCompleted()));
    connect(m_worker, SIGNAL(failed(QString)), this, SLOT(onFailed(QString)));
    m_worker->start();
}

void KLiteBootstrapDialog::onStageChanged(const QString &stage)
{
    m_status->setText(stage);
}

void KLiteBootstrapDialog::onDownloadProgress(qint64 received, qint64 total)
{
    int percent = 0;
    if (total > 0)
        percent = static_cast<int>((received * 70) / total);
    else
        percent = qMin(69, m_progress->value() + 1);
    m_progress->setValue(qBound(0, percent, 70));

    if (total > 0)
        m_detail->setText(QStringLiteral("Downloading from Codec Guide: %1 / %2").arg(humanBytes(received), humanBytes(total)));
    else
        m_detail->setText(QStringLiteral("Downloading from Codec Guide: %1").arg(humanBytes(received)));
}

void KLiteBootstrapDialog::onVerifying()
{
    m_progress->setValue(72);
    m_status->setText(QStringLiteral("Verifying download"));
    m_detail->setText(QStringLiteral("Checking the official SHA-256 checksum before setup is allowed to run."));
}

void KLiteBootstrapDialog::onInstalling()
{
    m_installProgress = 76;
    m_progress->setValue(m_installProgress);
    m_status->setText(QStringLiteral("Installing K-Lite Codec Pack Full"));
    m_detail->setText(QStringLiteral("Setup is running silently. Windows may request administrator authorization on Vista or newer."));
    m_installTimer->start();
}

void KLiteBootstrapDialog::animateInstallProgress()
{
    if (m_installProgress < 96) {
        const int step = m_installProgress < 86 ? 1 : ((m_installProgress % 3) == 0 ? 1 : 0);
        m_installProgress = qMin(96, m_installProgress + step);
        m_progress->setValue(m_installProgress);
    }
}

void KLiteBootstrapDialog::onCompleted()
{
    m_installTimer->stop();
    m_progress->setValue(100);
    m_status->setText(QStringLiteral("Media components ready"));
    m_detail->setText(QStringLiteral("K-Lite Codec Pack Full is installed. Starting Mathery Kadia!..."));
    QTimer::singleShot(450, this, SLOT(accept()));
}

void KLiteBootstrapDialog::onFailed(const QString &message)
{
    m_installTimer->stop();
    m_status->setText(QStringLiteral("Unable to install required media components"));
    m_detail->setText(message);
    m_progress->setValue(0);
    m_retry->show();
    m_exit->show();
}

void KLiteBootstrapDialog::pollController()
{
    const InputManager::Action action = m_input.poll();
    if (action == InputManager::None)
        return;

    QList<QPushButton *> buttons;
    if (m_retry->isVisible()) buttons << m_retry;
    if (m_exit->isVisible()) buttons << m_exit;
    if (buttons.isEmpty())
        return;

    int index = buttons.indexOf(qobject_cast<QPushButton *>(focusWidget()));
    if (index < 0) index = 0;
    if (action == InputManager::Left || action == InputManager::Up)
        index = (index - 1 + buttons.size()) % buttons.size();
    else if (action == InputManager::Right || action == InputManager::Down)
        index = (index + 1) % buttons.size();
    else if (action == InputManager::Accept) {
        buttons[index]->click();
        return;
    } else if (action == InputManager::Back) {
        m_exit->click();
        return;
    }
    buttons[index]->setFocus();
}

namespace KLiteBootstrap {

bool isFullInstalled(QString *displayName)
{
#ifdef Q_OS_WIN
    const QStringList roots = QStringList()
        << QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\KLiteCodecPack_is1")
        << QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\KLiteCodecPack_is1");

    for (int i = 0; i < roots.size(); ++i) {
        QSettings reg(roots[i], QSettings::NativeFormat);
        const QString name = reg.value(QStringLiteral("DisplayName")).toString();
        if (!name.isEmpty() && name.contains(QStringLiteral("K-Lite Codec Pack"), Qt::CaseInsensitive) &&
            name.contains(QStringLiteral("Full"), Qt::CaseInsensitive)) {
            if (displayName) *displayName = name;
            return true;
        }
    }
#else
    Q_UNUSED(displayName);
#endif
    return false;
}

KLitePackageInfo packageForCurrentWindows()
{
    KLitePackageInfo package;
#ifdef Q_OS_WIN
    OSVERSIONINFOEXW os;
    ZeroMemory(&os, sizeof(os));
    os.dwOSVersionInfoSize = sizeof(os);
    GetVersionExW(reinterpret_cast<OSVERSIONINFOW *>(&os));

    if (os.dwMajorVersion <= 5) {
        package.version = QStringLiteral("13.8.5");
        package.fileName = QStringLiteral("K-Lite_Codec_Pack_1385_Full.exe");
        package.url = QStringLiteral("https://files2.codecguide.com/K-Lite_Codec_Pack_1385_Full.exe");
        package.sha256 = QByteArrayLiteral("13f31319251a808d0489d54ff69e30f2d4672bd8e668d071ca47ab3433d9d6f1");
    } else if (os.dwMajorVersion == 6 && os.dwMinorVersion == 0) {
        package.version = QStringLiteral("16.7.6");
        package.fileName = QStringLiteral("K-Lite_Codec_Pack_1676_Full.exe");
        package.url = QStringLiteral("https://files2.codecguide.com/K-Lite_Codec_Pack_1676_Full.exe");
        package.sha256 = QByteArrayLiteral("450904524d22d95a283360edeca1ec19b19ab69cf2f580881f86583f41f2a145");
    } else {
        package.version = QStringLiteral("19.9.0");
        package.fileName = QStringLiteral("K-Lite_Codec_Pack_1990_Full.exe");
        package.url = QStringLiteral("https://files2.codecguide.com/K-Lite_Codec_Pack_1990_Full.exe");
        package.sha256 = QByteArrayLiteral("eb54d9df27c10c091e702e60db40c2c3d40e501e01247068cfb874fd35c46904");
    }
#endif
    return package;
}

bool ensureInstalled(QWidget *parent)
{
    QString installedName;
    if (isFullInstalled(&installedName))
        return true;

    const KLitePackageInfo package = packageForCurrentWindows();
    if (package.url.isEmpty())
        return false;

    KLiteBootstrapDialog dialog(package, parent);
    return dialog.exec() == QDialog::Accepted && isFullInstalled(0);
}

} // namespace KLiteBootstrap
