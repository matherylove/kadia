#include "windspro_bootstrap.h"

#include <QApplication>
#include <QDesktopWidget>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QProgressBar>
#include <QPushButton>
#include <QRegExp>
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

const char kMediaFirePage[] = "https://www.mediafire.com/file/z4dfapbzynlynli/WinDS_PRO_2026.08.22.exe/file";

static QString humanBytes(qint64 bytes)
{
    const double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    if (gb >= 0.90)
        return QString::number(gb, 'f', 2) + QStringLiteral(" GB");
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    return QString::number(mb, 'f', 1) + QStringLiteral(" MB");
}

static QString tempInstallerPath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (dir.isEmpty()) dir = QDir::tempPath();
    return QDir(dir).filePath(QStringLiteral("WinDS_PRO_2026.08.22.exe"));
}

static QString bootstrapDialogStyle()
{
    return QStringLiteral(
        "QDialog { background:transparent; }"
        "QFrame#glassPanel { background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(24,33,50,236), stop:0.50 rgba(10,16,28,228), stop:1 rgba(6,10,18,236)); border:1px solid rgba(255,248,231,54); border-radius:18px; }"
        "QFrame#accentGlow { background:qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 rgba(255,240,200,10), stop:0.18 rgba(255,240,200,115), stop:0.52 rgba(166,194,255,75), stop:1 rgba(166,194,255,0)); border:none; border-radius:3px; }"
        "QLabel { background:transparent; color:#fff8e7; }"
        "QLabel#dialogTitle { color:rgba(255,248,231,0.92); letter-spacing:2px; }"
        "QLabel#dialogStatus { color:#fff8e7; }"
        "QLabel#dialogDetail { color:rgba(255,248,231,0.72); }"
        "QProgressBar { border:1px solid rgba(255,248,231,52); border-radius:8px; padding:1px; background:rgba(9,14,23,180); color:#fff8e7; text-align:center; min-height:20px; }"
        "QProgressBar::chunk { border-radius:6px; background:qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 rgba(117,131,168,230), stop:0.45 rgba(184,176,158,235), stop:1 rgba(255,240,200,250)); }"
        "QPushButton { color:#fff8e7; background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(40,48,69,220), stop:1 rgba(18,24,37,220)); border:1px solid rgba(255,248,231,68); border-radius:12px; padding:8px 22px; min-width:104px; }"
        "QPushButton:hover, QPushButton:focus { background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(71,82,112,235), stop:1 rgba(26,34,54,230)); border:1px solid rgba(255,240,200,160); }"
        "QPushButton:pressed { background:rgba(22,28,44,235); }" );
}

#ifdef Q_OS_WIN
static QString winError(DWORD code)
{
    wchar_t *buffer = 0;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD len = FormatMessageW(flags, 0, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, 0);
    QString text;
    if (len && buffer) text = QString::fromWCharArray(buffer).trimmed();
    if (buffer) LocalFree(buffer);
    if (text.isEmpty()) text = QStringLiteral("Windows error %1").arg(code);
    return text;
}

static bool openUrl(const QString &url, HINTERNET *internetOut, HINTERNET *requestOut, QString *error)
{
    *internetOut = 0; *requestOut = 0;
    HINTERNET internet = InternetOpenW(L"Mathery Kadia!/1.0", INTERNET_OPEN_TYPE_PRECONFIG, 0, 0, 0);
    if (!internet) { if (error) *error = winError(GetLastError()); return false; }
    const DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE |
                        INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_SECURE;
    HINTERNET request = InternetOpenUrlW(internet, reinterpret_cast<LPCWSTR>(url.utf16()), 0, 0, flags, 0);
    if (!request) {
        const DWORD e = GetLastError(); InternetCloseHandle(internet);
        if (error) *error = winError(e); return false;
    }
    DWORD status = 0, statusSize = sizeof(status);
    if (HttpQueryInfoW(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &statusSize, 0) &&
        (status < 200 || status >= 300)) {
        InternetCloseHandle(request); InternetCloseHandle(internet);
        if (error) *error = QStringLiteral("HTTP status %1").arg(status); return false;
    }
    *internetOut = internet; *requestOut = request; return true;
}

static bool fetchText(const QString &url, QByteArray *data, QString *error)
{
    HINTERNET internet = 0, request = 0;
    if (!openUrl(url, &internet, &request, error)) return false;
    QByteArray result;
    QByteArray buffer(64 * 1024, '\0');
    for (;;) {
        DWORD got = 0;
        if (!InternetReadFile(request, buffer.data(), static_cast<DWORD>(buffer.size()), &got)) {
            if (error) *error = winError(GetLastError());
            InternetCloseHandle(request); InternetCloseHandle(internet); return false;
        }
        if (!got) break;
        result.append(buffer.constData(), static_cast<int>(got));
        if (result.size() > 8 * 1024 * 1024) break;
    }
    InternetCloseHandle(request); InternetCloseHandle(internet);
    *data = result; return true;
}

static QString extractMediaFireDirectUrl(const QByteArray &html)
{
    QString text = QString::fromUtf8(html);
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    QRegExp rx(QStringLiteral("https://download[^\\\"'<> ]*\\.mediafire\\.com/[^\\\"'<> ]*WinDS_PRO_2026\\.08\\.22\\.exe"), Qt::CaseInsensitive);
    if (rx.indexIn(text) >= 0)
        return rx.cap(0);
    QRegExp generic(QStringLiteral("https://download[^\\\"'<> ]*mediafire\\.com/[^\\\"'<> ]+"), Qt::CaseInsensitive);
    if (generic.indexIn(text) >= 0)
        return generic.cap(0);
    return QString();
}

static bool downloadFile(const QString &url, const QString &dest, WinDSProInstallWorker *worker, QString *error)
{
    HINTERNET internet = 0, request = 0;
    if (!openUrl(url, &internet, &request, error)) return false;

    qint64 total = 0;
    wchar_t lengthBuffer[64] = {0};
    DWORD lengthSize = sizeof(lengthBuffer);
    if (HttpQueryInfoW(request, HTTP_QUERY_CONTENT_LENGTH, lengthBuffer, &lengthSize, 0))
        total = QString::fromWCharArray(lengthBuffer).trimmed().toLongLong();

    QFile out(dest);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        InternetCloseHandle(request); InternetCloseHandle(internet);
        if (error) *error = QStringLiteral("Unable to create the temporary WinDS PRO installer.");
        return false;
    }

    QByteArray buffer(256 * 1024, '\0');
    qint64 received = 0;
    bool ok = true;
    for (;;) {
        DWORD got = 0;
        if (!InternetReadFile(request, buffer.data(), static_cast<DWORD>(buffer.size()), &got)) {
            if (error) *error = winError(GetLastError()); ok = false; break;
        }
        if (!got) break;
        if (out.write(buffer.constData(), static_cast<qint64>(got)) != static_cast<qint64>(got)) {
            if (error) *error = QStringLiteral("Unable to write the WinDS PRO installer to the temporary folder.");
            ok = false; break;
        }
        received += got;
        worker->reportDownloadProgress(received, total);
    }
    out.close(); InternetCloseHandle(request); InternetCloseHandle(internet);
    if (!ok) QFile::remove(dest);
    return ok;
}

static bool validateInstaller(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = QStringLiteral("Unable to open downloaded WinDS PRO installer."); return false; }
    const QByteArray mz = f.read(2);
    const qint64 size = f.size();
    f.close();
    if (mz != QByteArray("MZ", 2)) { if (error) *error = QStringLiteral("The downloaded file is not a Windows executable."); return false; }
    if (size < 500LL * 1024LL * 1024LL) { if (error) *error = QStringLiteral("The downloaded WinDS PRO file is unexpectedly small (%1).").arg(humanBytes(size)); return false; }
    return true;
}

static bool runInstaller(const QString &path, QString *error)
{
    SHELLEXECUTEINFOW sei; ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei); sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    OSVERSIONINFOEXW os; ZeroMemory(&os, sizeof(os)); os.dwOSVersionInfoSize = sizeof(os);
    GetVersionExW(reinterpret_cast<OSVERSIONINFOW *>(&os));
    sei.lpVerb = os.dwMajorVersion >= 6 ? L"runas" : L"open";
    sei.lpFile = reinterpret_cast<LPCWSTR>(path.utf16());
    const QString args = QStringLiteral("/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-");
    sei.lpParameters = reinterpret_cast<LPCWSTR>(args.utf16());
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) { if (error) *error = winError(GetLastError()); return false; }
    if (!sei.hProcess) { if (error) *error = QStringLiteral("WinDS PRO installer process could not be monitored."); return false; }
    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD exitCode = 1; GetExitCodeProcess(sei.hProcess, &exitCode); CloseHandle(sei.hProcess);
    if (exitCode != 0) { if (error) *error = QStringLiteral("WinDS PRO setup exited with code %1.").arg(exitCode); return false; }
    return true;
}
#endif

static QStringList likelyInstallPaths()
{
    QStringList paths;

    // WinDS PRO installs its shared emulator package under the Windows Public
    // Documents tree.  Use %PUBLIC% instead of assuming the system drive is C:.
    QString publicRoot = QString::fromLocal8Bit(qgetenv("PUBLIC"));
    if (publicRoot.isEmpty())
        publicRoot = QStringLiteral("C:/Users/Public");
    paths << QDir(publicRoot).filePath(QStringLiteral("Documents/WinDS PRO"));

    // Keep older/alternate locations as fallbacks for existing installations.
    paths << QStringLiteral("C:/ProgramData/WinDS PRO")
          << QStringLiteral("C:/ProgramData/winds pro");
    const QString pf = QString::fromLocal8Bit(qgetenv("ProgramFiles"));
    const QString pfx = QString::fromLocal8Bit(qgetenv("ProgramFiles(x86)"));
    if (!pf.isEmpty()) paths << QDir(pf).filePath(QStringLiteral("WinDS PRO"));
    if (!pfx.isEmpty()) paths << QDir(pfx).filePath(QStringLiteral("WinDS PRO"));
    return paths;
}

static bool looksLikeWinDSProInstall(const QString &path)
{
    QDir d(path);
    if (!d.exists())
        return false;

    // Some WinDS PRO releases do not place windspro.exe/windsprox.exe in the
    // package root.  The canonical %PUBLIC%\Documents\WinDS PRO directory
    // itself is therefore authoritative once it contains installed content.
    QString publicRoot = QString::fromLocal8Bit(qgetenv("PUBLIC"));
    if (publicRoot.isEmpty())
        publicRoot = QStringLiteral("C:/Users/Public");
    const QString publicInstall = QDir::cleanPath(
        QDir(publicRoot).filePath(QStringLiteral("Documents/WinDS PRO")));
    if (QDir::cleanPath(d.absolutePath()).compare(publicInstall, Qt::CaseInsensitive) == 0) {
        const QStringList entries = d.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        return !entries.isEmpty();
    }

    return QFileInfo(d.filePath(QStringLiteral("windspro.exe"))).exists() ||
           QFileInfo(d.filePath(QStringLiteral("windsprox.exe"))).exists();
}

static QString styleSheet()
{
    return QStringLiteral(
        "QDialog { background:#070b12; color:#fff8e7; border:1px solid rgba(255,248,231,42); }"
        "QLabel { color:#fff8e7; background:transparent; }"
        "QProgressBar { border:1px solid rgba(255,248,231,55); border-radius:5px; background:#10151f; color:#fff8e7; text-align:center; min-height:18px; }"
        "QProgressBar::chunk { border-radius:4px; background:qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #746f65, stop:0.55 #b8b09e, stop:1 #fff0c8); }"
        "QPushButton { color:#fff8e7; background:#151b26; border:1px solid rgba(255,248,231,60); border-radius:4px; padding:7px 18px; }"
        "QPushButton:focus { background:#252a34; border:1px solid #fff0c8; }");
}

}

WinDSProInstallWorker::WinDSProInstallWorker(QObject *parent) : QThread(parent) {}
WinDSProInstallWorker::~WinDSProInstallWorker() { wait(); }
void WinDSProInstallWorker::reportDownloadProgress(qint64 received, qint64 total) { emit downloadProgress(received, total); }

void WinDSProInstallWorker::run()
{
#ifndef Q_OS_WIN
    emit failed(QStringLiteral("Automatic WinDS PRO installation is only available on Windows."));
#else
    emit stageChanged(QStringLiteral("Resolving WinDS PRO download..."));
    QByteArray html; QString error;
    if (!fetchText(QString::fromLatin1(kMediaFirePage), &html, &error)) { emit failed(QStringLiteral("Unable to open the WinDS PRO MediaFire page. %1").arg(error)); return; }
    const QString direct = extractMediaFireDirectUrl(html);
    if (direct.isEmpty()) { emit failed(QStringLiteral("MediaFire did not expose a downloadable WinDS PRO link. The page format may have changed.")); return; }

    const QString installer = tempInstallerPath(); QFile::remove(installer);
    emit stageChanged(QStringLiteral("Downloading WinDS PRO 2026.08.22..."));
    if (!downloadFile(direct, installer, this, &error)) { QFile::remove(installer); emit failed(error); return; }
    emit verifying();
    if (!validateInstaller(installer, &error)) { QFile::remove(installer); emit failed(error); return; }
    emit installing();
    if (!runInstaller(installer, &error)) { QFile::remove(installer); emit failed(error); return; }
    QFile::remove(installer);
    if (!WinDSProBootstrap::isInstalled(0)) { emit failed(QStringLiteral("Setup completed, but Kadia could not detect the WinDS PRO files afterwards.")); return; }
    emit completed();
#endif
}

WinDSProBootstrapDialog::WinDSProBootstrapDialog(QWidget *parent)
    : QDialog(parent), m_title(new QLabel(this)), m_status(new QLabel(this)), m_detail(new QLabel(this)),
      m_progress(new QProgressBar(this)), m_install(new QPushButton(QStringLiteral("Install"), this)),
      m_skip(new QPushButton(QStringLiteral("Skip"), this)), m_retry(new QPushButton(QStringLiteral("Retry"), this)),
      m_close(new QPushButton(QStringLiteral("Close"), this)), m_installTimer(new QTimer(this)),
      m_inputTimer(new QTimer(this)), m_worker(0), m_input(this), m_installProgress(0), m_focusIndex(0)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint); setModal(true); setFixedSize(640, 300); setAttribute(Qt::WA_TranslucentBackground, true); setStyleSheet(bootstrapDialogStyle());
    m_title->setObjectName(QStringLiteral("dialogTitle")); m_status->setObjectName(QStringLiteral("dialogStatus")); m_detail->setObjectName(QStringLiteral("dialogDetail"));
    m_title->setText(QStringLiteral("MATHERY   Kadia!")); QFont tf = m_title->font(); tf.setPixelSize(25); tf.setLetterSpacing(QFont::AbsoluteSpacing, 1.2); tf.setWeight(QFont::DemiBold); m_title->setFont(tf);
    m_status->setText(QStringLiteral("Optional emulator package detected as missing")); QFont sf = m_status->font(); sf.setPixelSize(23); sf.setWeight(QFont::Light); m_status->setFont(sf);
    m_detail->setText(QStringLiteral("WinDS PRO 2026.08.22 was not detected. Kadia can download the official package once and install it silently. You can skip this offer; Kadia will not ask again.")); m_detail->setWordWrap(true); QFont df = m_detail->font(); df.setPixelSize(13); m_detail->setFont(df);
    m_progress->setRange(0,100); m_progress->setValue(0); m_progress->hide();
    m_retry->hide(); m_close->hide();
    QFrame *panel = new QFrame(this); panel->setObjectName(QStringLiteral("glassPanel")); QFrame *accent = new QFrame(panel); accent->setObjectName(QStringLiteral("accentGlow")); accent->setFixedHeight(6);
    QHBoxLayout *buttons = new QHBoxLayout; buttons->setSpacing(10); buttons->addStretch(); buttons->addWidget(m_install); buttons->addWidget(m_skip); buttons->addWidget(m_retry); buttons->addWidget(m_close);
    QVBoxLayout *panelLayout = new QVBoxLayout(panel); panelLayout->setContentsMargins(26,18,26,24); panelLayout->setSpacing(10); panelLayout->addWidget(accent); panelLayout->addSpacing(4); panelLayout->addWidget(m_title); panelLayout->addWidget(m_status); panelLayout->addWidget(m_detail); panelLayout->addSpacing(6); panelLayout->addWidget(m_progress); panelLayout->addStretch(); panelLayout->addLayout(buttons);
    QVBoxLayout *layout = new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0); layout->addWidget(panel);
    connect(m_install,SIGNAL(clicked()),this,SLOT(startInstall())); connect(m_skip,SIGNAL(clicked()),this,SLOT(skipInstall()));
    connect(m_retry,SIGNAL(clicked()),this,SLOT(startInstall())); connect(m_close,SIGNAL(clicked()),this,SLOT(reject()));
    connect(m_installTimer,SIGNAL(timeout()),this,SLOT(animateInstallProgress())); m_installTimer->setInterval(280);
    m_input.initialize(); connect(m_inputTimer,SIGNAL(timeout()),this,SLOT(pollController())); m_inputTimer->start(16);
    QRect target = parent ? parent->frameGeometry() : QApplication::desktop()->screenGeometry(QApplication::desktop()->primaryScreen()); move(target.center()-rect().center());
    m_install->setFocus();
}

WinDSProBootstrapDialog::~WinDSProBootstrapDialog() { stopWorker(); }
void WinDSProBootstrapDialog::stopWorker() { if (!m_worker) return; if (m_worker->isRunning()) m_worker->wait(); delete m_worker; m_worker=0; }
void WinDSProBootstrapDialog::skipInstall() { accept(); }

void WinDSProBootstrapDialog::startInstall()
{
    stopWorker(); m_install->hide(); m_skip->hide(); m_retry->hide(); m_close->hide(); m_progress->show(); m_progress->setValue(0); m_installProgress=0;
    m_status->setText(QStringLiteral("Preparing WinDS PRO")); m_detail->setText(QStringLiteral("Kadia is resolving the MediaFire download and will keep this window responsive to XInput."));
    m_worker = new WinDSProInstallWorker(this);
    connect(m_worker,SIGNAL(stageChanged(QString)),this,SLOT(onStageChanged(QString))); connect(m_worker,SIGNAL(downloadProgress(qint64,qint64)),this,SLOT(onDownloadProgress(qint64,qint64)));
    connect(m_worker,SIGNAL(verifying()),this,SLOT(onVerifying())); connect(m_worker,SIGNAL(installing()),this,SLOT(onInstalling())); connect(m_worker,SIGNAL(completed()),this,SLOT(onCompleted())); connect(m_worker,SIGNAL(failed(QString)),this,SLOT(onFailed(QString)));
    m_worker->start();
}
void WinDSProBootstrapDialog::onStageChanged(const QString &s){m_status->setText(s);} 
void WinDSProBootstrapDialog::onDownloadProgress(qint64 r,qint64 t){int p=t>0?static_cast<int>((r*72)/t):qMin(71,m_progress->value()+1);m_progress->setValue(qBound(0,p,72));m_detail->setText(t>0?QStringLiteral("Downloading: %1 / %2").arg(humanBytes(r),humanBytes(t)):QStringLiteral("Downloading: %1").arg(humanBytes(r)));}
void WinDSProBootstrapDialog::onVerifying(){m_progress->setValue(74);m_status->setText(QStringLiteral("Checking WinDS PRO download"));m_detail->setText(QStringLiteral("Kadia is validating the executable header and expected package size before setup runs."));}
void WinDSProBootstrapDialog::onInstalling(){m_installProgress=78;m_progress->setValue(m_installProgress);m_status->setText(QStringLiteral("Installing WinDS PRO"));m_detail->setText(QStringLiteral("The Inno Setup installer is running silently. A Windows UAC prompt can still appear on systems where elevation is required."));m_installTimer->start();}
void WinDSProBootstrapDialog::animateInstallProgress(){if(m_installProgress<96){m_installProgress=qMin(96,m_installProgress+1);m_progress->setValue(m_installProgress);}}
void WinDSProBootstrapDialog::onCompleted(){m_installTimer->stop();m_progress->setValue(100);m_status->setText(QStringLiteral("WinDS PRO ready"));m_detail->setText(QStringLiteral("WinDS PRO was detected successfully."));QTimer::singleShot(500,this,SLOT(accept()));}
void WinDSProBootstrapDialog::onFailed(const QString &m){m_installTimer->stop();m_status->setText(QStringLiteral("WinDS PRO installation could not be completed"));m_detail->setText(m);m_progress->setValue(0);m_retry->show();m_close->show();m_retry->setFocus();}
void WinDSProBootstrapDialog::updateButtonFocus(){}
void WinDSProBootstrapDialog::pollController(){const InputManager::Action a=m_input.poll();if(a==InputManager::None)return;QList<QPushButton*> b; if(m_install->isVisible())b<<m_install<<m_skip; if(m_retry->isVisible())b<<m_retry<<m_close; if(b.isEmpty())return;int idx=b.indexOf(qobject_cast<QPushButton*>(focusWidget()));if(idx<0)idx=0;if(a==InputManager::Left||a==InputManager::Up)idx=(idx-1+b.size())%b.size();else if(a==InputManager::Right||a==InputManager::Down)idx=(idx+1)%b.size();else if(a==InputManager::Accept){b[idx]->click();return;}else if(a==InputManager::Back){if(m_skip->isVisible())m_skip->click();else if(m_close->isVisible())m_close->click();return;}b[idx]->setFocus();}

namespace WinDSProBootstrap {

bool isInstalled(QString *location)
{
#ifdef Q_OS_WIN
    const QStringList roots = QStringList()
        << QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall")
        << QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall")
        << QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall");
    for (int r=0;r<roots.size();++r){QSettings root(roots[r],QSettings::NativeFormat);const QStringList groups=root.childGroups();for(int i=0;i<groups.size();++i){root.beginGroup(groups[i]);const QString name=root.value(QStringLiteral("DisplayName")).toString();const QString loc=root.value(QStringLiteral("InstallLocation")).toString();root.endGroup();if(name.contains(QStringLiteral("WinDS PRO"),Qt::CaseInsensitive)){if(location)*location=loc;return true;}}}
    const QStringList paths=likelyInstallPaths();for(int i=0;i<paths.size();++i){if(looksLikeWinDSProInstall(paths[i])){if(location)*location=QDir(paths[i]).absolutePath();return true;}}
#else
    Q_UNUSED(location);
#endif
    return false;
}
bool wasOffered(){QSettings s;return s.value(QStringLiteral("windspro/offerShown"),false).toBool();}
void markOffered(){QSettings s;s.setValue(QStringLiteral("windspro/offerShown"),true);s.sync();}
void offerOnce(QWidget *parent){if(isInstalled(0)||wasOffered())return;markOffered();WinDSProBootstrapDialog d(parent);d.exec();}

} // namespace WinDSProBootstrap
