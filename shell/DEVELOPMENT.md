# tvshell: development

How the shell works and how to work on it. What it does for users: [README.md](README.md).
Why things are the way they are, with dates and measurements:
[docs/gabecube-findings.md](../docs/gabecube-findings.md) (the log).

## Overview

A Qt 6.11 / QML program (C++ backend, QML UI, QML module `TvShell`). It runs inside Steam's
Gaming Mode session as an X11 client on gamescope's `:0`, next to Steam, and is started by a
user systemd unit. Only X11 windows can be put on screen in that session; `main.cpp` forces
the `xcb` platform. It uses SteamOS's own Qt (relying on Qt's ABI compatibility) and is
built in a toolchain image with the same package versions (`../toolchains/steamos-3.9`).

Sizes in QML are for a screen 1440 pixels high. `main.cpp` reads the X screen's height
(gamescope's output resolution) before the application object exists and sets
`QT_SCALE_FACTOR` to height / 1440, so everything scales as a whole (1.5 at 4K, 0.75 at
1080p); an explicit `QT_SCALE_FACTOR` wins. The factor can't change in a running program, so
when the screen's size changes the shell exits with code 75 and systemd starts it again
(`Restart=on-failure`). Images from the network aren't scaled by Qt: photo layers and tiles
ask for `sourceSize` times `Screen.devicePixelRatio`.

`Main.qml` holds the pages (`page`: `home`, `settings`, `photos`, `viewer`) and wires the
C++ objects together; each page and place is its own QML file. The apps it shows in its
place (YouTube, Jellyfin, Eden) are programs of their own (Managed apps, below); the shell
uses no web engine itself.

## Layout

The source is laid out by topic. Each folder is one QML module with its C++ and its QML,
built as a static library and linked into the one program; `main.cpp` imports their
plugins (`Q_IMPORT_QML_PLUGIN`). A QML file imports the modules whose types it uses, its
own too when it uses the module's C++ types.

| Folder | Module | What |
|---|---|---|
| (top) | `TvShell` | `main.cpp`, `Main.qml`, `TopBar.qml`, `SettingsPage.qml`: what puts the topics together |
| `common/` | `TvShell.Common` | The network layer, and controls used across topics (`ChoiceDialog`, `FitGrid`, `BarButton`, `SettingRow`) |
| `session/` | `TvShell.Session` | Taking the screen, global keys, the session bus service, Steam's log |
| `power/` | `TvShell.Power` | Idle (screensaver → screen off → sleep), the remote's connection for sleep, the power dialog |
| `audio/` | `TvShell.Audio` | The system volume and its display, what plays in the background |
| `photos/` | `TvShell.Photos` | Immich, the photos place, the viewer, the screensaver, place names |
| `steam/` | `TvShell.Steam` | Steam's installed games and the games place |
| `switch/` | `TvShell.Switch` | A Switch game's name and icon, read from its file by the shell itself (`format/`, also built into the `switch-gameinfo` tool) |
| `apps/` | `TvShell.Apps` | Programs the shell starts and shows (`ManagedApp`), how long they stay loaded (`AppSettings`), the Home place, app tiles and logos |
| `youtube/` | `TvShell.YouTube` | YouTube's settings, for the web app that is YouTube (`../apps/webapp`) |
| `controllers/` | `TvShell.Controllers` | The controller service's client, the Controllers settings, the motion test |
| `assistant/` | `TvShell.Assistant` | The remote's Assistant button and voice |
| `../shared/input/` | `TvShared.Input` | Controller input as keys (`GamepadInput`), shared with the web app (`../shared/DEVELOPMENT.md`) |

Dependencies point one way: topics may use `common/` (and `audio/` uses `apps/` for the
logos); `common/` knows none of them. Where `common/` needs something of a topic, the topic
hands it over at start: the network layer adds the Immich API key through a rule the photos
module gives it (`NetworkAccessManager::addRequestRule`, set up in `main.cpp`). The build
puts all modules under `build/qml/TvShell/…` for the tools; at run time they come from the
program's resources.

## Components

### Taking the screen (`session/BaseLayer.*`)

Tags the window `STEAM_GAME=2113929217` (`0x7E000001`) and puts that ID first in
gamescope's root property `GAMESCOPECTRL_BASELAYER_APPID`. Steam rewrites that list when a
game starts or exits and when Steam restarts; the shell re-claims it whenever only Steam's
own entries (769, 413091) are left, never while a game's ID is in it (`gameRunning`). On exit
it removes its ID again. `claim = false` steps aside for Steam. `onScreen` follows
`GAMESCOPE_FOCUSED_APP`. Does nothing outside gamescope (`available`).

IDs `0x7E0000xx` are the shell's own (`isOurs`): they never count as a game. `frontApp` names
one of them, a managed app's, to show in place of the shell: the list then starts with that
ID, then the shell's. `frontAppOnScreen` follows the focus like `onScreen`.

Steam keeps the Steam and … buttons and draws its menu and Quick Access in its own window,
behind the shell. The shell steps aside when Steam opens its menu (Steam writes root
`GAMESCOPE_BLUR_MODE`) or Quick Access (only visible in Steam's `logs/webhelper.txt`,
followed by `session/SteamLog.*`). If Steam closes Quick Access right after the handoff, the
shell re-opens it with Steam's Ctrl+2 shortcut (XTest).

### Global keys (`session/BaseLayer.*`, `../system/`)

Home (`XF86HomePage`), Power, YouTube, Netflix and Mute (`XF86AudioMute`) are passive grabs on the root window, so they
arrive whichever window has focus (Steam's screens included). Several remote keys send evdev
codes above 247, which X11 can't carry; `../system/etc/udev/hwdb.d/90-chromecast-remote.hwdb`
remaps them (OK → `enter`, YouTube → `prog1` = `XF86Launch1`, Netflix → `prog2` =
`XF86Launch2`, Power → `sleep` =
`XF86Sleep`), and `../system/etc/atomic-update.conf.d/tv-remote.conf` keeps that file across
SteamOS updates.

The grabs use `owner_events = false`: with `true`, X delivered the release to the shell's
own window and Power looked stuck down. A held key auto-repeats as release/press pairs every
40 ms; a Power release only counts after 80 ms without a new press. All grabs are redone
after a resume (logind `PrepareForSleep`). The shell holds a logind `handle-suspend-key`
inhibitor lock so logind doesn't suspend on Power itself.

### Controller and keys (`../shared/input/GamepadInput.*`)

Each controller's sticks are kept by themselves, and the one pushed furthest decides the
direction. Steam's virtual pad of a controller that lies untouched still sends its
sticks' noise, a couple of hundred events a second; with one position for all of them it
overwrote the stick being pushed, and every return to the pushed value was a new press.

The remote and keyboards arrive as normal key events. The controller is read through SDL3
from Steam's virtual Xbox pad (`SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD=1`, evdev
only, no hidapi) and turned into key events sent to the window. Only Steam's virtual pads
(USB id 28de:11ff) are opened: Steam makes one for every controller, and a controller the
kernel also shows as a gamepad (a Joy-Con, through `hid_nintendo`) would otherwise be read
twice, in two layouts. A = Return, B/Back = Back,
Start = Menu, d-pad and both sticks = arrows with repeat (whichever stick is pushed further
decides; the right one is there for one-handed use, next to the buttons); LB/RB are a signal
(`shoulderPressed`), not keys. Only while the shell is on screen and active, since the same
pad feeds games; the web app reads it for itself while it is shown. The sticks count only
while the shell is fully awake (`sticks`): a
thumb resting on a Steam Controller trackpad that Steam maps to the stick ended the
screensaver.

### Idle (`power/IdleController.*`)

States Active → Screensaver → ScreenOff, then suspend. Counts only while the shell is on
screen. Activity: keys reaching the shell, controller input (also during games), gamescope's
`GAMESCOPE_INPUT_COUNTER`, the global keys, resume. Its event filter lets Left, Right and
Return through to the screensaver; other keys only wake.

Three kinds of activity hold the chain back. Input restarts the idle time. Foreground
(`watching`, set in `Main.qml`: a photo's video, the shown app playing something that isn't
a song showing only its cover) stops the chain altogether, and when it ends the idle time
counts from that moment. Background (`audioPlaying`: an app playing what keeps playing when
it is left, such as music, shown or not) lets the screensaver
and a black screen come but keeps the signal and the machine awake. The time since the last
input is kept separate from the other two, so a sleep timer that ignores what is playing can
be added on top of it.

Sleep waits for the remote (`power/RemoteLink.*`, bound to `remoteConnected` in
`Main.qml`): the remote can wake the machine only if it let go of its Bluetooth connection
by itself before the machine went to sleep, which it does about 30 minutes after it was
last used (see the log, "Two ways of going to sleep"). So `sleepIfDue()` puts the machine
to sleep only with the screen off, nothing playing or watched, and the remote not
connected; until then it waits. Sleep is due when the idle time is up or when it was asked
for: the remote's Power and the power dialog's Sleep call `sleep()`, which turns the screen
off at once. Any input before the sleep calls it off. `RemoteLink` knows the remote as the
paired device offering the Android TV Voice service and follows its `Connected` through
bluez's signals, so a connection is seen as it happens.

The ready state: logind's `PrepareForSleep` notes whether the remote was connected as the
machine went to sleep. If it wasn't, it may be what woke the machine: at resume the screen
stays off for 2 s, and if the remote connects in that time (it took 64 to 148 ms), the
wake is the remote's. A pickup and a press look the same until then; a press arrives as
its key about 0.2 s after the connection. The screen then stays off as after `sleep()`:
any key or button wakes it, otherwise the machine sleeps again once the remote lets go
(30 minutes after the pickup). If the remote doesn't connect within 2 s, or it was
connected at sleep, something else woke the machine, and the screen comes on as usual.
Two things arrive at every resume that aren't anyone's input: `XF86WakeUp`, which isn't
taken as a key press, and one move of gamescope's input counter, which comes before logind
says the machine is back. Input counts for nothing from the shell's own suspend until then,
so that resume handling decides alone (otherwise the screen came on for a moment at every
resume).

Screen off is
`gamescopectl drm_sleep_external_screen 1`. Screen on (`0`) is sent on waking and after every
resume, then three more times two seconds apart: sent once right after a resume it can go
unheeded, and the screen then stays dark (see the log, 2026-11-22). What is being watched
(`watching`) keeps it awake. A shell that starts (a deploy, a restart) sends screen on once
too, and `TvPower` wakes the TV once: gamescope may still have the screen asleep from the
shell before, and both only act on changes otherwise.

### Screensaver (`photos/Screensaver.qml`, `ScreensaverLayer.qml`, `PhotoBackdrop.qml`, `PhotoInfo.qml`, `photos/ScreensaverSettings.*`)

`ImmichClient::loadScreensaver` collects all chosen albums (timeline buckets, images only, no
duplicates) and shuffles them once. That happens when a run ends (and at startup, and when
the albums change), so the next run's order and first photo (preloaded) are ready; a run
starts at opacity 0 and fades in once its first photo is revealed (at most 4 s), so the
screen is never black first.

Two photo layers: the next photo loads into the hidden one and is revealed once loaded
(slide 600 ms, fade 800 ms or at once); the other is then hidden, not just covered (the bars
beside a portrait photo would show it). `index` is the target, `shownIndex` what's on
screen; presses during a transition only move `index`, and the next transition goes
straight there. Backgrounds are a separate `PhotoBackdrop` pair under the layers that
crossfades in place (500 ms): Immich's thumbnail decoded at 96×54 (blur) or 3×2 (gradient),
blurred with `MultiEffect` and darkened. Date and place sit above the layers (z 2) and drift
to a nearby spot every 5 minutes (OLED).

### Photos (`photos/ImmichClient.*`, `photos/ImmichConfig.*`, `photos/PhotosPlace.qml`, `AlbumGroups.qml`, `PhotoGrid.qml`, `PhotoViewer.qml`, `PhotoTile.qml`, `PhotoLayer.qml`)

Albums sorted by their newest photo (`endDate`), newest first, with its `year`; memories for
today; album contents a month bucket at a time
(`/timeline/buckets`, `/timeline/bucket`). Each asset map carries `id`, `ratio`, `isImage`,
`date` (local, from `fileCreatedAt` + `localOffsetHours`) and `place`.

`AlbumGroups` shows the albums by year: it turns the sorted list into rows for a `ListView`,
a heading row per year and under it rows of as many albums as fit (the `FitGrid` rule for
columns and card size). The selection is a row and a column, not a list index. Left and
Right set `intendedColumn`; Up and Down go to the next row of albums and take that column,
or the row's last one if it is shorter, so passing through a short row doesn't lose the
column. Left and Right past a row's end continue in the neighbouring row of albums (first
or last album), headings skipped. The selected album is found again by its ID when the rows are rebuilt (each time
Photos is opened, or when the width changes).

The viewer shows the cached thumbnail at once and loads the preview and full size after
250 ms on a photo; the last image stays up until the next one has something to show.
Once settled, the next three and previous two full-size images are preloaded and kept
referenced (Qt's image cache drops unreferenced large images quickly). Transitions follow
the screensaver's model (`index`/`shownIndex`, one at a time, 250 ms OutCubic); a held key
(auto-repeat) skips them and steps at most ~8 photos/s, and the backdrop follows once it
stops for 0.2 s. Images are requested with Immich's edits (`edited=true`) and EXIF rotation
(`autoTransform`).

Videos play in Qt Multimedia (ffmpeg backend) through `photos/ImmichMediaProxy.*`, a loopback
HTTP proxy that adds the API key (Qt's player can't send headers) and passes Range requests
through; a random token in its URLs keeps other local programs out.
While a video is loaded (playing or paused) the viewer's Left and Right set the player's
`position` (5 s steps under two minutes, 10 s above), a bar along the bottom shows
`position / duration`, and Back unloads the video; only then do Left and Right step between
photos again.

The viewer's player is a `MediaPlayer` with its own `AudioOutput` whose `device` is bound to
`MediaDevices.defaultAudioOutput`: an output left to itself keeps the device that was the
default when it was created, and stays silent when the sound has since moved (the soundbar
connecting over Bluetooth after the shell started).

`common/NetworkAccess.*` is the shell's network layer, QML images included: it adds the key to
requests for the Immich server only, and caches on disk (`~/.cache/tvshell/network`, up to
2 GiB; asset images are served from the cache when present).

Place names (`photos/PlaceLookup.*`): the nearest populated place to the photo's GPS and its
town, from `places.tsv` next to the program, made by `./make-places.py` from GeoNames
(CC BY 4.0). Sweden and the Czech Republic only: each country needs a rule there for which
administrative level is the town. Elsewhere, or without the list, Immich's `city, country`.
Immich's own names are too coarse; see the log, "Place names".

### Managed apps (`apps/ManagedApp.*`, `apps/AppSettings.*`, `Main.qml`)

A managed app is a separate program the shell starts and shows itself, without Steam:
YouTube (a web app, `../apps/webapp`), Jellyfin Desktop (`../apps/jellyfin-desktop`) and
Eden with a Switch game (`../apps/eden`). Each runs as its own user service
(`webapp@youtube.service`, `jellyfin-desktop.service`, `eden.service`, installed with the
program), so restarting the shell doesn't end it and the shell picks a running one up again.

`ManagedApp` does three things for one such program:

- **Service:** follows the unit's `ActiveState` on systemd's session-bus API (`running`),
  starts and stops it (`start()`, `quit()`, `kill()`), and reports when it ends on its own
  (`exited`).
- **Window:** while the service runs, looks every half second for the largest top-level
  window with the program's `WM_CLASS` and tags it `STEAM_GAME=<appId>` (`ready`). The
  program doesn't know about gamescope; the tag is what lets `BaseLayer.frontApp` show it.
- **Playback:** its player is the MPRIS name with the given prefix whose owner's process runs
  in the app's unit (D-Bus `GetConnectionUnixProcessID`, then systemd's `GetUnitByPID`):
  several programs may share a prefix, as every web app's player is Chromium's
  (`org.mpris.MediaPlayer2.chromium.instance<pid>`). It follows `PlaybackStatus` (`playing`)
  and `Metadata` (`title`, `artist`), and for a web app its own `local.webapp.<name>`
  (`mediaKindName`): `background`, whether what is loaded keeps playing when the app is
  left, and `coverOnly`. Other programs are never `background`.

Pausing comes in two kinds. `pause()` pauses for good: it stays paused until someone plays
it. `pauseWhileAway()` is the pause of leaving: `resume()` continues it when the app is
opened again (`pausedWhileAway`), and nothing else. A parked game (`alwaysResume`, Eden)
continues whatever paused it. For a web app the title of what leaving paused is kept even
if its player forgets it, and `resume()` asks the web app itself (`local.webapp.<name>`
`Resume`), which plays once the page is visible again.
For other programs `resume()` sends MPRIS Play, waiting up to 5 s for the player to say
`Paused` if it doesn't yet.

`Main.qml` holds which app is shown (`shownApp`). `BaseLayer.frontApp` is that app's ID while
it is ready, the idle state is Active and the power dialog is closed; otherwise the shell
itself has the screen, which is how the screensaver, screen off and the dialog appear over
it and how it returns afterwards. The shell stays on screen until a starting app's window
is there. The rules that span the apps:

- **Leaving** (`leaveApp()`: Home, the YouTube or Netflix key while another app is shown,
  the app's `LeaveApp`): an app that is `background` plays on; any other is
  `pauseWhileAway()`d. **Opening** (`openApp()`) starts the unit and `resume()`s.
- **One thing plays at a time:** when an app starts playing, every other app that plays is
  `pause()`d (`onlyOnePlaying`), so opening Jellyfin leaves YouTube's music alone until a
  film starts. A photo's video doesn't pause anything.
- **Suspending a left web app:** Settings, Apps, also has the web app suspend time, how
  long any web app left with nothing playing stays open before it suspends its page
  (`AppSettings.webAppSuspendAfter`, written to `~/.config/tvshell/apps/webapp.conf`; half
  an hour). The web app counts it itself
  (`../apps/webapp/DEVELOPMENT.md`, Suspending).
- **Closing a left app:** an app that is running, not shown and not playing is closed after
  the time in Settings, Apps (`AppSettings.closeAfter`, in `tvshell.conf` `[apps]`:
  YouTube 0, kept; Jellyfin ten minutes), counted from when it was left or stopped playing,
  also for one found running at start. Eden has no such timer: a parked game stays.
- **Idle:** time counts while the app is on screen (`frontAppOnScreen`); its `playing`
  (unless `coverOnly`) is foreground activity like the shell's own videos, and any app
  playing `background` is `audioPlaying` (`IdleController`, Idle). Keys go to the app's
  window, so the shell sees input there only through gamescope's input counter. Screen off
  over an app pauses it unless it is `background`; `aboutToSleep` pauses every app.
- **The top bar** (`NowPlayingButton`) shows YouTube while it isn't shown and either plays
  on in the background (OK plays and pauses it, `togglePlay()`: MPRIS `PlayPause`) or was
  paused by leaving (OK opens it, which continues it). The web app ends a background
  session after ten minutes paused, which takes it off the bar.
- A Steam game started from the shell (`Main.launchGame()`) asks with a `ChoiceDialog` while
  YouTube plays in the background.

YouTube is opened from its tile or the remote's YouTube key (`openYouTube()`), which
remembers where it came from: a page of the shell, or Steam if Steam had the screen.
YouTube's own way back (Back on its account picker, its Exit: both `LeaveApp`) returns
there (`leaveYouTube()`); Home always goes to the shell. Its settings in Settings (what
keeps playing when it is left, what SponsorBlock skips) are `youtube/YouTubeSettings`,
written to `~/.config/tvshell/apps/youtube.conf`, which the web app reads.

The other direction, a program asking for the shell, goes through `ShellService`
(`session/ShellService.*`): the shell owns `local.tvshell` on the session bus, and its
`LeaveApp` method does what Home does to the app (YouTube's goes back where it came from).
Jellyfin calls it when Back is pressed with nowhere to go back to: its web interface then
asks the host to exit, and a patch in `../apps/jellyfin-desktop/patches` turns that into
running a profile script that makes the call. The web app calls it itself.

`ShellService` also listens to the controller service (`../controllers`, `local.tvcontrollers`
on the session bus): its `HomePressed` signal, Home on a Joy-Con, becomes
`controllerHomePressed()`, which `Main.qml` passes on as `BaseLayer`'s `homePressed()`, so
it does exactly what Home on the remote does. The Joy-Cons' virtual controllers have no
Home button, and nothing but that service can read a Joy-Con.

What a program needs to behave on a TV (flags, controller settings, key map, logging) is in
its own folder and the log. The remote's Netflix key (`XF86Launch2`, grabbed like the
others) opens Jellyfin.

Eden differs in three ways. It opens windows as it goes (its on-screen keyboard is a
dialog the game waits behind), so with `allWindows` every window of its class is tagged,
not only the largest. Left with Home it is paused and kept: a parked game, continued with
`ManagedApp::resume()` (MPRIS `Play`) when its tile is opened; our Eden speaks that much
MPRIS. And because a parked game holds about half the machine's memory, `launchGame` asks
before a Steam game is started next to one (`parkedDialog`).

On the Home place OK opens a tile when the key is let go; held for 0.6 s it brings up the
tile's menu (`tileMenu` in `Main.qml`): Open, and for a loaded app (`ManagedApp.running`,
`HomePlace.runningApps`) Close and Force close. Close ends the program its own way
(`ManagedApp::quit()` stops the unit), Force close kills the unit's processes
(`ManagedApp::kill()`). `ChoiceDialog` takes no repeats of OK, so the key that is still held
when the menu opens doesn't pick its first answer.

### Controllers in Settings (`controllers/ControllerClient.*`, `controllers/ControllersPane.qml`)

Nintendo's controllers belong to the controller service (`../controllers/DEVELOPMENT.md`);
the shell only shows them and asks. `ControllerClient` is the session-bus client of
`local.tvcontrollers`: it asks `Describe()` when the service signals `Changed` (and on
`refresh()`, since battery levels aren't announced) and turns the JSON into `units` and
`waiting`; `startSearch`, `stopSearch`, `switchOff` and `endUnit` pass straight through.

The Controllers category of `SettingsPage` has no options. `ControllersPane` takes the
options' place: the instructions and the list of controllers, with a `ChoiceDialog` for a
line's actions (parented to the page, so it covers all of it). The search runs exactly
while the focus is in the pane's list or its dialog and the shell is awake
(`searchWanted`), renewed every minute because the service ends a search after two; a
search takes radio time from the controllers in use, so passing over the category doesn't
start one. Forming units is not done here: the gestures (L+R, SL+SR) are the service's,
and the pane shows the result.

A line is a `ControllerRow` in two parts: the handle (`PlayerLights`: four lights, as many
lit as the player number, all flashing for a waiting Joy-Con) and the body (what the
controller is made of, the batteries). The list is in player order (the service sorts).
The focus stays on the `ListView`; which line and which part is selected is the pane's
state (`selectedAddress`, `onHandle`, `holding`), and the rows draw it. The selection is
kept by the address of a line's first Joy-Con, because the model is rebuilt whenever
something changes and a controller that moves has to keep the selection.

Reordering needs a deliberate grab, since a stray push of a d-pad or stick must not
reassign players: Left goes onto the handle, OK holds it, Up and Down call `swapPlayers`
with the neighbour (key repeats ignored, and no second move until the list has changed),
OK or Back lets go, and it stays where it is. `ControllerClient` only announces a change
when the service's answer differs from the last one, so the five-second battery refresh
doesn't rebuild the list.

The motion test (`controllers/ControllerTest.qml`, `controllers/ControllerModel.qml`,
`controllers/MotionClient.*`) reads motion the way an emulator does: `MotionClient` is a client of
the controller service's cemuhook server on localhost:26760 (subscribing every second,
since the server drops a silent client), so the test also tests that server. Each
`MotionPad` keeps the latest acceleration and rotation and an orientation: the gyroscope's
turn is added up reading by reading, and while the acceleration is close to 1 g the tilt
is pulled 1 % of the way towards what gravity says. The turn about the vertical has no
such anchor and drifts; `recenter()` drops it. It also gives the push (the acceleration
turned into the room's frame with gravity taken out, shown as a bar), limited to `reach`.
The `position` it gives is that push as a displacement, so far for each g, not a place.
(Adding the push up to a speed and a place was tried in several forms and dropped: from
this sensor alone every small error keeps growing; see the log.) `recenter()` takes the
controller as level and pointing away, from the accelerometer's average over half a
second. Three readouts check the sensor itself: `strength` (1 g at rest), `turned` (360
after a full turn on the table) and the reports lost on the radio since
recentring (from the service's `lostReports`, fetched every second while the test is
open; the service fills their readings in). The screen's two actions, Recenter and Calibrate, wait
0.9 s before acting, because pressing OK on the controller itself shakes it; Calibrate
asks the service (`ControllerClient::calibrateMotion`), which measures at rest and corrects
what it passes on from then on. At a wall only the place is held; the speed is kept, because the hand is
still moving and its slowing down has to come off that speed (set to zero at the wall, a
long move ended in travel backwards). `ControllerTest` draws a wire box that stays
put and moves the shape inside it by that position. Readings arrive 200 times a second and are
passed on to QML 60 times. `MotionClient` turns the packets' directions (set by what Eden expects) into the
pad's axes, which for a controller lying flat, face up, top
away from the viewer are Qt Quick 3D's (x right, y up, z towards the viewer), so the
orientation goes straight onto `ControllerModel`, a few boxes and cylinders in that pose.
Qt Quick 3D is only imported in QML, not linked; SteamOS ships it and the toolchain image
has it since the motion test.

While the test is open it asks the service every second to read the Ring-Con on each
right Joy-Con (`ControllerClient::watchRing`; the service stops by itself five seconds
after the last request). Whether a ring is attached comes with the service's description
(`ring`), its flex with `ControllerClient::ringFlex`, 66 times a second. The test shows
the raw value and a bar around the value at the last Recenter, as wide as the ring has
been bent since: the service passes the flex on unscaled, and the game does the
calibrating.

In `SettingsPage`, an option without a `change` function is a button (the screensaver's
album picker): its value is shown without the ‹ › that mean "Left and Right change this".

### Power (`power/PowerActions.*`, `power/PowerDialog.qml`)

Power short press asks for sleep (`IdleController::sleep()`: screen off now, asleep once the
remote lets go) or, with the screen off, turns it on; held 0.8 s it opens the dialog (Turn off
screen, Sleep, Restart, Power off, Turn off controllers, Cancel), which closes itself after 20 s
without a key. Its Turn off screen is `IdleController::screenOff()`: only the screen, the
machine sleeps later by the usual idle times. The dialog's Sleep also goes through
`IdleController::sleep()`; Restart and Power off go to logind.

The TV follows the screen (`power/TvPower.*`): whenever IdleController tells gamescope to put
the screen to sleep (`screenSleeping`), TvPower sends the TV to standby over HDMI-CEC
through SteamOS's cecd (D-Bus `CecDevice1.Standby(0)`), and wakes it when the screen wakes
(`Wake`: Image View On and Active Source); the soundbar follows the TV. After a resume with
the screen meant to be on (a sleep Steam started, which cecd turned the TV off for), it
wakes the TV again. This needs Steam's HDMI CEC settings set so the TV can't act on the
machine: "Sleep when turning off the TV" off (the TV's answer to our standby would put the
machine to sleep) and "Turn on TV when resuming from sleep" off (the ready state keeps the
screen off after a wake by the remote). Log, "The TV off with the screen". "Turn off controllers" sends the Steam Controller "turn off"
feature report (`01 9F 04 "off!"`, 64 bytes) to each HID interface of a Steam Controller
puck (`28de:1304`/`1305`) through hidraw, which `deck` may open alongside Steam, and
disconnects Bluetooth gamepads through bluez (not the remote).

### Assistant (`assistant/RemoteAssistant.*`, `assistant/ImaAdpcm.*`, `assistant/AudioPlayer.*`, `assistant/AssistantIndicator.qml`)

The remote's Android TV Voice GATT service over bluez D-Bus (no root). On (re)subscribing it
asks the remote for the hold-to-talk model. While Assistant is held, the audio (IMA ADPCM,
16 kHz) is decoded and its level drives the ring; on release the recording is normalized
(peak to 70%) and played back through vendored miniaudio (`3rdparty/miniaudio/`). Protocol
notes: the log, "Voice protocol details".

### Other

- **Settings** (`SettingsPage.qml`, `SettingRow.qml`, `AlbumPicker.qml`,
  `photos/ScreensaverSettings.*`, `photos/ViewerSettings.*`): categories on the left, values
  changed with left/right and saved at once through `QSettings` (`tvshell.conf`).
- **Steam games** (`steam/SteamLibrary.*`, `steam/GamesPlace.qml`, `GameTile.qml`): installed
  games from Steam's manifests and its cached cover art; launched with
  `steam://rungameid/<id>`.
- **A Switch game's icon** (`switch/`): `SwitchGame` takes a game file (`.xci` or `.nsp`,
  relative to `~/Games/Emulators/Switch/`) and gives its name and its 256 px icon. Ring Fit's
  tile on Home uses it (`AppTile`'s `icon`: in place of a drawn logo, the picture fills
  the top of the tile, the name under it). The file is read
  by the shell's own reader (`switch/format/`, plain C++ with libcrypto and zlib, written
  from switchbrew's descriptions, nothing from Eden), on a thread of its own, with the keys
  from the newest version folder in `~/Games/Emulators/Switch/keys/` (not Eden's copy),
  loaded for that read and dropped after it. What it read is cached in
  `~/.cache/tvshell/switch/<hash of the path>/` (`game.json`, `icon.jpg`) and read again only
  when the file's size or date changes. Not covered: updates that change a game's name or
  icon, tickets made for one console, NSZ and XCZ. `switch-gameinfo <file> [icon.jpg]`
  prints what the reader gets from one file.
- **Top bar** (`TopBar.qml`, `BarButton.qml`): one tab per place; the highlight moves
  freely, OK switches the place showing and the focus stays on the bar. Only Down, LB/RB and
  Back move the focus between the bar and a place (the places' `focus` isn't bound to the
  selected tab, which would pull it into the place on every switch).
- **Volume** (`audio/SystemVolume.*`, `audio/VolumeIndicator.qml`): Steam handles volume up and
  down; the shell follows the default output's volume and mute through PipeWire's PulseAudio
  interface (libpulse, its own thread) and shows the indicator when they change on the same
  output, or when a volume key press reaches the window (an event filter; so it also shows
  at 0 % and 100 %). Nothing handles Mute, so the shell grabs it globally and toggles the
  output's mute itself. In the screensaver the volume keys pass without ending it
  (`IdleController`).
- **Card grids** (`common/FitGrid.qml`): as many columns as fit at the design card size, then
  the cards grow so rows end flush with the right margin. Used by Home, Games and the photo
  grid; the albums use the same rule in `AlbumGroups`.

## Build, deploy, debug

`./build.sh` runs CMake and Ninja in the toolchain image (`tv/toolchains/steamos-3.9:dev`,
`--network none`) and writes `build/tvshell`. It mounts the whole repository, for
`../shared`. `./deploy.sh` copies it (and
`build/places.tsv`) to `deck@gabecube:~/.local/share/tvshell/`, installs
`deploy/tvshell.service` and restarts it.

Lint QML in the toolchain image:

    docker run --rm -v "$PWD:/src:ro" -v "$PWD/build:/out" tv/toolchains/steamos-3.9:dev \
        /usr/lib/qt6/bin/qmllint -I /out/qml /src/<folder>/<File>.qml

`deploy/tvshell.service` is a user unit that belongs to the Gaming Mode session
(`PartOf=graphical-session.target`, reads `%t/gamescope-environment`). `--enable` links it
into `~/.config/systemd/user/gamescope-session.target.wants/`: the shell then comes up with
Gaming Mode, before Steam has started, and takes the screen when Steam finishes starting.

- `TVSHELL_NO_CLAIM=1` runs it without taking the screen.
- A start without a display, to catch QML errors before deploying (runs until the timeout;
  anything QML prints is an error; without `QT_FORCE_STDERR_LOGGING=1` Qt logs to the
  journal and nothing shows):

      docker run --rm --network none -u "$(id -u):$(id -g)" -e HOME=/tmp \
          -e QT_QPA_PLATFORM=offscreen -e QT_FORCE_STDERR_LOGGING=1 -v "$PWD/build:/out:ro" \
          tv/toolchains/steamos-3.9:dev timeout 8 /out/tvshell

- A screenshot of what gamescope shows: `gamescopectl screenshot /tmp/x.png` on gabecube.
- Logging categories: `tvshell.baselayer`, `tv.gamepad`, `tvshell.idle`, `tvshell.tv`,
  `tvshell.power`, `tvshell.assistant`, `tvshell.audio`, `tvshell.immich`,
  `tvshell.mediaproxy`, `tvshell.steam`, `tvshell.steamlog`, `tvshell.volume`,
  `tvshell.app`, `tvshell.service`.

## Next

- Settings: the rows with a choice (`SettingRow`) change their value with Left and Right as
  soon as they have the focus, and values get changed by accident on the way through (the
  idle times, more than once). Make them work like a combo box: the row only shows its
  value; OK opens the list of choices, and nothing changes until one is picked there.
- Installed apps, defined outside the shell: an app is installed when it has a manifest
  (its name and other details) and an icon file, and the shell reads those in place of
  naming each app in its code and drawing its logo itself. The manifest would also say how
  to start, pause, continue and close the app and which settings it has for the shell's
  Settings to show. Not designed yet on purpose: what it needs to say gets clearer with
  every app added (Jellyfin, Eden, YouTube, a library of Switch games). Until then the
  logos stay in the shell.
- The core and the modules talk through a few general contracts, not by name. Today the
  topic modules depend on `common` at most, but `Main.qml` creates every module's objects
  and holds every rule that spans them (what counts as watching for idle, pausing
  everything before sleep, what Home does, asking about a parked game), and
  `SettingsPage` knows every module's settings. Candidates: something that plays (playing,
  plays on in the background, pause; `ManagedApp` has it for the apps), a place (its tab and page), a settings section. Not designed ahead: kept
  in mind and shaped as things are added; the installed-app manifests are one such step.
- An overlay layer: one place for what is shown on top of whatever has the screen (the
  volume display, probably the Assistant's ring).
- The remote and sleep, open after the redesign: the machine's power button and Steam's
  power menu still suspend at once with the remote connected, so the remote can't wake the
  machine afterwards (the shell could take the power key from logind as it does the
  sleep key). Not known yet whether Steam lights the screen by itself after a resume.
  A bumped table, maybe even a vibrating floor, is enough for the remote to wake the
  machine (most likely three times in one evening of someone packing nearby), and each
  such wake keeps the machine up for the remote's next 30 minutes.
- Remotes behind one interface. The TV's own remote (IR, once the LG TV is here) may be
  used instead of or next to this one. Each remote has its own quirks (this one: wakes
  the machine only if it let go of its connection first, and when merely moved), so each
  gets its own module that handles them, and the idle chain asks a common interface: can
  the machine sleep now, did a remote wake it. `RemoteLink` and `RemoteAssistant` are this
  remote's pieces today.
- Hand the Assistant audio to speech recognition.
- Photos: smarter framing for 4:3 and 3:4 on 16:9 (faces? crop/zoom), timeline and people,
  seeking and progress for videos.
- More apps in Home (music), more places (emulators).
- Jellyfin: text entry with the remote, interface size by resolution, its paused film in the
  top bar, a way to sign in from the shell.
- YouTube: check video decoding (GPU or CPU, 4K), the remote's media keys, memory use.
- A photo's video over music playing in the background: both play now. Maybe the music
  gets quieter for as long as the video plays.
- The shell stays on screen while an app starts (a few seconds for YouTube): something
  that shows the app is coming.
- A quick double press of a direction goes all the way that way: to the end of a row, or
  down to the bottom (YouTube's recommended rows and its refresh button take long runs of
  presses today). The first press acts at once as now; only the second, if it comes
  quickly, jumps. An idea, not designed: how a jump is made in YouTube's page (keys it
  already knows, or repeating the press until the focus stops moving), how quick counts
  as quick, and whether the shell's own grids and places get it too. For YouTube it lives
  in the web app (`../apps/webapp`).
- Home, the YouTube and Netflix keys and the power dialog over a running game.
