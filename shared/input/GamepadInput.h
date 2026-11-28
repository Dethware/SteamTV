#pragma once

#include <QHash>

#include <algorithm>
#include <cstdlib>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

// Controller input through SDL3's gamepad API. In Gaming Mode this reads Steam's virtual
// Xbox pad (Steam Input keeps the real controller). Buttons become key events for the window,
// so QML handles the remote, a keyboard and the controller the same way.
//
// The virtual pad also feeds games: keep `enabled` false whenever the program isn't on screen.
// While Steam's own UI has focus, Steam reads the controller itself and the pad stays silent.
class GamepadInput : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QWindow *window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool sticks READ sticks WRITE setSticks NOTIFY sticksChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit GamepadInput(QObject *parent = nullptr);
    ~GamepadInput() override;

    QWindow *window() const { return m_window; }
    void setWindow(QWindow *window);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    // Whether the sticks count at all (keys and activity). A thumb resting on a trackpad
    // that Steam maps to the stick moves it, so the screensaver and screen-off turn this off:
    // only real button presses end them.
    bool sticks() const { return m_sticks; }
    void setSticks(bool sticks);

    // Connected gamepads.
    int count() const { return m_count; }

signals:
    // Any button or a real stick movement, also while disabled (for idle tracking).
    void activity();
    // LB (-1) / RB (+1), only while enabled. Controller-only: no key event.
    void shoulderPressed(int direction);
    void windowChanged();
    void enabledChanged();
    void sticksChanged();
    void countChanged();

private:
    void poll();
    void press(int key);
    void release(int key);
    void sendKey(int key, bool pressed, bool autoRepeat = false);

    QPointer<QWindow> m_window;
    bool m_enabled = true;
    bool m_sticks = true;
    bool m_initialized = false;
    int m_count = 0;
    QTimer m_pollTimer;

    // Held direction (stick or d-pad) and its auto-repeat.
    int m_heldKey = 0;
    QTimer m_repeatTimer;
    // The sticks' positions and the direction they currently count as (0 = centred). Either
    // stick moves: the right one sits by the buttons, for using the controller in one hand.
    // Each controller's sticks by themselves (keyed by SDL's id of it). One position for
    // all of them made a second controller's noise at rest overwrite the stick that was
    // being pushed, 200 times a second, and every return counted as a new press.
    struct Sticks
    {
        int leftX = 0;
        int leftY = 0;
        int rightX = 0;
        int rightY = 0;

        int furthest() const { return std::max({std::abs(leftX), std::abs(leftY), std::abs(rightX), std::abs(rightY)}); }
    };
    QHash<quint32, Sticks> m_stickStates;
    int m_stickKey = 0;
};
