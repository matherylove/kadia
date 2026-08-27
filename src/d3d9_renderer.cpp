#include "d3d9_renderer.h"

#include <QDebug>
#include <cstring>

#ifdef Q_OS_WIN
namespace {
static QString hrString(const char *what, HRESULT hr)
{
    return QString::fromLatin1("%1 failed (HRESULT 0x%2)")
        .arg(QString::fromLatin1(what))
        .arg(static_cast<qulonglong>(static_cast<unsigned long>(hr)), 8, 16, QLatin1Char('0'));
}
}
#endif

D3D9Renderer::D3D9Renderer()
#ifdef Q_OS_WIN
    : m_d3d(0)
    , m_device(0)
    , m_surface(0)
    , m_hwnd(0)
    , m_deviceLost(false)
    , m_adapter(D3DADAPTER_DEFAULT)
    , m_refreshRate(60)
#endif
{
}

D3D9Renderer::~D3D9Renderer()
{
    shutdown();
}

bool D3D9Renderer::initialize(WId windowId, int width, int height)
{
#ifdef Q_OS_WIN
    shutdown();
    m_hwnd = reinterpret_cast<HWND>(windowId);
    if (!m_hwnd) {
        setError(QStringLiteral("The Qt window does not have a native HWND."));
        return false;
    }
    return createDevice(m_hwnd, qMax(1, width), qMax(1, height));
#else
    Q_UNUSED(windowId);
    Q_UNUSED(width);
    Q_UNUSED(height);
    setError(QStringLiteral("Direct3D 9 is available only on Windows."));
    return false;
#endif
}

void D3D9Renderer::shutdown()
{
#ifdef Q_OS_WIN
    releaseAll();
    m_hwnd = 0;
#endif
}

bool D3D9Renderer::resize(int width, int height)
{
#ifdef Q_OS_WIN
    if (!m_device || width <= 0 || height <= 0)
        return false;
    const QSize next(qMax(1, width), qMax(1, height));
    if (next == m_clientSize)
        return true;
    m_clientSize = next;
    m_deviceLost = true;
    return true;
#else
    Q_UNUSED(width);
    Q_UNUSED(height);
    return false;
#endif
}

bool D3D9Renderer::present(const QImage &frame)
{
#ifdef Q_OS_WIN
    if (!m_device || frame.isNull())
        return false;

    if (m_deviceLost || FAILED(m_device->TestCooperativeLevel())) {
        if (!handleLostDevice())
            return false;
    }

    QImage source = frame;
    if (source.format() != QImage::Format_ARGB32 &&
        source.format() != QImage::Format_ARGB32_Premultiplied) {
        source = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }

    if (!m_surface || source.size() != m_frameSize) {
        releaseSurface();
        if (!createSurface(source.size()))
            return false;
    }

    D3DLOCKED_RECT locked;
    std::memset(&locked, 0, sizeof(locked));
    HRESULT hr = m_surface->LockRect(&locked, 0, D3DLOCK_NOSYSLOCK);
    if (FAILED(hr)) {
        setError(hrString("IDirect3DSurface9::LockRect", hr));
        return false;
    }

    const int bytesPerLine = source.width() * 4;
    for (int y = 0; y < source.height(); ++y) {
        const unsigned char *src = source.constScanLine(y);
        unsigned char *dst = static_cast<unsigned char *>(locked.pBits) + y * locked.Pitch;
        std::memcpy(dst, src, static_cast<size_t>(bytesPerLine));
    }
    m_surface->UnlockRect();

    return blitToScreen(source.width(), source.height());
#else
    Q_UNUSED(frame);
    return false;
#endif
}

bool D3D9Renderer::isReady() const
{
#ifdef Q_OS_WIN
    return m_device != 0;
#else
    return false;
#endif
}

QString D3D9Renderer::description() const
{
#ifdef Q_OS_WIN
    if (!isReady())
        return QStringLiteral("Direct3D 9 unavailable");
    if (m_adapterName.isEmpty())
        return QStringLiteral("Direct3D 9 BGRA surface presenter");
    return QStringLiteral("Direct3D 9 BGRA surface presenter - %1 - %2 Hz VSync")
        .arg(m_adapterName).arg(refreshRate());
#else
    return QStringLiteral("Direct3D 9 unavailable on this platform");
#endif
}

QString D3D9Renderer::lastError() const
{
    return m_lastError;
}

int D3D9Renderer::refreshRate() const
{
#ifdef Q_OS_WIN
    return qMax(1, m_refreshRate);
#else
    return 60;
#endif
}

void D3D9Renderer::setError(const QString &message)
{
    m_lastError = message;
    qWarning() << "D3D9:" << message;
}

#ifdef Q_OS_WIN
bool D3D9Renderer::createDevice(HWND hwnd, int width, int height)
{
    m_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!m_d3d) {
        setError(QStringLiteral("Direct3DCreate9 returned null."));
        return false;
    }

    // Select the adapter driving the monitor that actually contains Kadia.
    // D3DADAPTER_DEFAULT is often the primary display, but a moved window or
    // multi-GPU setup can otherwise report/cap to the wrong refresh rate.
    m_adapter = D3DADAPTER_DEFAULT;
    const HMONITOR windowMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    const UINT adapterCount = m_d3d->GetAdapterCount();
    for (UINT i = 0; i < adapterCount; ++i) {
        if (m_d3d->GetAdapterMonitor(i) == windowMonitor) {
            m_adapter = i;
            break;
        }
    }

    D3DADAPTER_IDENTIFIER9 identifier;
    std::memset(&identifier, 0, sizeof(identifier));
    if (SUCCEEDED(m_d3d->GetAdapterIdentifier(m_adapter, 0, &identifier)))
        m_adapterName = QString::fromLatin1(identifier.Description);

    D3DDISPLAYMODE mode;
    std::memset(&mode, 0, sizeof(mode));
    if (SUCCEEDED(m_d3d->GetAdapterDisplayMode(m_adapter, &mode)) && mode.RefreshRate > 0)
        m_refreshRate = static_cast<int>(mode.RefreshRate);
    else
        m_refreshRate = 60;

    D3DPRESENT_PARAMETERS pp;
    std::memset(&pp, 0, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.BackBufferWidth = qMax(1, width);
    pp.BackBufferHeight = qMax(1, height);
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    // Let D3D9 block on the desktop vertical blank. The render timer runs as
    // fast as the event loop allows, while Present caps animation delivery at
    // the primary monitor's real refresh rate (60/75/120/144/etc.).
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    const DWORD flags = D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE;
    HRESULT hr = m_d3d->CreateDevice(m_adapter, D3DDEVTYPE_HAL,
                                     hwnd, flags, &pp, &m_device);
    if (FAILED(hr) || !m_device) {
        setError(hrString("IDirect3D9::CreateDevice", hr));
        releaseAll();
        return false;
    }

    m_clientSize = QSize(pp.BackBufferWidth, pp.BackBufferHeight);
    m_deviceLost = false;
    m_lastError.clear();
    return true;
}

bool D3D9Renderer::createSurface(const QSize &size)
{
    if (!m_device || size.isEmpty())
        return false;

    IDirect3DSurface9 *surface = 0;
    HRESULT hr = m_device->CreateOffscreenPlainSurface(
        size.width(), size.height(), D3DFMT_X8R8G8B8,
        D3DPOOL_DEFAULT, &surface, 0);
    if (FAILED(hr) || !surface) {
        setError(hrString("IDirect3DDevice9::CreateOffscreenPlainSurface", hr));
        return false;
    }

    m_surface = surface;
    m_frameSize = size;
    return true;
}

bool D3D9Renderer::handleLostDevice()
{
    if (!m_device)
        return false;

    const HRESULT state = m_device->TestCooperativeLevel();
    if (state == D3DERR_DEVICELOST)
        return false;

    if (state == D3DERR_DEVICENOTRESET || m_deviceLost) {
        releaseSurface();

        D3DPRESENT_PARAMETERS pp;
        std::memset(&pp, 0, sizeof(pp));
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = m_hwnd;
        pp.BackBufferWidth = qMax(1, m_clientSize.width());
        pp.BackBufferHeight = qMax(1, m_clientSize.height());
        pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        pp.BackBufferCount = 1;
        // Let D3D9 block on the desktop vertical blank. The render timer runs as
    // fast as the event loop allows, while Present caps animation delivery at
    // the primary monitor's real refresh rate (60/75/120/144/etc.).
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

        const HRESULT hr = m_device->Reset(&pp);
        if (FAILED(hr)) {
            setError(hrString("IDirect3DDevice9::Reset", hr));
            return false;
        }
        m_deviceLost = false;
    }
    return true;
}

void D3D9Renderer::releaseSurface()
{
    if (m_surface) {
        m_surface->Release();
        m_surface = 0;
    }
    m_frameSize = QSize();
}

void D3D9Renderer::releaseAll()
{
    releaseSurface();
    if (m_device) {
        m_device->Release();
        m_device = 0;
    }
    if (m_d3d) {
        m_d3d->Release();
        m_d3d = 0;
    }
    m_deviceLost = false;
    m_clientSize = QSize();
    m_adapterName.clear();
    m_adapter = D3DADAPTER_DEFAULT;
    m_refreshRate = 60;
}

bool D3D9Renderer::blitToScreen(int width, int height)
{
    if (!m_device || !m_surface)
        return false;

    IDirect3DSurface9 *backBuffer = 0;
    HRESULT hr = m_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
    if (FAILED(hr) || !backBuffer) {
        setError(hrString("IDirect3DDevice9::GetBackBuffer", hr));
        return false;
    }

    m_device->ColorFill(backBuffer, 0, D3DCOLOR_XRGB(0, 0, 0));

    RECT source = { 0, 0, width, height };
    RECT target;
    const double frameAspect = static_cast<double>(width) / static_cast<double>(qMax(1, height));
    const double clientAspect = static_cast<double>(m_clientSize.width()) /
                                static_cast<double>(qMax(1, m_clientSize.height()));
    if (clientAspect > frameAspect) {
        const int drawWidth = static_cast<int>(m_clientSize.height() * frameAspect);
        target.left = (m_clientSize.width() - drawWidth) / 2;
        target.right = target.left + drawWidth;
        target.top = 0;
        target.bottom = m_clientSize.height();
    } else {
        const int drawHeight = static_cast<int>(m_clientSize.width() / frameAspect);
        target.left = 0;
        target.right = m_clientSize.width();
        target.top = (m_clientSize.height() - drawHeight) / 2;
        target.bottom = target.top + drawHeight;
    }
    hr = m_device->StretchRect(m_surface, &source, backBuffer, &target, D3DTEXF_LINEAR);
    backBuffer->Release();
    if (FAILED(hr)) {
        setError(hrString("IDirect3DDevice9::StretchRect", hr));
        return false;
    }

    hr = m_device->Present(0, 0, 0, 0);
    if (hr == D3DERR_DEVICELOST) {
        m_deviceLost = true;
        return false;
    }
    if (FAILED(hr)) {
        setError(hrString("IDirect3DDevice9::Present", hr));
        return false;
    }
    return true;
}
#endif
