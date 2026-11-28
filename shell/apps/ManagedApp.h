#pragma once

#include <QDBusObjectPath>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <xcb/xcb.h>

// A program the shell starts itself and shows by handing it the screen (Jellyfin Desktop,
// Eden, a web app such as YouTube).
//
// The program runs as its own systemd user service, so it survives a restart of the shell;
// this class starts and stops that service, and picks a running one up again when the shell
// starts. Once the program's window exists it is tagged with the app's gamescope ID
// (STEAM_GAME), after which BaseLayer can put it on screen (BaseLayer::frontApp).
//
// What the program plays is followed over MPRIS, which is also how it is paused. Its player
// is the MPRIS name with the given prefix whose owner runs in the app's unit: several
// programs may share a prefix (every web app's is Chromium's, ending in its process ID).
class ManagedApp : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // The systemd user unit, e.g. "jellyfin-desktop.service".
    Q_PROPERTY(QString unit READ unit WRITE setUnit NOTIFY unitChanged)
    // The class of its main window (WM_CLASS), e.g. "jellyfin-desktop": how the window is found.
    Q_PROPERTY(QString windowClass READ windowClass WRITE setWindowClass NOTIFY windowClassChanged)
    // The gamescope app ID its window gets: one of BaseLayer's own IDs (BaseLayer::isOurs).
    Q_PROPERTY(quint32 appId READ appId WRITE setAppId NOTIFY appIdChanged)
    // The start of its MPRIS bus name, e.g. "org.mpris.MediaPlayer2.JellyfinDesktop" (the
    // program appends its profile).
    Q_PROPERTY(QString mprisName READ mprisName WRITE setMprisName NOTIFY mprisNameChanged)
    // A web app's own bus name (apps/webapp, MediaKind), e.g. "local.webapp.youtube": whether
    // what it plays keeps playing when left. Empty for other programs: they always pause.
    Q_PROPERTY(QString mediaKindName READ mediaKindName WRITE setMediaKindName NOTIFY mediaKindNameChanged)
    Q_PROPERTY(bool allWindows READ allWindows WRITE setAllWindows NOTIFY allWindowsChanged)
    // resume() continues it whatever paused it (a parked game: only the shell pauses it).
    Q_PROPERTY(bool alwaysResume MEMBER m_alwaysResume NOTIFY alwaysResumeChanged)

    // The service is active (or starting).
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    // Its window exists and carries the app ID: it can be shown.
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    // It is playing something right now (MPRIS PlaybackStatus "Playing").
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    // What it plays or last played (MPRIS Metadata), "" if it doesn't say.
    Q_PROPERTY(QString title READ title NOTIFY metadataChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY metadataChanged)
    // What it has loaded keeps playing when the app is left (music, say; the web app decides
    // from the Settings), and shows only a cover (nothing on screen worth keeping it awake
    // for). From mediaKindName.
    Q_PROPERTY(bool background READ background NOTIFY mediaKindChanged)
    Q_PROPERTY(bool coverOnly READ coverOnly NOTIFY mediaKindChanged)
    // Leaving it paused it (pauseWhileAway), and it hasn't played since: opening it continues.
    Q_PROPERTY(bool pausedWhileAway READ pausedWhileAway NOTIFY pausedWhileAwayChanged)

public:
    explicit ManagedApp(QObject *parent = nullptr);

    QString unit() const { return m_unit; }
    void setUnit(const QString &unit);
    QString windowClass() const { return m_windowClass; }
    void setWindowClass(const QString &windowClass);
    quint32 appId() const { return m_appId; }
    void setAppId(quint32 appId);
    QString mprisName() const { return m_mprisName; }
    void setMprisName(const QString &name);
    QString mediaKindName() const { return m_mediaKindName; }
    void setMediaKindName(const QString &name);
    bool allWindows() const { return m_allWindows; }
    void setAllWindows(bool all);

    bool running() const { return m_running; }
    bool ready() const { return m_window != 0; }
    bool playing() const { return m_playing; }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    bool background() const { return m_background; }
    bool coverOnly() const { return m_coverOnly; }
    bool pausedWhileAway() const { return m_pausedByUs; }

    // Start the service unless it runs already. `ready` follows when its window appears.
    Q_INVOKABLE void start();
    // Stop the service.
    Q_INVOKABLE void quit();
    Q_INVOKABLE void kill();
    // Pause what it plays; nothing happens if it plays nothing. It stays paused until someone
    // plays it again.
    Q_INVOKABLE void pause();
    // Pause it because it is left: resume() continues it when it is opened again.
    Q_INVOKABLE void pauseWhileAway();
    // Continue what pauseWhileAway() paused. Nothing else: what was paused another way (by
    // the user, for something else that plays, for sleep) stays paused.
    Q_INVOKABLE void resume();
    Q_INVOKABLE void togglePlay();

signals:
    void unitChanged();
    void windowClassChanged();
    void appIdChanged();
    void mprisNameChanged();
    void mediaKindNameChanged();
    void allWindowsChanged();
    void alwaysResumeChanged();
    void runningChanged();
    void readyChanged();
    void playingChanged();
    void metadataChanged();
    void mediaKindChanged();
    void pausedWhileAwayChanged();
    // The program ended on its own (quit from its menu, or crashed) while it was running.
    void exited();

private slots:
    // systemd: something about the unit changed; read its state again.
    void unitPropertiesChanged();
    // The bus: a name appeared or went away (the program's MPRIS service, its media kind).
    void nameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    // MPRIS: the player's properties changed.
    void playerPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);
    // The web app's media kind changed.
    void mediaKindPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    // systemd (session bus, org.freedesktop.systemd1): load the unit, follow its ActiveState.
    void watchUnit();
    void readUnitState();
    void setRunning(bool running);
    // X11: look for the program's main window and tag it; a known window is checked for still
    // being there. Polled while the service runs.
    void checkWindow();
    QList<xcb_window_t> classWindows() const;
    xcb_window_t findWindow() const;
    void tag(xcb_window_t window);
    void setWindow(xcb_window_t window);
    // MPRIS (session bus): find the name with our prefix whose owner runs in our unit,
    // follow PlaybackStatus and Metadata.
    void findPlayer();
    void checkPlayer(const QString &busName);
    void setPlayer(const QString &busName);
    void setPlaying(bool playing);
    // MPRIS PlaybackStatus: "Playing", "Paused" or "Stopped".
    void setStatus(const QString &status);
    void play();
    void setMetadata(const QVariant &metadata);
    void setPausedByUs(bool paused);
    // The web app's media kind (session bus): follow Music and CoverOnly.
    void watchMediaKind();
    void readMediaKind(const QVariantMap &properties);
    void setMediaKind(bool background, bool coverOnly);

    QString m_unit;
    QString m_windowClass;
    QString m_mprisName;
    QString m_mediaKindName;
    quint32 m_appId = 0;

    bool m_running = false;
    bool m_playing = false;
    // pauseWhileAway() paused it, and it hasn't played since: resume() may continue it.
    bool m_pausedByUs = false;
    QString m_status;
    // resume() came while the player wasn't back to "Paused" yet: Play once it is, if that
    // is soon.
    QTimer m_resumePending;
    bool m_alwaysResume = false;
    QString m_title;
    QString m_artist;
    bool m_background = false;
    bool m_coverOnly = false;
    // quit() was called: the service stopping is not "exited on its own".
    bool m_stopping = false;
    bool m_allWindows = false;
    // The windows tagged so far, when all of them are.
    QSet<xcb_window_t> m_tagged;
    QDBusObjectPath m_unitPath;
    xcb_connection_t *m_connection = nullptr;
    xcb_window_t m_root = 0;
    xcb_window_t m_window = 0;
    // The full bus name of the program's MPRIS service, once seen.
    QString m_player;
    QTimer m_windowPoll;
};
