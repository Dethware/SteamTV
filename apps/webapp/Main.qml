pragma ComponentBehavior: Bound

import QtQuick
import QtWebChannel
import QtWebEngine
import TvShared.Input
import WebApp

// One site, full screen (DEVELOPMENT.md, "How it works"). The page is told what it needs
// through functions in it: webapp.media() and webapp.back() in its own world (host-main.js
// and the site's scripts), and the `host` object over a web channel in the program's world,
// which the page can't see (host-app.js).
Window {
    id: root

    required property Site site
    required property SiteSettings settings
    required property MediaKind mediaKind

    visible: true
    visibility: Window.FullScreen
    color: "black"
    title: site.title

    // gamescope shows us (always, outside gamescope).
    readonly property bool shown: onScreen.onScreen || !onScreen.available
    // Until the shell first shows the window, the page loads as if it were shown.
    property bool everShown: false
    // From webapp.media(), once a second while the page runs.
    property bool playing: false
    // "song", "music-video", "podcast", "video", or "" for nothing loaded.
    property string kind: ""
    property bool coverOnly: false
    // The site's own name for what is loaded, for the log (YouTube's mark).
    property string mark: ""
    onMarkChanged: console.info("loaded:", kind.length > 0 ? kind : "nothing", mark.length > 0 ? "(" + mark + ")" : "")
    // The kinds that keep playing when the app is left, from the shell's Settings
    // (backgroundKinds in the site's settings file; a list of one reads back as a string).
    readonly property var backgroundKinds: "backgroundKinds" in settings.values
                                           ? [].concat(settings.values.backgroundKinds || [])
                                           : ["song", "music-video", "podcast"]
    readonly property bool playsOn: kind.length > 0 && backgroundKinds.includes(kind)
    // Left paused in the background for a while: its session is over.
    property bool parked: false
    // The shell asked to continue what leaving paused (MediaKind's Resume).
    property bool resumeWanted: false
    // Seconds a page left with nothing playing stays open before it is suspended (the shell's
    // Settings, Apps, "Web app suspend time": suspendAfter in webapp.conf); 0 = never.
    readonly property int suspendAfter: "suspendAfter" in settings.values ? Number(settings.values.suspendAfter) : 1800
    // Left with nothing playing for suspendAfter.
    property bool suspended: false
    // The page counts as visible to itself, as if on screen. Suspended, it is hidden, which
    // Chromium throttles (no drawing, slow timers), unless something plays or what is loaded
    // plays on in the background and is only paused (a press of play in the shell can
    // resume it). Woken from hiding, YouTube reloads its video at the last point it seeked
    // to; never frozen: woken from a freeze, it went back to "Who's watching?" and lost the
    // video (the log, "YouTube moves out of the shell").
    readonly property bool pageVisible: !suspended || playing || (playsOn && !parked)

    onShownChanged: {
        if (shown) {
            everShown = true
            parked = false
            suspended = false
        } else {
            resumeWanted = false
        }
    }

    Connections {
        target: root.mediaKind
        function onResumeRequested() {
            console.info("asked to continue")
            root.resumeWanted = true
        }
    }

    // Continue once the page is visible and has something to play: right after it is shown
    // again, YouTube's player takes a moment. Given up after ten seconds.
    Timer {
        interval: 500
        repeat: true
        running: root.resumeWanted && root.pageVisible && !view.loading
        onTriggered: view.runJavaScript("globalThis.webapp && webapp.play ? webapp.play() : false", played => {
            if (played === true) {
                console.info("continued")
                root.resumeWanted = false
            }
        })
    }

    Timer {
        interval: 10 * 1000
        running: root.resumeWanted
        onTriggered: {
            console.info("nothing to continue")
            root.resumeWanted = false
        }
    }
    onPlayingChanged: {
        if (playing) {
            parked = false
            suspended = false
        }
    }

    Timer {
        interval: Math.max(1, root.suspendAfter) * 1000
        running: root.everShown && !root.shown && !root.playing && root.suspendAfter > 0 && !root.suspended
        onTriggered: {
            console.info("suspended")
            root.suspended = true
        }
    }
    onPageVisibleChanged: console.info(pageVisible ? "page visible" : "page hidden")

    // What the shell sees besides MPRIS. Something parked counts as over.
    Binding {
        target: root.mediaKind
        property: "background"
        value: root.playsOn && !root.parked
    }
    Binding {
        target: root.mediaKind
        property: "coverOnly"
        value: root.coverOnly
    }
    Binding {
        target: root.mediaKind
        property: "kind"
        value: root.kind
    }

    Timer {
        interval: 10 * 60 * 1000
        running: !root.shown && !root.playing && root.playsOn && !root.parked
        onTriggered: root.parked = true
    }

    Timer {
        interval: 1000
        repeat: true
        running: true
        onTriggered: view.runJavaScript("globalThis.webapp ? webapp.media() : null", state => {
            root.playing = state ? state.playing === true : false
            root.kind = state && typeof state.kind === "string" ? state.kind : ""
            root.mark = state && typeof state.mark === "string" ? state.mark : ""
            root.coverOnly = state ? state.coverOnly === true : false
        })
    }

    OnScreen {
        id: onScreen
        window: root
    }

    // Back asks the site first: on its first screen it means leaving.
    ShellClient {
        id: shell
        window: root
        onBackPressed: view.runJavaScript("globalThis.webapp ? webapp.back() : 'page'", answer => {
            if (answer === "leave")
                shell.leave()
            else
                shell.sendEscape()
        })
    }

    GamepadInput {
        window: root
        enabled: root.shown && root.active
    }

    // The key that got us on screen is still down: its repeats and release aren't ours.
    FreshKeys {
        window: root
        armed: root.shown
    }

    // What the site's scripts in the program's world can ask for (host-app.js).
    QtObject {
        id: host

        WebChannel.id: "host"

        property var settings: root.settings.values

        signal got(int requestId, int status, string text)

        // A GET for one of the addresses site.json allows; the answer comes as got().
        function get(requestId, url) {
            const target = String(url)
            if (!root.site.fetch.some(prefix => target.startsWith(prefix))) {
                console.warn("not allowed to fetch", target)
                host.got(requestId, 0, "")
                return
            }
            const request = new XMLHttpRequest()
            request.onreadystatechange = () => {
                if (request.readyState === XMLHttpRequest.DONE)
                    host.got(requestId, request.status, request.responseText)
            }
            request.open("GET", target)
            request.send()
        }
    }

    WebChannel {
        id: channel
        registeredObjects: [host]
    }

    WebEngineProfilePrototype {
        id: siteProfile

        storageName: root.site.name
        persistentStoragePath: root.site.storagePath
        persistentCookiesPolicy: WebEngineProfile.ForcePersistentCookies
    }

    WebEngineView {
        id: view

        anchors.fill: parent
        visible: root.pageVisible
        focus: true
        backgroundColor: "black"
        settings.playbackRequiresUserGesture: false
        settings.showScrollBars: false
        webChannel: channel
        webChannelWorld: WebEngineScript.ApplicationWorld

        // No browser menu (Back, Reload, View page source…): none of it belongs on the TV.
        onContextMenuRequested: request => request.accepted = true
        // The site's own exit.
        onWindowCloseRequested: shell.leave()
        onJavaScriptConsoleMessage: (level, message) => {
            if (message.startsWith("webapp:"))
                console.info("page:", message.substring(7).trim())
        }
        onRenderProcessTerminated: (status, exitCode) => {
            console.warn("page process ended:", status, exitCode, "- reloading")
            reload()
        }

        Component.onCompleted: {
            profile = siteProfile.instance()
            if (root.site.userAgent.length > 0)
                profile.httpUserAgent = root.site.userAgent
            const own = [{
                name: "qwebchannel",
                sourceUrl: "qrc:///qtwebchannel/qwebchannel.js",
                injectionPoint: WebEngineScript.DocumentCreation,
                worldId: WebEngineScript.ApplicationWorld
            }, {
                name: "host-app",
                sourceUrl: Qt.resolvedUrl("host-app.js"),
                injectionPoint: WebEngineScript.DocumentCreation,
                worldId: WebEngineScript.ApplicationWorld
            }, {
                name: "host-main",
                sourceUrl: Qt.resolvedUrl("host-main.js"),
                injectionPoint: WebEngineScript.DocumentCreation,
                worldId: WebEngineScript.MainWorld
            }]
            userScripts.collection = own.concat(root.site.scripts)
            url = root.site.url
        }
    }
}
