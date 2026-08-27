#pragma once

#include <QString>

class QWidget;

namespace EmulatorManager
{
    bool launch(const QString &system, const QString &romPath, QWidget *parent = 0);
    QString configuredEmulator(const QString &system);
    void setConfiguredEmulator(const QString &system, const QString &executable);
}
