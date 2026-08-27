#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <QtGlobal>
#include <QtGui/qwindowdefs.h>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <d3d9.h>
#endif

// Direct3D 9 presenter derived from the proven D3D9 path in the supplied
// Sightline project. Kadia renders into a BGRA QImage (native or an adaptive
// lower internal resolution), copies it into an X8R8G8B8 offscreen surface,
// and lets D3D9 scale/present it to the native Qt HWND at monitor VSync.
class D3D9Renderer
{
public:
    D3D9Renderer();
    ~D3D9Renderer();

    bool initialize(WId windowId, int width, int height);
    void shutdown();
    bool resize(int width, int height);
    bool present(const QImage &frame);

    bool isReady() const;
    QString description() const;
    QString lastError() const;
    int refreshRate() const;

private:
#ifdef Q_OS_WIN
    bool createDevice(HWND hwnd, int width, int height);
    bool createSurface(const QSize &size);
    bool handleLostDevice();
    void releaseSurface();
    void releaseAll();
    bool blitToScreen(int width, int height);
#endif
    void setError(const QString &message);

#ifdef Q_OS_WIN
    IDirect3D9 *m_d3d;
    IDirect3DDevice9 *m_device;
    IDirect3DSurface9 *m_surface;
    HWND m_hwnd;
    QSize m_frameSize;
    QSize m_clientSize;
    bool m_deviceLost;
    UINT m_adapter;
    int m_refreshRate;
    QString m_adapterName;
#endif
    QString m_lastError;
};
