import QtQuick
import TvShell.Apps
import TvShell.Assistant
import TvShell.Audio
import TvShell.Common
import TvShell.Controllers
import TvShell.Photos
import TvShell.Power
import TvShell.Session
import TvShell.Steam
import TvShared.Input
import TvShell.YouTube

Window {
    id: root

    visible: true
    visibility: Window.FullScreen
    color: "#101418"
    title: qsTr("tvshell")

    // "home", "settings", "photos" (an album's or memory's grid) or "viewer".
    property string page: "home"
    // When the shell stepped aside for Quick Access (ms timestamp, 0 = not pending).
    property double quickAccessHandoff: 0
    // Where leaving YouTube goes back to: the page it was opened from, or Steam if Steam had
    // the screen when the remote's YouTube key opened it.
    property string youtubeReturnPage: "home"
    property bool youtubeReturnToSteam: false
    // The managed app that has the screen in place of the shell (YouTube, Jellyfin, a Switch
    // game in Eden), or null.
    property ManagedApp shownApp: null
    readonly property var apps: [youtube, jellyfin, eden]

    // Show a managed app, starting it if needed and continuing it if leaving it paused it.
    // The shell stays on screen until its window is there. What plays in the background goes
    // on until the app plays something itself (onlyOnePlaying).
    function openApp(app) {
        app.start()
        app.resume()
        shownApp = app
    }

    // Back to the shell. What plays on in the background (by the Settings: music, podcasts)
    // carries on; anything else pauses. What
    // happens to the app then is up to Settings (Apps): it stays, or it is closed after a
    // while; a game stays parked as it is until it is opened again or closed.
    function leaveApp() {
        if (!shownApp)
            return
        const app = shownApp
        if (!app.background)
            app.pauseWhileAway()
        shownApp = null
    }

    // One thing plays at a time: when an app starts playing, the others pause (for good: they
    // don't continue by themselves when opened again).
    function onlyOnePlaying(app) {
        for (const other of apps) {
            if (other !== app && other.playing)
                other.pause()
        }
    }

    function openYouTube() {
        if (shownApp === youtube)
            return
        youtubeReturnPage = page
        // Coming from another app the shell wasn't on screen, but Steam didn't have it either.
        youtubeReturnToSteam = baseLayer.available && !baseLayer.onScreen && shownApp === null
        leaveApp()
        openApp(youtube)
    }

    // YouTube's own way back (Back on its account picker, its Exit): to where it was opened
    // from.
    function leaveYouTube() {
        leaveApp()
        showPage(youtubeReturnPage)
        if (youtubeReturnToSteam)
            baseLayer.claim = false
    }

    function showPage(name) {
        page = name
        if (name === "settings")
            settingsPage.forceActiveFocus()
        else if (name === "photos")
            photoGrid.forceActiveFocus()
        else if (name === "viewer")
            photoViewer.forceActiveFocus()
        else
            home.forceActiveFocus()
    }

    function openAlbum(albumId, name) {
        immich.loadAlbum(albumId)
        photoGrid.title = name
        photoGrid.assets = Qt.binding(() => immich.albumAssets)
        photoGrid.loading = Qt.binding(() => immich.albumLoading)
        photoGrid.currentIndex = 0
        showPage("photos")
    }

    function openMemory(memory) {
        photoGrid.title = memory.title
        photoGrid.assets = memory.assets
        photoGrid.loading = false
        photoGrid.currentIndex = 0
        showPage("photos")
    }

    function openTile(appId) {
        if (appId === "youtube")
            openYouTube()
        else if (appId === "jellyfin")
            openApp(jellyfin)
        else if (appId === "ringfit")
            openApp(eden)
    }

    // Close ends the program the way it ends itself; force close is for one that doesn't.
    function closeTile(appId, force) {
        const app = appId === "youtube" ? youtube : appId === "jellyfin" ? jellyfin : appId === "ringfit" ? eden : null
        if (!app)
            return
        if (force)
            app.kill()
        else
            app.quit()
    }

    // Starting a game from the shell. With a Switch game parked: ask first whether to close
    // it (it holds half the machine's memory). With music playing in the background: ask.
    property string pendingGame
    property string pendingGameName

    function launchGame(appId, name) {
        pendingGame = appId
        pendingGameName = name
        if (eden.running) {
            parkedDialog.title = qsTr("Start %1?").arg(name)
            parkedDialog.open()
        } else {
            launchPendingGame()
        }
    }

    function launchPendingGame() {
        if (youtube.playing && youtube.background) {
            gameDialog.title = qsTr("Start %1?").arg(pendingGameName)
            gameDialog.open()
        } else {
            steamLibrary.launch(pendingGame)
        }
    }

    function openPowerDialog() {
        // Not over a running game for now.
        if (baseLayer.gameRunning)
            return
        idle.wake()
        baseLayer.claim = true
        powerDialog.open()
    }

    // Steam button / Quick Access → Steam gets the screen. Home → back to the shell.
    // Power: short → screen off (again → on); held → power dialog.
    BaseLayer {
        id: baseLayer
        window: root
        // The shell itself takes the screen for the screensaver, screen off and the power
        // dialog; the app gets it back afterwards.
        frontApp: root.shownApp !== null && root.shownApp.ready && idle.state === IdleController.Active
                  && !powerDialog.visible ? root.shownApp.appId : 0
        onSteamMenuOpened: claim = false
        // Home: back to the shell, where it was left. Pressed there: to the Home place.
        onHomePressed: {
            const alreadyHome = onScreen && root.page === "home" && !powerDialog.visible
                              && idle.state === IdleController.Active
            idle.wake()
            claim = true
            if (powerDialog.visible)
                powerDialog.close()
            if (gameDialog.visible)
                gameDialog.close()
            root.leaveApp()
            root.showPage("home")
            if (alreadyHome) {
                topBar.currentTab = 0
                homePlace.forceActiveFocus()
            }
        }
        // The remote's YouTube key: YouTube, from anywhere (not over a running game for now).
        onYoutubePressed: {
            if (gameRunning)
                return
            idle.wake()
            if (powerDialog.visible)
                powerDialog.close()
            root.openYouTube()
            claim = true
        }
        // The remote's Netflix key: Jellyfin, from anywhere (not over a running game for now).
        onNetflixPressed: {
            if (gameRunning)
                return
            idle.wake()
            if (powerDialog.visible)
                powerDialog.close()
            if (gameDialog.visible)
                gameDialog.close()
            if (root.shownApp === jellyfin)
                return
            root.leaveApp()
            root.showPage("home")
            root.openApp(jellyfin)
            claim = true
        }
        onMutePressed: systemVolume.toggleMute()
        onPowerPressed: {
            if (powerDialog.visible)
                powerDialog.close()
            else
                idle.togglePower()
        }
        onPowerHeld: root.openPowerDialog()
        onInputActivity: idle.activity()
    }

    // The TV is just a screen: off when the screen sleeps, on when it wakes.
    TvPower {
        on: !idle.screenSleeping
    }

    IdleController {
        id: idle
        // Also over a managed app: a paused film or an untouched menu gets the screensaver too.
        counting: baseLayer.onScreen || baseLayer.frontAppOnScreen || !baseLayer.available
        // Foreground activity: a video that is playing, wherever it plays. Not a music track
        // that only shows its cover; that is background (audioPlaying).
        watching: photoViewer.playing || (root.shownApp !== null && root.shownApp.playing && !root.shownApp.coverOnly)
        // What plays on in the background (music, podcasts; shown or not): screensaver and a
        // black screen as usual, but
        // the sound's signal stays and the machine doesn't sleep.
        audioPlaying: root.apps.some(app => app.playing && app.background)
        // The machine sleeps only once the remote has let go: then the remote can wake it.
        remoteConnected: remoteLink.connected
        onAboutToSleep: {
            for (const app of root.apps)
                app.pause()
        }
        // Screen off over something playing (the remote's Power): it pauses. Music goes on.
        onStateChanged: {
            if (state !== IdleController.Active && root.shownApp !== null && !root.shownApp.background)
                root.shownApp.pause()
        }
    }

    // YouTube, a web app (apps/webapp) shown in place of the shell like Jellyfin. Its player
    // is Chromium's; whether what it plays keeps playing when left comes from the web app,
    // which decides by the kinds chosen in Settings (YouTube).
    ManagedApp {
        id: youtube
        unit: "webapp@youtube.service"
        windowClass: "webapp-youtube"
        appId: 0x7E000004
        mprisName: "org.mpris.MediaPlayer2.chromium.instance"
        mediaKindName: "local.webapp.youtube"
        onPlayingChanged: if (playing) root.onlyOnePlaying(youtube)
        onExited: {
            if (root.shownApp === youtube)
                root.shownApp = null
        }
    }

    // Jellyfin Desktop, a separate program (apps/jellyfin-desktop) shown in place of the shell.
    ManagedApp {
        id: jellyfin
        unit: "jellyfin-desktop.service"
        windowClass: "jellyfin-desktop"
        appId: 0x7E000002
        mprisName: "org.mpris.MediaPlayer2.JellyfinDesktop"
        onPlayingChanged: if (playing) root.onlyOnePlaying(jellyfin)
        // Quit from its own menu, or crashed: the shell is back by itself.
        onExited: {
            if (root.shownApp === jellyfin)
                root.shownApp = null
        }
    }

    // Eden with a Switch game (apps/eden), shown in place of the shell like Jellyfin. Left
    // with Home it is paused and stays: a parked game, continued from its tile. It opens
    // windows as it goes (its on-screen keyboard), so all of them are tagged.
    ManagedApp {
        id: eden
        unit: "eden.service"
        windowClass: "eden"
        appId: 0x7E000003
        mprisName: "org.mpris.MediaPlayer2.eden"
        allWindows: true
        alwaysResume: true
        onPlayingChanged: if (playing) root.onlyOnePlaying(eden)
        onExited: {
            if (root.shownApp === eden)
                root.shownApp = null
        }
    }

    // The Joy-Cons and the Pro Controller, as the controller service reports them.
    ControllerClient {
        id: controllerClient
    }

    // Back at the top level of a managed app: it asks for the shell, like Home. YouTube goes
    // back to where it was opened from.
    ShellService {
        onLeaveAppRequested: {
            if (root.shownApp === youtube)
                root.leaveYouTube()
            else
                root.leaveApp()
        }
        // Home on a Joy-Con does what Home on the remote does.
        onControllerHomePressed: baseLayer.homePressed()
    }

    // An app that is loaded, not shown and not playing closes after the time set in
    // Settings (Apps), counted from when it was left or stopped playing. Also one found
    // running when the shell started.
    Timer {
        interval: Math.max(1, appSettings.closeAfter.youtube) * 1000
        running: appSettings.closeAfter.youtube > 0 && youtube.running && root.shownApp !== youtube && !youtube.playing
        onTriggered: youtube.quit()
    }

    Timer {
        interval: Math.max(1, appSettings.closeAfter.jellyfin) * 1000
        running: appSettings.closeAfter.jellyfin > 0 && jellyfin.running && root.shownApp !== jellyfin && !jellyfin.playing
        onTriggered: jellyfin.quit()
    }

    PowerActions {
        id: powerActions
    }

    // Whether the remote holds its Bluetooth connection, for the idle chain.
    RemoteLink {
        id: remoteLink
    }

    // The remote's Assistant button and microphone. Only shown for now.
    RemoteAssistant {
        id: assistant
        onListeningChanged: if (listening) idle.wake()
    }

    // Quick Access gives no X11 signal; Steam's log is the only trace of it. Stepping aside moves
    // the focus to Steam, which sometimes closes Quick Access again about a second later
    // (seen once of three tries): re-open it once if that happens.
    SteamLog {
        onQuickAccessOpened: {
            if (baseLayer.onScreen && !baseLayer.gameRunning) {
                root.quickAccessHandoff = Date.now()
                baseLayer.claim = false
            }
        }
        onQuickAccessClosed: {
            if (root.quickAccessHandoff > 0 && Date.now() - root.quickAccessHandoff < 2000)
                baseLayer.toggleQuickAccess()
            root.quickAccessHandoff = 0
        }
    }

    GamepadInput {
        window: root
        enabled: root.active && (baseLayer.onScreen || !baseLayer.available)
        // Screensaver and screen off end with a button, never with a brushed stick or trackpad.
        sticks: idle.state === IdleController.Active
        onActivity: idle.activity()
        // LB/RB: previous/next place, directly (only on the home screen, not over dialogs).
        onShoulderPressed: direction => {
            if (root.page !== "home" || powerDialog.visible || gameDialog.visible)
                return
            topBar.step(direction)
            home.currentPlace.forceActiveFocus()
        }
    }

    SteamLibrary {
        id: steamLibrary
    }

    ImmichClient {
        id: immich
    }

    ImmichMediaProxy {
        id: mediaProxy
    }


    FocusScope {
        id: home

        readonly property Item currentPlace: [homePlace, games, photos][topBar.currentTab]

        // Fresh albums and memories each time Photos is opened. (Not from the tab's own change
        // signal: its handler can run before currentPlace has followed the tab.)
        onCurrentPlaceChanged: if (currentPlace === photos) immich.refresh()

        anchors.fill: parent
        focus: true
        visible: root.page === "home"

        // The controller's Start/≡ opens the settings from anywhere on the home page.
        Keys.onMenuPressed: root.showPage("settings")
        // Back from inside a place: up to the top bar, so there's always a way out.
        Keys.onBackPressed: topBar.forceActiveFocus()
        Keys.onEscapePressed: topBar.forceActiveFocus()

        TopBar {
            id: topBar

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            tabs: [qsTr("Home"), qsTr("Games"), qsTr("Photos")]

            KeyNavigation.down: home.currentPlace
            onSettingsActivated: root.showPage("settings")

            // YouTube while it isn't shown: playing on in the background (music, say), playing
            // or paused (the web app ends it after a while paused), where OK plays and pauses
            // it; or a video that leaving paused, where OK opens YouTube to continue it.
            nowPlayingSource: "youtube"
            nowPlayingTitle: root.shownApp === youtube || !(youtube.background || youtube.pausedWhileAway) ? ""
                           : youtube.title.length > 0 ? youtube.title : qsTr("YouTube")
            nowPlayingArtist: youtube.artist
            nowPlayingPaused: !youtube.playing
            onNowPlayingActivated: {
                if (youtube.background)
                    youtube.togglePlay()
                else
                    root.openYouTube()
            }
        }

        HomePlace {
            id: homePlace

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: topBar.bottom
            anchors.bottom: parent.bottom
            visible: home.currentPlace === homePlace
            // Starts here; afterwards only Down, LB/RB, Back and Home move the focus between the
            // bar and the places, so switching tabs with OK keeps it on the bar.
            focus: true

            KeyNavigation.up: topBar
            KeyNavigation.priority: KeyNavigation.AfterItem
            runningApps: [youtube.running ? "youtube" : "", jellyfin.running ? "jellyfin" : "",
                          eden.running ? "ringfit" : ""]
            onAppOpened: appId => root.openTile(appId)
            onMenuRequested: (appId, name) => {
                tileMenu.appId = appId
                tileMenu.title = name
                tileMenu.loaded = runningApps.includes(appId)
                tileMenu.open()
            }
        }

        GamesPlace {
            id: games

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: topBar.bottom
            anchors.bottom: parent.bottom
            visible: home.currentPlace === games
            library: steamLibrary
            onLaunchRequested: (appId, name) => root.launchGame(appId, name)
            onScreen: baseLayer.onScreen || !baseLayer.available

            KeyNavigation.up: topBar
            KeyNavigation.priority: KeyNavigation.AfterItem
        }

        PhotosPlace {
            id: photos

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: topBar.bottom
            anchors.bottom: parent.bottom
            visible: home.currentPlace === photos
            immich: immich

            onLeftUpward: topBar.forceActiveFocus()
            onAlbumOpened: (albumId, name) => root.openAlbum(albumId, name)
            onMemoryOpened: memory => root.openMemory(memory)
        }
    }

    PhotoGrid {
        id: photoGrid

        anchors.fill: parent
        visible: root.page === "photos"
        immich: immich

        onClosed: root.showPage("home")
        onPhotoOpened: index => {
            photoViewer.index = index
            root.showPage("viewer")
        }
    }

    PhotoViewer {
        id: photoViewer

        anchors.fill: parent
        visible: root.page === "viewer"
        immich: immich
        mediaProxy: mediaProxy
        settings: viewerSettings
        assets: photoGrid.assets

        onClosed: {
            photoGrid.currentIndex = index
            root.showPage("photos")
        }
    }

    SettingsPage {
        id: settingsPage

        anchors.fill: parent
        visible: root.page === "settings"
        idle: idle
        screensaver: screensaverSettings
        viewer: viewerSettings
        youtube: youtubeSettings
        apps: appSettings
        immich: immich
        controllers: controllerClient

        onClosed: root.showPage("home")
    }

    ChoiceDialog {
        id: gameDialog

        anchors.fill: parent
        message: qsTr("YouTube is playing.")
        choices: [qsTr("Pause it and play"), qsTr("Keep it playing"), qsTr("Cancel")]

        onChosen: index => {
            if (index === 0)
                youtube.pause()
            if (index <= 1)
                steamLibrary.launch(root.pendingGame)
        }
        onClosed: root.showPage(root.page)
    }

    // OK held on a tile of the Home place.
    ChoiceDialog {
        id: tileMenu

        property string appId
        // The app is loaded: there is something to close.
        property bool loaded: false

        anchors.fill: parent
        choices: loaded ? [qsTr("Open"), qsTr("Close"), qsTr("Force close"), qsTr("Cancel")]
                        : [qsTr("Open"), qsTr("Cancel")]

        onChosen: index => {
            if (index === 0)
                root.openTile(appId)
            else if (loaded && index <= 2)
                root.closeTile(appId, index === 2)
        }
        onClosed: root.showPage(root.page)
    }

    ChoiceDialog {
        id: parkedDialog

        anchors.fill: parent
        message: qsTr("Ring Fit Adventure is still open.")
        choices: [qsTr("Close it and play"), qsTr("Keep it open and play"), qsTr("Cancel")]

        onChosen: index => {
            if (index === 0)
                eden.quit()
            if (index <= 1)
                root.launchPendingGame()
        }
        onClosed: root.showPage(root.page)
    }

    PowerDialog {
        id: powerDialog

        anchors.fill: parent
        actions: powerActions

        onClosed: root.showPage(root.page)
        onScreenOffRequested: idle.screenOff()
        onSleepRequested: idle.sleep()
    }

    ScreensaverSettings {
        id: screensaverSettings
    }

    ViewerSettings {
        id: viewerSettings
    }

    YouTubeSettings {
        id: youtubeSettings
    }

    AppSettings {
        id: appSettings
    }

    SystemVolume {
        id: systemVolume
        window: root
    }

    Screensaver {
        id: saver

        anchors.fill: parent
        visible: idle.state === IdleController.Screensaver
        running: visible
        immich: immich
        settings: screensaverSettings

        // The photo opens in the viewer, with the screensaver's order to browse.
        onPhotoOpened: index => {
            photoGrid.title = qsTr("Screensaver")
            photoGrid.assets = saver.assets
            photoGrid.loading = false
            photoGrid.currentIndex = index
            photoViewer.index = index
            root.page = "viewer"
            idle.wake()
        }
    }

    // The screensaver takes the keys while it runs; afterwards they go back to the page.
    Connections {
        target: idle
        function onStateChanged() {
            if (idle.state === IdleController.Screensaver)
                saver.forceActiveFocus()
            else if (saver.activeFocus)
                root.showPage(root.page)
        }
    }

    // "Screen off" while music plays keeps the signal (IdleController): black instead.
    Rectangle {
        anchors.fill: parent
        visible: idle.state === IdleController.ScreenOff
        color: "black"
    }

    AssistantIndicator {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 64
        assistant: assistant
    }

    // Last, so it draws over everything.
    VolumeIndicator {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 48
        volume: systemVolume
    }
}
