#include "GamepadInput.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QLoggingCategory>

#include <SDL3/SDL.h>

#include <cstdlib>

Q_LOGGING_CATEGORY(lcGamepad, "tv.gamepad")

namespace {

constexpr int PollIntervalMs = 8;
constexpr int RepeatDelayMs = 400;
constexpr int RepeatIntervalMs = 120;
// Stick deflection (of 32767) that counts as a direction; lower to let go, to avoid chatter.
constexpr int StickPress = 20000;
constexpr int StickRelease = 12000;
constexpr int StickActivity = 8000;

int keyForButton(int button)
{
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return Qt::Key_Return;
    case SDL_GAMEPAD_BUTTON_EAST:
    case SDL_GAMEPAD_BUTTON_BACK:
        return Qt::Key_Back;
    case SDL_GAMEPAD_BUTTON_START:
        return Qt::Key_Menu;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return Qt::Key_Up;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return Qt::Key_Down;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return Qt::Key_Left;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return Qt::Key_Right;
    default:
        return 0;
    }
}

// Steam's virtual pads all have this USB id, whatever the controller behind them.
bool isSteamVirtualPad(SDL_JoystickID id)
{
    return SDL_GetGamepadVendorForID(id) == 0x28de && SDL_GetGamepadProductForID(id) == 0x11ff;
}

bool isDirection(int key)
{
    return key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left || key == Qt::Key_Right;
}

} // namespace

GamepadInput::GamepadInput(QObject *parent)
    : QObject(parent)
{
    // Steam owns the real controller (its hidraw devices); only use the evdev gamepads.
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
    // SDL skips Steam's virtual pad unless told otherwise. SDL reads this switch from the
    // environment only; SDL_SetHint() has no effect on it (checked with SDL 3.4.12).
    qputenv("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", "1");
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        qCWarning(lcGamepad) << "SDL_Init failed:" << SDL_GetError();
        return;
    }
    m_initialized = true;

    int joystickCount = 0;
    if (SDL_JoystickID *joysticks = SDL_GetJoysticks(&joystickCount)) {
        for (int i = 0; i < joystickCount; ++i) {
            qCInfo(lcGamepad).nospace() << "joystick " << SDL_GetJoystickNameForID(joysticks[i]) << " "
                                        << Qt::hex << SDL_GetJoystickVendorForID(joysticks[i]) << ":"
                                        << SDL_GetJoystickProductForID(joysticks[i])
                                        << (SDL_IsGamepad(joysticks[i]) ? " (gamepad)" : " (not a gamepad)");
        }
        SDL_free(joysticks);
    }
    qCInfo(lcGamepad) << joystickCount << "joysticks at startup";

    m_pollTimer.setInterval(PollIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &GamepadInput::poll);
    m_pollTimer.start();

    m_repeatTimer.setSingleShot(false);
    connect(&m_repeatTimer, &QTimer::timeout, this, [this] {
        m_repeatTimer.setInterval(RepeatIntervalMs);
        if (m_heldKey)
            sendKey(m_heldKey, true, true);
    });
}

GamepadInput::~GamepadInput()
{
    if (m_initialized)
        SDL_Quit();
}

void GamepadInput::setWindow(QWindow *window)
{
    if (m_window == window)
        return;
    m_window = window;
    emit windowChanged();
}

void GamepadInput::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    if (!enabled && m_heldKey)
        release(m_heldKey);
    m_enabled = enabled;
    emit enabledChanged();
}

void GamepadInput::setSticks(bool sticks)
{
    if (m_sticks == sticks)
        return;
    if (!sticks && m_stickKey) {
        release(m_stickKey);
        m_stickKey = 0;
    }
    m_sticks = sticks;
    qCDebug(lcGamepad) << "sticks" << (sticks ? "on" : "off");
    emit sticksChanged();
}

void GamepadInput::poll()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_GAMEPAD_ADDED:
            // Steam makes a virtual pad for every controller it knows. A controller that the
            // kernel also shows as a gamepad (a Joy-Con) would otherwise be read twice, in
            // two different layouts.
            if (!isSteamVirtualPad(event.gdevice.which)) {
                qCInfo(lcGamepad) << "gamepad ignored, not one of Steam's:"
                                  << SDL_GetGamepadNameForID(event.gdevice.which);
                break;
            }
            if (SDL_Gamepad *pad = SDL_OpenGamepad(event.gdevice.which)) {
                qCInfo(lcGamepad) << "gamepad added:" << SDL_GetGamepadName(pad);
                ++m_count;
                emit countChanged();
            }
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            m_stickStates.remove(event.gdevice.which);
            if (SDL_Gamepad *pad = SDL_GetGamepadFromID(event.gdevice.which)) {
                SDL_CloseGamepad(pad);
                --m_count;
                emit countChanged();
            }
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            emit activity();
            qCDebug(lcGamepad) << "button down" << SDL_GetGamepadStringForButton(SDL_GamepadButton(event.gbutton.button))
                               << (m_enabled ? "" : "(ignored, not on screen)");
            if (m_enabled && event.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)
                emit shoulderPressed(-1);
            else if (m_enabled && event.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)
                emit shoulderPressed(1);
            else if (const int key = keyForButton(event.gbutton.button))
                press(key);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            if (const int key = keyForButton(event.gbutton.button))
                release(key);
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            Sticks &moved = m_stickStates[event.gaxis.which];
            if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX)
                moved.leftX = event.gaxis.value;
            else if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY)
                moved.leftY = event.gaxis.value;
            else if (event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHTX)
                moved.rightX = event.gaxis.value;
            else if (event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHTY)
                moved.rightY = event.gaxis.value;
            else
                break;
            if (!m_sticks)
                break;
            // The virtual pad reports some stick noise at rest; ignore that for idle tracking.
            if (std::abs(event.gaxis.value) >= StickActivity)
                emit activity();
            // Whichever stick is pushed further decides: of all controllers, then of that
            // controller's two.
            Sticks sticks;
            for (const Sticks &candidate : std::as_const(m_stickStates)) {
                if (candidate.furthest() > sticks.furthest())
                    sticks = candidate;
            }
            const bool right = std::max(std::abs(sticks.rightX), std::abs(sticks.rightY))
                               > std::max(std::abs(sticks.leftX), std::abs(sticks.leftY));
            const int x = right ? sticks.rightX : sticks.leftX;
            const int y = right ? sticks.rightY : sticks.leftY;
            int key = 0;
            const int threshold = m_stickKey ? StickRelease : StickPress;
            if (std::abs(x) >= std::abs(y) && std::abs(x) >= threshold)
                key = x < 0 ? Qt::Key_Left : Qt::Key_Right;
            else if (std::abs(y) > std::abs(x) && std::abs(y) >= threshold)
                key = y < 0 ? Qt::Key_Up : Qt::Key_Down;
            if (key != m_stickKey) {
                qCDebug(lcGamepad) << "stick key" << key;
                if (m_stickKey)
                    release(m_stickKey);
                m_stickKey = key;
                if (key)
                    press(key);
            }
            break;
        }
        default:
            break;
        }
    }
}

void GamepadInput::press(int key)
{
    if (!m_enabled)
        return;
    sendKey(key, true);
    if (isDirection(key)) {
        m_heldKey = key;
        m_repeatTimer.start(RepeatDelayMs);
    }
}

void GamepadInput::release(int key)
{
    if (key == m_heldKey) {
        m_heldKey = 0;
        m_repeatTimer.stop();
    }
    if (m_enabled)
        sendKey(key, false);
}

void GamepadInput::sendKey(int key, bool pressed, bool autoRepeat)
{
    if (!m_window)
        return;
    QKeyEvent event(pressed ? QEvent::KeyPress : QEvent::KeyRelease, key, Qt::NoModifier, QString(), autoRepeat);
    QGuiApplication::sendEvent(m_window, &event);
}
