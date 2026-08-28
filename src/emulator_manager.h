#pragma once

#include <QString>

class QWidget;

namespace EmulatorManager
{
    bool launch(const QString &system, const QString &romPath, QWidget *parent = 0,
                qint64 *processIdOut = 0, QString *emulatorExecutableOut = 0);
    QString configuredEmulator(const QString &system);
    void setConfiguredEmulator(const QString &system, const QString &executable);

    // Opens a persistent per-console emulator assignment editor. Detection is
    // suggestion-only; nothing is chosen until the user confirms it.
    bool configureEmulators(QWidget *parent = 0);

    // Staged fullscreen fallback for emulator versions that ignore their native
    // CLI option. Kadia asks the emulator itself to toggle fullscreen using a
    // foreground SendInput sequence (normally Alt+Enter), never by rewriting the
    // emulator HWND styles/swapchain geometry. The executable name selects the
    // safest fallback for that emulator.
    bool enforceFullscreen(qint64 processId, const QString &emulatorExecutable, int stage);
    bool isProcessTreeRunning(qint64 processId);
    bool isLaunchRunning(qint64 processId, const QString &emulatorExecutable);
}
