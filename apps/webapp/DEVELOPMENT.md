# webapp: development

A program that shows one website full screen on the TV, as a program of its own that the
shell starts and shows like Jellyfin and Eden (`../../shell/DEVELOPMENT.md`, Managed apps).
What a site needs beyond a browser (an address, a user agent, scripts that make it fit a TV
and tell the program what plays) lives in a folder of its own under `sites/`; the program
itself knows no site by name. YouTube (`sites/youtube/`) is the first.

It is a program of its own because the shell restarts with every deploy: as a separate
service a site keeps running, signed in and where it was, while the shell comes and goes.

## The parts

| File | What |
|---|---|
| `main.cpp` | Reads the site folder given on the command line, names the program after it, sets the scale, starts QML |
| `Main.qml` | The window: one `WebEngineView`, the site's hooks, the web channel, hiding, the controller |
| `Site.*` | The site's `site.json` |
| `SiteSettings.*` | The site's settings file, which the shell writes; watched for changes |
| `OnScreen.*` | Whether gamescope shows this program's window |
| `ShellClient.*` | The Back key, and asking the shell to take the screen back |
| `MediaKind.*` | Whether what plays keeps playing when the app is left, for the shell, on the session bus |
| `host-main.js` | Injected into every site, in the page's world: the default hooks |
| `host-app.js` | Injected into every site, in the program's world: the `host` object and its helpers |
| `sites/<name>/` | One site: `site.json` and its scripts |
| `webapp@.service` | The user service, one instance per site: `webapp@youtube.service` |

From `../../shared`: `TvShared.Input` (`GamepadInput`, `FreshKeys`).

## A site

`sites/<name>/site.json`:

```json
{
    "name": "YouTube",
    "url": "https://www.youtube.com/tv",
    "userAgent": "…",
    "scripts": [
        { "file": "mediacaps.js", "world": "main", "at": "creation" },
        { "file": "youtube.js", "world": "main", "at": "creation" },
        { "file": "sponsorskip.js", "world": "application", "at": "ready" }
    ],
    "fetch": ["https://sponsor.ajay.app/api/"]
}
```

- `world`: `main` is the page's own JavaScript world (it reaches the page's objects, and the
  page sees what it defines); `application` is the program's world, which shares only the
  DOM with the page.
- `at`: `creation` runs before the page's own scripts, `ready` once the document is there.
  The program's own scripts come first, then the site's in the order listed.
- `fetch`: address prefixes the site's scripts may ask the program to fetch (below).
- The profile (cookies, storage) is named after the folder and kept in
  `~/.local/share/webapp/<name>/`; the cache in `~/.cache/webapp-<name>/`.

### What a site's scripts may define and use

In the page's world, `host-main.js` defines `globalThis.webapp` with defaults, and a site's
script replaces what it knows better (`Object.assign(webapp, {...})`):

- `media()` → `{ playing, kind, coverOnly, mark }`, asked once a second while the page runs.
  `kind` is what is loaded: `"song"` (a song showing only its cover), `"music-video"`,
  `"podcast"`, `"video"`, or `""` for nothing; the shell's Settings say which kinds keep
  playing when the app is left. `coverOnly`: nothing on screen but a cover. `mark` is the
  site's own name for the kind, only for the log. Default: playing while a `<video>` or
  `<audio>` plays, `"video"` if there is one.
- `back()` → `"page"` (the page gets Escape, which TV web apps take as back) or `"leave"`
  (the program asks the shell to take the screen back). Default: always `"page"`.
- `play()` → whether there was something to play: continue what leaving paused (the
  shell's `Resume`, below). Default: the first paused `<video>` or `<audio>`.

Title and artist aren't asked for: a site sets them in `navigator.mediaSession` (YouTube
does), and Chromium's player passes them on (Playing, below).

In the program's world, `host-app.js` connects to the program over a web channel and gives
the site's scripts there:

- `webappSettings(callback)`: `callback(settings)` with the site's settings, now and on
  every change. A list saved with one entry reads back as a string, an empty one as null.
- `webappFetch(url)` → a promise of `{ status, text }`: a GET made by the program, for the
  prefixes in `fetch`. A page's own rules may forbid it to reach other servers (YouTube's
  `connect-src` does), and the program's world is held to them too.
- `webappHost`: a promise of the `host` object itself.

The web channel lives only in the program's world: the page can't see it or call through
it. The page-world hooks are called with `runJavaScript` instead, since they have to reach
the page's own objects (YouTube's player).

Lines a script logs with `console.log("webapp: …")` go to the program's log.

## How it works

- **Window and screen.** An X11 window, full screen; its `WM_CLASS` is `webapp-<name>`
  (both parts), which is how the shell finds it. The shell tags it with the app's gamescope
  ID and hands it the screen (`BaseLayer.frontApp`). `OnScreen` compares the window's own
  `STEAM_GAME` with gamescope's `GAMESCOPE_FOCUSED_APP`, so the program knows when it is
  shown without asking the shell. Sizes are for a screen 1440 pixels high, as in the shell,
  so a page sees the same CSS size it saw as the shell's page.
- **Keys.** The remote and keyboards reach the window directly. The controller is read with
  `GamepadInput` while the window is on screen. `FreshKeys` is armed each time it comes on
  screen, so the key that opened it doesn't act in it. `ShellClient` takes Back before the
  page sees it and asks the site's `back()`: Escape to the page, or the shell's `LeaveApp`
  (`busctl --user call local.tvshell /local/tvshell local.tvshell LeaveApp`). The page's own
  exit (`window.close()`) also calls `LeaveApp`.
- **Playing.** Qt WebEngine registers an MPRIS player of its own for the process,
  `org.mpris.MediaPlayer2.chromium.instance<pid>`, with the application name as its
  `Identity`. It follows the page's media session: `PlaybackStatus`, `Metadata` (title,
  artist, cover, length), `Position`, and `Play`, `Pause`, `Next` and the rest go to the
  page. That is how the shell sees what plays and how it pauses. What MPRIS has no place
  for goes on `MediaKind` (`local.webapp.<name>`, object `/local/webapp`): `Background`
  (what is loaded keeps playing when the app is left), `CoverOnly`, and `Kind`.
- **Continuing.** When the shell opens the app again after leaving paused it, it calls
  `MediaKind`'s `Resume()` rather than MPRIS Play, so the site decides what continuing
  means. The program calls the site's `play()` every half second once the page is visible,
  until it says it played or ten seconds have passed.
- **Background.** `backgroundKinds` in the site's settings file lists the kinds that keep
  playing when the app is left (written by the shell's Settings; without the key: songs,
  music videos, podcasts). `Background` is true when the loaded kind is one of them; the
  shell then doesn't pause it when it leaves the app.
- **Suspending.** A page left with nothing playing stays open, as if on screen, for
  `suspendAfter` seconds (the web app suspend time in the shell's Settings, Apps, the same
  for every web app; without the key half an hour; 0 = never). Then it is suspended: hidden (the view's
  `visible`), which Chromium throttles (no drawing, slow timers). Shown again, YouTube
  reloads its video at the last point it seeked to. It is never frozen: YouTube woken from
  a freeze goes back to "Who's watching?" and loses the open video, and a frozen page says
  `Stopped` with no title on MPRIS. Something that keeps playing when left stays visible
  while paused, so play in the shell's top bar can continue it; after ten minutes paused
  that way it counts as over: `Background` goes false, which takes it out of the shell's
  top bar.
- **Settings.** The shell writes `~/.config/tvshell/apps/webapp.conf`, what every web app
  reads (`suspendAfter`), and `~/.config/tvshell/apps/<name>.conf`, the site's own, whose
  keys win (INI); `SiteSettings` reads both and watches them and their folder (the shell
  replaces a file when it saves). The program uses `backgroundKinds` and `suspendAfter` itself and hands all of it
  to the site's scripts.
- **Crashes.** A page process that ends is reloaded.

## YouTube (`sites/youtube/`)

`youtube.com/tv`, the interface smart TVs use.

- **User agent.** In the format of YouTube's TV app platform (Cobalt), naming the device as
  it is (Valve, SteamMachine): without a TV user agent `/tv` redirects to the desktop site,
  and YouTube picks the app's quality tier from it (this one gets
  `TV_APP_QUALITY_FULL_ANIMATION`; the log, "YouTube").
- **Resolution** (`mediacaps.js`, page world, before the page's scripts). YouTube's TV
  player asks `MediaSource.isTypeSupported()` about width, height, frame rate, bitrate and
  more, including trick questions (`width=99999`, `eotf=catavision`) that a TV's browser
  answers with no. Chromium ignores those parameters and says yes to all of it, so the
  player distrusts every answer and stays at 720p. The script answers them: up to the
  screen's real resolution, 60 frames a second, standard dynamic range only. With it the
  player offers up to 2160p.
- **What plays** (`youtube.js`, page world). `media()` looks at the main video
  (`video.html5-main-video`) while one is open (`#/watch`). The kind comes from YouTube's
  own mark, the player's `getPlayerResponse().videoDetails.musicVideoType`: `_ATV` (a song
  showing only its cover) and `_PRIVATELY_OWNED_TRACK` are `song`; `_OMV`, `_UGC` and
  `_OFFICIAL_SOURCE_MUSIC` are `music-video`; `_PODCAST_EPISODE` is `podcast`. With no
  mark (or one not listed), a video for which the player offers "Switch to audio" is a
  `music-video`: YouTube offers it for music it hasn't marked, such as a track uploaded as
  an ordinary video. That is the action `TRANSPORT_CONTROLS_BUTTON_TYPE_MUSIC_DISPLAY` in
  the player's actions, read from the data of `ytlr-player-actions-container` in the page
  (`__instance.props.data.settingActions`; there with the controls hidden too). The TV
  page's player response has no category or keywords to go by. Anything else is `video`.
  `_ATV` is also `coverOnly`.
- **Back** (`youtube.js`). The account picker ("Who's watching?") has the same address as
  YouTube's home (`/tv#/`), so `back()` looks for a visible `ytlr-account-selector`: there
  Back leaves, anywhere else YouTube gets Escape, its own back. YouTube's own Exit calls
  `window.close()`.
- **SponsorBlock** (`sponsorskip.js`, program world). It reads the video from the address
  (`#/watch?v=<id>`), asks sponsor.ajay.app for its segments by the first four hex
  characters of the ID's SHA-256 (the service's privacy mode), for the categories in the
  settings (`skipCategories`), through `webappFetch` (YouTube's `connect-src` doesn't allow
  sponsor.ajay.app), and keeps this video's entries. Four times a second it checks the main
  `<video>`, seeks past a segment and shows a note. Our own script rather than the
  SponsorBlock browser extension (Qt 6.10+ can load extensions): that one is written for
  the desktop site.
- Sign-in: YouTube shows a code to enter at youtube.com/activate on a phone.

## Open

- Stopping the program once ended in "double free or corruption" on the way out (the log,
  "Building it"): something is torn down in the wrong order at exit.
- For about two seconds after a YouTube video opens, the player's actions aren't there yet:
  a music video without a mark counts as an ordinary video until then, and leaving it in
  that time pauses it.

- Which page Chromium's player follows when a site opens more than one (one player per
  process). YouTube has one.
- The double press that jumps to the end of a row (`../../shell/DEVELOPMENT.md`, Next) would
  live here, in a site's scripts or in `Main.qml`.

## Build, deploy, debug

`./build.sh` builds in the toolchain image (`tv/toolchains/steamos-3.9:dev`), with the
repository mounted so `../../shared` is found; output in `build/`. `./deploy.sh` copies the
program and `sites/` to `~/apps/webapp/` on the Steam Machine, installs `webapp@.service`
and restarts running sites; the shell starts a site from its tile. The first deploy moved
YouTube's profile from the shell's folder (`~/.local/share/tvshell/QtWebEngine/youtube`).

- `./deploy.sh --log youtube` follows a site's log.
- A start without a display, to catch QML errors (`QT_FORCE_STDERR_LOGGING=1`, or Qt logs to
  the journal):

      docker run --rm --network none -u "$(id -u):$(id -g)" -e HOME=/tmp \
          -e QT_QPA_PLATFORM=offscreen -e QTWEBENGINE_DISABLE_SANDBOX=1 -e QT_FORCE_STDERR_LOGGING=1 \
          -v "$PWD/build:/out:ro" -v "$PWD/sites:/sites:ro" \
          tv/toolchains/steamos-3.9:dev timeout 10 /out/webapp /sites/youtube

- `QTWEBENGINE_REMOTE_DEBUGGING=127.0.0.1:9222` (a drop-in for the site's unit) opens
  Chromium's DevTools on gabecube; reach it with `ssh -L 9222:127.0.0.1:9222`. It exposes
  the signed-in session: remove it when done.
- Logging categories: `webapp.screen`, `webapp.shell`, `webapp.media`, `webapp.settings`,
  `tv.gamepad`; `qml` for `Main.qml` (`page visible`, `page hidden`, `loaded: …`, `continued`).
