#pragma once

#include <QString>

class QWidget;

namespace EmulatorManager
{
    bool launch(const QString &system, const QString &romPath, QWidget *parent = 0,
                qint64 *processIdOut = 0);
    QString configuredEmulator(const QString &system);
    void setConfiguredEmulator(const QString &system, const QString &executable);

    // Opens a persistent per-console emulator assignment editor. Detection is
    // suggestion-only; nothing is chosen until the user confirms it.
    bool configureEmulators(QWidget *parent = 0);

    // Staged fullscreen safety fallback for emulator versions that ignore their
    // native CLI option. stage 0 requests Alt+Enter, stage 1 requests F11 and
    // stage 2 merely maximizes the untouched top-level window. Kadia never
    // strips styles or keeps re-forcing a window, so the emulator remains free
    // to leave fullscreen with its own shortcut.
    bool enforceFullscreen(qint64 processId, int stage);
    bool isProcessTreeRunning(qint64 processId);
}
