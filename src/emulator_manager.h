#pragma once

#include <QString>

class QWidget;

namespace EmulatorManager
{
    bool launch(const QString &system, const QString &romPath, QWidget *parent = 0,
                qint64 *processIdOut = 0);
    QString configuredEmulator(const QString &system);
    void setConfiguredEmulator(const QString &system, const QString &executable);

    // Best-effort Windows fallback for emulator builds that ignore or do not
    // expose a fullscreen CLI switch. Finds the largest visible window owned by
    // the launched process (or one of its children) and makes it borderless at
    // the monitor bounds. It does not steal foreground focus.
    bool enforceFullscreen(qint64 processId);
    bool isProcessTreeRunning(qint64 processId);
}
