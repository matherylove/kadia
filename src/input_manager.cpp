#include "input_manager.h"

#ifdef Q_OS_WIN
#  include <windows.h>
namespace {
struct KadiaXInputGamepad {
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
};
struct KadiaXInputState {
    DWORD dwPacketNumber;
    KadiaXInputGamepad Gamepad;
};
const WORD KADIA_XINPUT_DPAD_UP = 0x0001;
const WORD KADIA_XINPUT_DPAD_DOWN = 0x0002;
const WORD KADIA_XINPUT_DPAD_LEFT = 0x0004;
const WORD KADIA_XINPUT_DPAD_RIGHT = 0x0008;
const WORD KADIA_XINPUT_BACK = 0x0020;
const WORD KADIA_XINPUT_A = 0x1000;
const WORD KADIA_XINPUT_B = 0x2000;
const WORD KADIA_XINPUT_Y = 0x8000;
}
#endif

InputManager::InputManager(QObject *parent)
    : QObject(parent)
#ifdef Q_OS_WIN
    , m_module(0)
    , m_getState(0)
    , m_previousButtons(0)
#endif
    , m_connected(false)
{
}

InputManager::~InputManager()
{
#ifdef Q_OS_WIN
    if (m_module)
        FreeLibrary(static_cast<HMODULE>(m_module));
#endif
}

bool InputManager::initialize()
{
#ifdef Q_OS_WIN
    const wchar_t *candidates[] = {
        L"xinput1_3.dll",
        L"xinput9_1_0.dll",
        L"xinput1_4.dll"
    };

    for (int i = 0; i < 3 && !m_module; ++i) {
        HMODULE module = LoadLibraryW(candidates[i]);
        if (!module)
            continue;

        FARPROC proc = GetProcAddress(module, "XInputGetState");
        if (!proc) {
            FreeLibrary(module);
            continue;
        }

        m_module = module;
        m_getState = reinterpret_cast<XInputGetStateFn>(proc);
    }

    return m_getState != 0;
#else
    return false;
#endif
}

InputManager::Action InputManager::poll()
{
#ifdef Q_OS_WIN
    if (!m_getState) {
        m_connected = false;
        return None;
    }

    KadiaXInputState state;
    ZeroMemory(&state, sizeof(state));
    const DWORD result = m_getState(0, &state);
    if (result != ERROR_SUCCESS) {
        m_connected = false;
        m_previousButtons = 0;
        return None;
    }

    m_connected = true;
    const unsigned short buttons = state.Gamepad.wButtons;
    const unsigned short edges = static_cast<unsigned short>(buttons & ~m_previousButtons);
    m_previousButtons = buttons;

    if (edges & KADIA_XINPUT_DPAD_UP) return Up;
    if (edges & KADIA_XINPUT_DPAD_DOWN) return Down;
    if (edges & KADIA_XINPUT_DPAD_LEFT) return Left;
    if (edges & KADIA_XINPUT_DPAD_RIGHT) return Right;
    if (edges & KADIA_XINPUT_A) return Accept;
    if (edges & KADIA_XINPUT_B) return Back;
    if (edges & KADIA_XINPUT_BACK) return ToggleGallery;
    if (edges & KADIA_XINPUT_Y) return CycleSort;
    return None;
#else
    return None;
#endif
}

bool InputManager::controllerConnected() const
{
    return m_connected;
}

QString InputManager::backendName() const
{
#ifdef Q_OS_WIN
    return m_getState ? QStringLiteral("XInput") : QStringLiteral("Keyboard only");
#else
    return QStringLiteral("Keyboard only");
#endif
}
