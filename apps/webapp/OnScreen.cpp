#include "OnScreen.h"

#include <QGuiApplication>
#include <QLoggingCategory>

#include <cstdlib>
#include <memory>

Q_LOGGING_CATEGORY(lcOnScreen, "webapp.screen")

namespace {

template<typename T>
std::unique_ptr<T, decltype(&std::free)> reply(T *raw)
{
    return {raw, &std::free};
}

} // namespace

OnScreen::OnScreen(QObject *parent)
    : QObject(parent)
{
    auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!x11 || !x11->connection())
        return;
    m_connection = x11->connection();
    const xcb_window_t root = xcb_setup_roots_iterator(xcb_get_setup(m_connection)).data->root;
    if (cardinal(root, atom("GAMESCOPE_PID")) == 0) {
        qCInfo(lcOnScreen) << "not running in gamescope";
        return;
    }
    m_root = root;
    m_focusedApp = atom("GAMESCOPE_FOCUSED_APP");
    m_steamGame = atom("STEAM_GAME");

    // Watch the root window's properties, keeping whatever Qt selected there.
    auto attributes = reply(xcb_get_window_attributes_reply(
        m_connection, xcb_get_window_attributes(m_connection, m_root), nullptr));
    const uint32_t mask = (attributes ? attributes->your_event_mask : 0) | XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_change_window_attributes(m_connection, m_root, XCB_CW_EVENT_MASK, &mask);
    xcb_flush(m_connection);
    qGuiApp->installNativeEventFilter(this);
}

OnScreen::~OnScreen()
{
    if (m_root)
        qGuiApp->removeNativeEventFilter(this);
}

void OnScreen::setWindow(QWindow *window)
{
    if (m_window == window)
        return;
    m_window = window;
    emit windowChanged();
    evaluate();
}

bool OnScreen::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
    if (eventType != "xcb_generic_event_t")
        return false;
    auto *event = static_cast<xcb_generic_event_t *>(message);
    if ((event->response_type & ~0x80) != XCB_PROPERTY_NOTIFY)
        return false;
    auto *notify = reinterpret_cast<xcb_property_notify_event_t *>(event);
    // The focus moved, or the shell tagged our window.
    const bool ours = m_window && notify->window == m_window->winId();
    if ((notify->window == m_root && notify->atom == m_focusedApp) || (ours && notify->atom == m_steamGame))
        QMetaObject::invokeMethod(this, &OnScreen::evaluate, Qt::QueuedConnection);
    return false;
}

void OnScreen::evaluate()
{
    if (!m_root || !m_window)
        return;
    const quint32 ours = cardinal(xcb_window_t(m_window->winId()), m_steamGame);
    const bool onScreen = ours != 0 && cardinal(m_root, m_focusedApp) == ours;
    if (m_onScreen == onScreen)
        return;
    m_onScreen = onScreen;
    qCInfo(lcOnScreen) << (onScreen ? "on screen" : "not on screen");
    emit onScreenChanged();
}

xcb_atom_t OnScreen::atom(const char *name) const
{
    auto atomReply = reply(xcb_intern_atom_reply(
        m_connection, xcb_intern_atom(m_connection, 0, qstrlen(name), name), nullptr));
    return atomReply ? atomReply->atom : XCB_ATOM_NONE;
}

quint32 OnScreen::cardinal(xcb_window_t window, xcb_atom_t atom) const
{
    auto property = reply(xcb_get_property_reply(
        m_connection, xcb_get_property(m_connection, 0, window, atom, XCB_ATOM_CARDINAL, 0, 1), nullptr));
    if (!property || property->format != 32 || xcb_get_property_value_length(property.get()) < 4)
        return 0;
    return *static_cast<const quint32 *>(xcb_get_property_value(property.get()));
}
