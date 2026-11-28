#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

#include <xcb/xcb.h>

// Whether gamescope shows this program's window.
//
// The shell tags the window with STEAM_GAME (an ID of its own, 0x7E0000xx) and hands it the
// screen; gamescope then writes that ID to GAMESCOPE_FOCUSED_APP on the root window. This
// compares the two, so the program needs nothing from the shell to know it. As in the
// shell's session/BaseLayer.
class OnScreen : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QWindow *window READ window WRITE setWindow NOTIFY windowChanged)
    // Running in gamescope; otherwise onScreen is always false.
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool onScreen READ onScreen NOTIFY onScreenChanged)

public:
    explicit OnScreen(QObject *parent = nullptr);
    ~OnScreen() override;

    QWindow *window() const { return m_window; }
    void setWindow(QWindow *window);
    bool available() const { return m_root != 0; }
    bool onScreen() const { return m_onScreen; }

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void windowChanged();
    void onScreenChanged();

private:
    void evaluate();
    xcb_atom_t atom(const char *name) const;
    quint32 cardinal(xcb_window_t window, xcb_atom_t atom) const;

    QPointer<QWindow> m_window;
    xcb_connection_t *m_connection = nullptr;
    xcb_window_t m_root = 0;
    xcb_atom_t m_focusedApp = XCB_ATOM_NONE;
    xcb_atom_t m_steamGame = XCB_ATOM_NONE;
    bool m_onScreen = false;
};
