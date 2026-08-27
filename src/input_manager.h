#pragma once

#include <QObject>
#include <QString>
#include <QtGlobal>

class InputManager : public QObject
{
    Q_OBJECT
public:
    enum Action {
        None,
        Up,
        Down,
        Left,
        Right,
        Accept,
        Back,
        ToggleGallery,
        CycleSort
    };

    explicit InputManager(QObject *parent = 0);
    ~InputManager();

    bool initialize();
    Action poll();
    bool controllerConnected() const;
    QString backendName() const;

private:
#ifdef Q_OS_WIN
    typedef unsigned long (__stdcall *XInputGetStateFn)(unsigned long, void *);
    void *m_module;
    XInputGetStateFn m_getState;
    unsigned short m_previousButtons;
#endif
    bool m_connected;
};
