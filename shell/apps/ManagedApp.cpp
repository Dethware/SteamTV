#include "ManagedApp.h"

#include <csignal>
#include <QDBusConnection>
#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QGuiApplication>
#include <QLoggingCategory>

#include <cstdlib>
#include <memory>

Q_LOGGING_CATEGORY(lcManagedApp, "tvshell.app")

namespace {

constexpr int WindowPollMs = 500;
// How long resume() waits for a player that isn't back to "Paused" yet.
constexpr int ResumePendingMs = 5000;

const QString Systemd = QStringLiteral("org.freedesktop.systemd1");
const QString SystemdPath = QStringLiteral("/org/freedesktop/systemd1");
const QString SystemdManager = QStringLiteral("org.freedesktop.systemd1.Manager");
const QString SystemdUnit = QStringLiteral("org.freedesktop.systemd1.Unit");
const QString Properties = QStringLiteral("org.freedesktop.DBus.Properties");
const QString MprisPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString MprisPlayer = QStringLiteral("org.mpris.MediaPlayer2.Player");
// A web app's media kind (apps/webapp/MediaKind.h).
const QString MediaKindPath = QStringLiteral("/local/webapp");
const QString MediaKindInterface = QStringLiteral("local.webapp");

template<typename T>
using XcbReply = std::unique_ptr<T, decltype(&std::free)>;

template<typename T>
XcbReply<T> reply(T *raw)
{
    return XcbReply<T>(raw, &std::free);
}

QDBusMessage systemdCall(const QString &method)
{
    return QDBusMessage::createMethodCall(Systemd, SystemdPath, SystemdManager, method);
}

QDBusMessage getProperty(const QString &service, const QString &path, const QString &interface, const QString &name)
{
    auto call = QDBusMessage::createMethodCall(service, path, Properties, QStringLiteral("Get"));
    call << interface << name;
    return call;
}

QDBusMessage getAll(const QString &service, const QString &path, const QString &interface)
{
    auto call = QDBusMessage::createMethodCall(service, path, Properties, QStringLiteral("GetAll"));
    call << interface;
    return call;
}

// A dictionary inside a variant arrives as a QDBusArgument still to be read.
QVariantMap toMap(const QVariant &value)
{
    if (value.canConvert<QDBusArgument>())
        return qdbus_cast<QVariantMap>(value.value<QDBusArgument>());
    return value.toMap();
}

QStringList toStrings(const QVariant &value)
{
    if (value.canConvert<QDBusArgument>())
        return qdbus_cast<QStringList>(value.value<QDBusArgument>());
    return value.toStringList();
}

} // namespace

ManagedApp::ManagedApp(QObject *parent)
    : QObject(parent)
{
    // No X11 (the "offscreen" platform in a test run): the service can still be followed, but
    // there is never a window to show.
    if (auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>(); x11 && x11->connection()) {
        m_connection = x11->connection();
        m_root = xcb_setup_roots_iterator(xcb_get_setup(m_connection)).data->root;
    }
    m_resumePending.setSingleShot(true);
    m_resumePending.setInterval(ResumePendingMs);
    m_windowPoll.setInterval(WindowPollMs);
    connect(&m_windowPoll, &QTimer::timeout, this, &ManagedApp::checkWindow);

    QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                          QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameOwnerChanged"),
                                          this, SLOT(nameOwnerChanged(QString, QString, QString)));
}

void ManagedApp::setUnit(const QString &unit)
{
    if (m_unit == unit)
        return;
    m_unit = unit;
    emit unitChanged();
    watchUnit();
}

void ManagedApp::setWindowClass(const QString &windowClass)
{
    if (m_windowClass == windowClass)
        return;
    m_windowClass = windowClass;
    emit windowClassChanged();
}

void ManagedApp::setAppId(quint32 appId)
{
    if (m_appId == appId)
        return;
    m_appId = appId;
    emit appIdChanged();
}

void ManagedApp::setMprisName(const QString &name)
{
    if (m_mprisName == name)
        return;
    m_mprisName = name;
    emit mprisNameChanged();
    findPlayer();
}

void ManagedApp::setMediaKindName(const QString &name)
{
    if (m_mediaKindName == name)
        return;
    m_mediaKindName = name;
    emit mediaKindNameChanged();
    watchMediaKind();
}

void ManagedApp::start()
{
    m_stopping = false;
    if (m_running || m_unit.isEmpty())
        return;
    qCInfo(lcManagedApp) << "starting" << m_unit;
    auto call = systemdCall(QStringLiteral("StartUnit"));
    call << m_unit << QStringLiteral("replace");
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        if (finished->isError())
            qCWarning(lcManagedApp) << "could not start" << m_unit << ":" << finished->error().message();
    });
}

void ManagedApp::quit()
{
    if (!m_running || m_unit.isEmpty())
        return;
    qCInfo(lcManagedApp) << "stopping" << m_unit;
    m_stopping = true;
    auto call = systemdCall(QStringLiteral("StopUnit"));
    call << m_unit << QStringLiteral("replace");
    QDBusConnection::sessionBus().asyncCall(call);
}

// For a program that doesn't end when asked: every process of the unit is killed.
void ManagedApp::kill()
{
    if (!m_running || m_unit.isEmpty())
        return;
    qCInfo(lcManagedApp) << "killing" << m_unit;
    m_stopping = true;
    auto call = systemdCall(QStringLiteral("KillUnit"));
    call << m_unit << QStringLiteral("all") << qint32(SIGKILL);
    QDBusConnection::sessionBus().asyncCall(call);
}

void ManagedApp::pause()
{
    if (m_player.isEmpty() || !m_playing)
        return;
    qCInfo(lcManagedApp) << "pausing" << m_unit;
    setPausedByUs(false);
    m_resumePending.stop();
    QDBusConnection::sessionBus().asyncCall(
        QDBusMessage::createMethodCall(m_player, MprisPath, MprisPlayer, QStringLiteral("Pause")));
}

void ManagedApp::pauseWhileAway()
{
    if (m_player.isEmpty() || !m_playing)
        return;
    pause();
    setPausedByUs(true);
}

void ManagedApp::setPausedByUs(bool paused)
{
    if (m_pausedByUs == paused)
        return;
    m_pausedByUs = paused;
    emit pausedWhileAwayChanged();
}

void ManagedApp::setAllWindows(bool all)
{
    if (m_allWindows == all)
        return;
    m_allWindows = all;
    emit allWindowsChanged();
}

// The other half of pause(): a program that was paused when it was left carries on.
void ManagedApp::resume()
{
    if (m_player.isEmpty() || m_playing || !(m_pausedByUs || m_alwaysResume))
        return;
    setPausedByUs(false);
    // A web app's page may be frozen while away: MPRIS Play doesn't reach it, and Chromium's
    // player says "Stopped" until something plays. The web app continues it itself once
    // the page runs again.
    if (!m_mediaKindName.isEmpty()) {
        qCInfo(lcManagedApp) << "continuing" << m_unit << "through the web app";
        QDBusConnection::sessionBus().asyncCall(
            QDBusMessage::createMethodCall(m_mediaKindName, MediaKindPath, MediaKindInterface, QStringLiteral("Resume")));
        return;
    }
    if (m_status == u"Paused") {
        play();
        return;
    }
    qCInfo(lcManagedApp) << m_unit << "is" << m_status << "- continuing once it is paused";
    m_resumePending.start();
}

void ManagedApp::play()
{
    qCInfo(lcManagedApp) << "continuing" << m_unit;
    QDBusConnection::sessionBus().asyncCall(
        QDBusMessage::createMethodCall(m_player, MprisPath, MprisPlayer, QStringLiteral("Play")));
}

void ManagedApp::togglePlay()
{
    if (m_player.isEmpty())
        return;
    QDBusConnection::sessionBus().asyncCall(
        QDBusMessage::createMethodCall(m_player, MprisPath, MprisPlayer, QStringLiteral("PlayPause")));
}

void ManagedApp::watchUnit()
{
    if (m_unit.isEmpty())
        return;
    auto bus = QDBusConnection::sessionBus();
    // systemd only sends change signals to those who asked.
    bus.asyncCall(systemdCall(QStringLiteral("Subscribe")));
    auto call = systemdCall(QStringLiteral("LoadUnit"));
    call << m_unit;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        const QDBusPendingReply<QDBusObjectPath> loaded = *finished;
        if (loaded.isError()) {
            qCWarning(lcManagedApp) << "no unit" << m_unit << ":" << loaded.error().message();
            return;
        }
        m_unitPath = loaded.value();
        QDBusConnection::sessionBus().connect(Systemd, m_unitPath.path(), Properties, QStringLiteral("PropertiesChanged"),
                                              this, SLOT(unitPropertiesChanged()));
        readUnitState();
    });
}

void ManagedApp::unitPropertiesChanged()
{
    readUnitState();
}

void ManagedApp::readUnitState()
{
    if (m_unitPath.path().isEmpty())
        return;
    auto call = getProperty(Systemd, m_unitPath.path(), SystemdUnit, QStringLiteral("ActiveState"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        const QDBusPendingReply<QDBusVariant> state = *finished;
        if (state.isError())
            return;
        const QString active = state.value().variant().toString();
        setRunning(active == u"active" || active == u"activating" || active == u"reloading");
    });
}

void ManagedApp::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    qCInfo(lcManagedApp) << m_unit << (running ? "is running" : "is not running");
    if (running) {
        m_windowPoll.start();
        checkWindow();
        findPlayer();
        watchMediaKind();
    } else {
        m_windowPoll.stop();
        m_tagged.clear();
        setWindow(0);
        setPlayer(QString());
        setMediaKind(false, false);
    }
    emit runningChanged();
    if (!running && !m_stopping)
        emit exited();
    m_stopping = false;
}

void ManagedApp::checkWindow()
{
    if (!m_connection)
        return;
    // A program that opens windows as it goes (a dialog the rest of it then waits behind)
    // has every one of them tagged, or gamescope doesn't show them.
    if (m_allWindows) {
        const QList<xcb_window_t> windows = classWindows();
        for (xcb_window_t window : windows) {
            if (!m_tagged.contains(window)) {
                m_tagged.insert(window);
                tag(window);
            }
        }
        m_tagged.intersect(QSet<xcb_window_t>(windows.cbegin(), windows.cend()));
    }
    if (m_window) {
        // Still there?
        auto attributes = reply(xcb_get_window_attributes_reply(
            m_connection, xcb_get_window_attributes(m_connection, m_window), nullptr));
        if (attributes)
            return;
        setWindow(0);
    }
    setWindow(findWindow());
}

// The top-level windows of the program's class.
QList<xcb_window_t> ManagedApp::classWindows() const
{
    QList<xcb_window_t> windows;
    if (m_windowClass.isEmpty())
        return windows;
    auto tree = reply(xcb_query_tree_reply(m_connection, xcb_query_tree(m_connection, m_root), nullptr));
    if (!tree)
        return windows;
    const QByteArray wanted = m_windowClass.toUtf8();
    const xcb_window_t *children = xcb_query_tree_children(tree.get());
    for (int i = 0; i < xcb_query_tree_children_length(tree.get()); ++i) {
        // WM_CLASS is "instance\0class\0".
        auto property = reply(xcb_get_property_reply(
            m_connection,
            xcb_get_property(m_connection, 0, children[i], XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 0, 64), nullptr));
        if (!property || xcb_get_property_value_length(property.get()) == 0)
            continue;
        const QByteArray value(static_cast<const char *>(xcb_get_property_value(property.get())),
                               xcb_get_property_value_length(property.get()));
        if (value.split('\0').contains(wanted))
            windows.append(children[i]);
    }
    return windows;
}

xcb_window_t ManagedApp::findWindow() const
{
    xcb_window_t best = 0;
    quint32 bestArea = 0;
    const QList<xcb_window_t> windows = classWindows();
    for (xcb_window_t window : windows) {
        // The program also has tiny helper windows of the same class: take the largest.
        auto geometry = reply(xcb_get_geometry_reply(m_connection, xcb_get_geometry(m_connection, window), nullptr));
        if (!geometry)
            continue;
        const quint32 area = quint32(geometry->width) * geometry->height;
        if (area > bestArea && area > 100) {
            best = window;
            bestArea = area;
        }
    }
    return best;
}

void ManagedApp::tag(xcb_window_t window)
{
    const quint32 id = m_appId;
    const char name[] = "STEAM_GAME";
    auto atom = reply(xcb_intern_atom_reply(
        m_connection, xcb_intern_atom(m_connection, 0, sizeof(name) - 1, name), nullptr));
    if (atom) {
        xcb_change_property(m_connection, XCB_PROP_MODE_REPLACE, window, atom->atom, XCB_ATOM_CARDINAL, 32, 1, &id);
        xcb_flush(m_connection);
    }
}

void ManagedApp::setWindow(xcb_window_t window)
{
    if (m_window == window)
        return;
    m_window = window;
    if (window) {
        tag(window);
        qCInfo(lcManagedApp) << m_unit << "window" << Qt::hex << window << Qt::dec << "tagged with app ID" << m_appId;
    } else {
        qCInfo(lcManagedApp) << m_unit << "window gone";
    }
    emit readyChanged();
}

void ManagedApp::findPlayer()
{
    if (m_mprisName.isEmpty() || !m_running)
        return;
    auto *bus = QDBusConnection::sessionBus().interface();
    if (!bus)
        return;
    const QStringList names = bus->registeredServiceNames();
    for (const QString &name : names) {
        if (name.startsWith(m_mprisName))
            checkPlayer(name);
    }
}

// The name is ours if its owner's process runs in our unit (systemd's GetUnitByPID).
void ManagedApp::checkPlayer(const QString &busName)
{
    if (m_unitPath.path().isEmpty())
        return;
    auto pidCall = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                                  QStringLiteral("org.freedesktop.DBus"),
                                                  QStringLiteral("GetConnectionUnixProcessID"));
    pidCall << busName;
    auto *pidWatcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(pidCall), this);
    connect(pidWatcher, &QDBusPendingCallWatcher::finished, this, [this, busName](QDBusPendingCallWatcher *pidDone) {
        pidDone->deleteLater();
        const QDBusPendingReply<quint32> pid = *pidDone;
        if (pid.isError())
            return;
        auto unitCall = systemdCall(QStringLiteral("GetUnitByPID"));
        unitCall << pid.value();
        auto *unitWatcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(unitCall), this);
        connect(unitWatcher, &QDBusPendingCallWatcher::finished, this, [this, busName](QDBusPendingCallWatcher *unitDone) {
            unitDone->deleteLater();
            const QDBusPendingReply<QDBusObjectPath> unit = *unitDone;
            if (!unit.isError() && unit.value() == m_unitPath && m_running)
                setPlayer(busName);
        });
    });
}

void ManagedApp::nameOwnerChanged(const QString &name, const QString &, const QString &newOwner)
{
    if (!m_mediaKindName.isEmpty() && name == m_mediaKindName) {
        if (newOwner.isEmpty())
            setMediaKind(false, false);
        else
            watchMediaKind();
        return;
    }
    if (m_mprisName.isEmpty() || !name.startsWith(m_mprisName))
        return;
    if (newOwner.isEmpty()) {
        if (name == m_player)
            setPlayer(QString());
    } else if (m_running) {
        checkPlayer(name);
    }
}

void ManagedApp::setPlayer(const QString &busName)
{
    if (m_player == busName)
        return;
    auto bus = QDBusConnection::sessionBus();
    if (!m_player.isEmpty()) {
        bus.disconnect(m_player, MprisPath, Properties, QStringLiteral("PropertiesChanged"), this,
                       SLOT(playerPropertiesChanged(QString, QVariantMap, QStringList)));
    }
    m_player = busName;
    if (m_player.isEmpty()) {
        m_status.clear();
        m_resumePending.stop();
        setPlaying(false);
        setPausedByUs(false);
        setMetadata(QVariant());
        return;
    }
    qCInfo(lcManagedApp) << m_unit << "plays through" << m_player;
    bus.connect(m_player, MprisPath, Properties, QStringLiteral("PropertiesChanged"), this,
                SLOT(playerPropertiesChanged(QString, QVariantMap, QStringList)));
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(getAll(m_player, MprisPath, MprisPlayer)), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        const QDBusPendingReply<QVariantMap> properties = *finished;
        if (properties.isError())
            return;
        setStatus(properties.value().value(QStringLiteral("PlaybackStatus")).toString());
        setMetadata(properties.value().value(QStringLiteral("Metadata")));
    });
}

void ManagedApp::playerPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &)
{
    if (interface != MprisPlayer)
        return;
    const auto status = changed.constFind(QStringLiteral("PlaybackStatus"));
    if (status != changed.constEnd())
        setStatus(status->toString());
    const auto metadata = changed.constFind(QStringLiteral("Metadata"));
    if (metadata != changed.constEnd())
        setMetadata(*metadata);
}

void ManagedApp::setStatus(const QString &status)
{
    m_status = status;
    setPlaying(status == u"Playing");
    if (status == u"Paused" && m_resumePending.isActive()) {
        m_resumePending.stop();
        play();
    }
}

void ManagedApp::setMetadata(const QVariant &metadata)
{
    const QVariantMap map = toMap(metadata);
    const QString title = map.value(QStringLiteral("xesam:title")).toString();
    // A web app's page frozen while away forgets what it was playing; the top bar still
    // shows the video that leaving paused.
    if (title.isEmpty() && m_pausedByUs)
        return;
    QStringList artists = toStrings(map.value(QStringLiteral("xesam:artist")));
    artists.removeAll(QString());
    const QString artist = artists.join(QStringLiteral(", "));
    if (title == m_title && artist == m_artist)
        return;
    m_title = title;
    m_artist = artist;
    emit metadataChanged();
}

void ManagedApp::watchMediaKind()
{
    if (m_mediaKindName.isEmpty() || !m_running)
        return;
    auto bus = QDBusConnection::sessionBus();
    // Connecting twice to the same signal is refused by Qt, so this may run again.
    bus.connect(m_mediaKindName, MediaKindPath, Properties, QStringLiteral("PropertiesChanged"), this,
                SLOT(mediaKindPropertiesChanged(QString, QVariantMap, QStringList)));
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(getAll(m_mediaKindName, MediaKindPath, MediaKindInterface)), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        const QDBusPendingReply<QVariantMap> properties = *finished;
        if (!properties.isError())
            readMediaKind(properties.value());
    });
}

void ManagedApp::mediaKindPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &)
{
    if (interface == MediaKindInterface)
        readMediaKind(changed);
}

void ManagedApp::readMediaKind(const QVariantMap &properties)
{
    setMediaKind(properties.value(QStringLiteral("Background"), m_background).toBool(),
                 properties.value(QStringLiteral("CoverOnly"), m_coverOnly).toBool());
}

void ManagedApp::setMediaKind(bool background, bool coverOnly)
{
    if (m_background == background && m_coverOnly == coverOnly)
        return;
    m_background = background;
    m_coverOnly = coverOnly;
    qCInfo(lcManagedApp) << m_unit << (background ? "keeps playing when left" : "pauses when left")
                         << (coverOnly ? "(cover only)" : "");
    emit mediaKindChanged();
}

void ManagedApp::setPlaying(bool playing)
{
    if (m_playing == playing)
        return;
    m_playing = playing;
    if (playing)
        setPausedByUs(false);
    qCInfo(lcManagedApp) << m_unit << (playing ? "is playing" : "is not playing");
    emit playingChanged();
}
