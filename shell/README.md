# tvshell

The TV's home screen on the Steam Machine. It runs inside Steam's Gaming Mode, next to
Steam: games still start and run through Steam, and the shell is what you see when no game
is running. Everything works with a TV remote or a controller; no mouse or keyboard needed.
Layouts and looks are placeholders for now.

How it's built: [DEVELOPMENT.md](DEVELOPMENT.md).

## What it does

- **Home**: the first place, with your apps. For now YouTube, in the same TV interface smart
  TVs use, with all its accounts, playing at up to the screen's resolution (4K on a 4K
  screen). Sponsor segments in videos are skipped (SponsorBlock), with
  a short note on screen; Settings chooses what else to skip. And Jellyfin, for your films and
  shows: its own TV interface, playing files directly. Both are programs of their own
  (YouTube: `../apps/webapp`), so they stay where you left them when the shell restarts.
  Opening one the first time takes a few seconds, during which the shell stays on screen.
- **Games**: your installed Steam games. OK starts one; when it ends, you're back in the shell.
- **Photos**: photos from an Immich server. "On this day" memories and your albums, in
  groups by the year of their newest photo with the newest album first. Moving up and down
  keeps to the column you chose, also across a short row; Left and Right carry on into the
  next or previous row at a row's end. An album opens a grid, OK opens a
  photo full screen. Left/right move between photos, with a
  slide or crossfade and the photo's colours or a blurred copy behind it. Each photo shows
  its date and place. Videos carry a play badge in an album and play with OK; while one is
  open, a bar along the bottom shows how far it is, Left and Right jump back and forward,
  and Back returns to moving between photos.
- **Screensaver**: after a while without input, photos from the albums you choose, one at a
  time, with their date and place. Left/right browse without leaving it, OK opens the photo,
  any other key ends it. Without albums: a clock.
- **Screen off and sleep**: later still, the screen turns off, and later the machine
  sleeps. Defaults: screensaver after 5 minutes, screen off after 30, sleep after 60.
  The machine goes to sleep only once the remote has been left alone for about 30
  minutes; until then it waits with the screen off. That way the remote can always wake it.
- **Waking with the remote**: picking up the remote wakes the machine, but the screen
  stays off until you press a button. If nobody does (someone only moved it), the machine
  goes back to sleep about 30 minutes later. Controllers and the machine's power button
  wake it with the screen on.
- **Music in the background**: leave YouTube while music or a podcast is playing (what
  YouTube itself marks so) and it plays on; an ordinary video pauses. Settings, YouTube,
  chooses which of songs, music videos, podcasts and other videos play on. The top bar then
  shows the title and artist: OK there pauses and resumes, the YouTube button goes back to
  it. While it plays, the screensaver and the screen-off time work as usual, but the screen
  only goes black (the sound stays) and the machine doesn't sleep. Paused for 10 minutes,
  it's put away. When Jellyfin or a game starts playing, it pauses and stays paused;
  starting a Steam game asks first. A photo's video plays over it.
- **A paused video**: leaving YouTube pauses a video, which then shows in the top bar; OK
  there, the tile or the YouTube button open YouTube and continue it. A video you paused
  yourself stays paused.
- **Volume**: with the remote's volume and Mute buttons sending infrared to the TV (see
  Remote, below), the TV changes the sound and shows it; the shell doesn't hear them. Otherwise, volume buttons show a small indicator in the top right
  corner, over whatever is on screen, also when the volume is already at either end. Mute
  mutes and unmutes, from anywhere; the indicator turns red and says "Muted". Neither ends
  the screensaver.
- **Any resolution**: the interface keeps its proportions from 1080p to 4K, and photos and
  YouTube are shown at the screen's full resolution. When the resolution changes, the shell
  restarts itself (a couple of seconds).
- **Assistant button**: holding it shows a ring that follows your voice and plays the
  recording back when you let go. It doesn't do anything else yet.

## Controls

| | Remote | Controller |
|---|---|---|
| Move | ring | d-pad, either stick |
| Select | OK | A |
| Back | Back | B |
| Switch place | top bar | LB / RB |
| Settings | top bar | Start (≡) |
| Back to the shell | Home, from anywhere (again: the Home place) | Home on a Joy-Con |
| YouTube | YouTube, from anywhere | Home → YouTube |
| Jellyfin | Netflix, from anywhere | Home → Jellyfin |
| Mute | Mute, from anywhere | |
| Screen off and sleep / screen on | Power (short press) | |
| Screen off, sleep, restart, power off, controllers off | Power (hold); the menu goes away by itself after 20 s | |

When the screen goes off, the TV goes to standby (over HDMI-CEC), and it comes back on with
the screen. For that, in Steam's Settings, Power, HDMI CEC, switch off "Enable remote
control input", "Resume from sleep when turning on the TV", "Sleep when turning off the TV"
and "Turn on TV when resuming from sleep".
| Steam's menu, Quick Access | | Steam, … buttons |

The top bar holds the places (Home, Games, Photos) and Settings: left/right move along it,
OK switches place, Down goes in. Back inside a place returns to the top bar.

In YouTube, Back is YouTube's own back. On its "Who's watching?" screen, Back (or YouTube's
own Exit) leaves YouTube for whatever was open before, Steam included. Home pauses a video
(opening YouTube again carries on) and leaves music playing.

In Jellyfin, the arrows, OK and Back move around; while something plays, Left and Right seek.
On the controller the d-pad or either stick moves, A is OK, B is Back, the bumpers seek, X
pauses and Y searches. Home pauses and returns to the shell, and so does Back on Jellyfin's start screen;
the Jellyfin tile (or the remote's Netflix button) brings it back where it was. Left alone for ten minutes it closes; what you were
watching resumes from Jellyfin's "Continue watching" (Settings, Apps, changes how long, or
keeps it). A paused film or an untouched menu gets the screensaver like the shell does.

**Ring Fit Adventure** (the tile on Home) runs in the Switch emulator Eden with the
Joy-Cons, the leg strap and the Ring-Con (`../apps/eden`). Form the controller first: L on
the left Joy-Con and R on the right one together. Home on a Joy-Con or the remote parks the
game: it is paused and stays exactly as it is, also while the machine sleeps, and its tile
brings it back. After the Joy-Cons have been away (sleep), press L+R again. A parked game
is lost if the machine restarts; what the game itself saved is not. Because a parked game
takes about half the machine's memory, the TV asks before starting a Steam game next to it.

A blue dot in the corner of an app's tile means the app is still loaded: opening it is
instant and it is where you left it. Hold OK on a tile for its menu: Close ends a loaded
app (a parked game is then gone; what the game saved itself is kept), and Force close is
for one that doesn't react.

## Setup

**Steam:** turn off Steam's own dimming, screensaver and sleep; the shell handles those.

**Photos:** the shell signs in to Immich with an API key of a dedicated TV account and shows
the albums shared with that account. Put the server and key in `~/.config/tvshell/immich.conf`
(readable only by you, `chmod 600`):

    [immich]
    url=https://photos.example.com
    apiKey=...

The key needs the permissions `album.read`, `memory.read`, `asset.read` and `asset.view`.

**YouTube:** open it and choose "Sign in": it shows a code to enter at youtube.com/activate
on your phone. Add more accounts the same way. Sign-ins are kept.

**Jellyfin:** it is a separate program, Jellyfin Desktop, installed from
`../apps/jellyfin-desktop` (see its README). Signing in needs a keyboard once, in its
desktop mode; that is described there too.

**Remote:** the Chromecast with Google TV remote, paired over Bluetooth. Pair it with
`system/pair-remote.sh` (hold Back + Home on the remote until its light pulses). If pairing
keeps failing, take its batteries out for a few seconds.

Its volume and Mute buttons can control the TV directly by infrared, with the TV showing the
change: with the remote connected, run `ssh deck@gabecube 'python3 - lg' <
system/remote-ir.py` once (an LG TV; the remote keeps the codes). `clear` in place of `lg`
removes them, and the buttons reach the machine again.

## Settings

In the shell: Settings in the top bar, or Start on the controller. Power (when the
screensaver, screen off and sleep start), Controllers (below), Screensaver (albums, time per photo, transition,
background, date and place), Photos (transition and background in the viewer), Apps
(the web app suspend time: how long a web app such as YouTube stays open when you leave it
with nothing playing before it is suspended, half an hour at first, after which opening
YouTube reloads the video where it was; and whether YouTube and Jellyfin stay loaded, or close after 10 minutes, 30 minutes or an hour; at
first YouTube stays and Jellyfin closes after 10 minutes) and YouTube (what keeps playing when you leave it: songs that show only their
cover, music videos and podcasts at first, other videos too if you like; and what
SponsorBlock skips: sponsors, self-promotion, reminders, intros, outros, recaps, non-music
parts, filler; only sponsors at first). Changes apply at once.

**Controllers** shows Nintendo's controllers (Joy-Cons and the Pro Controller), if the
controller service is installed (`../controllers`). Go into the list with Right or OK:
while you are in it, the TV looks for new ones. Hold a controller's small sync button
until its lights sweep, and it is added. Then show how you hold it: L and R on two Joy-Cons make them one
controller, SL and SR on one make it a controller held sideways; a Pro Controller needs
nothing. The list shows each controller with its player number, drawn as the lights on
the controller show it (one to four lit; flashing while it waits), and its battery. OK on
a line offers Motion test, Change grip (the Joy-Cons wait to be regrouped) and Switch off.

The motion test draws the controller in 3D inside a box. It turns as you turn it, and
pushing or shaking it pushes the shape off the middle by as much. A bar shows how hard;
the sensor's readings are below. Left and Right pick one of two actions, OK runs it:
Recenter lines the picture up with the controller as it lies, and Calibrate measures the
controller's sensors at rest (lay it on a table and leave it alone for a few seconds) and
corrects its motion from then on, for games too. Lay the controller flat, face up, its top away from you, and press OK to
line the picture up; OK again any time it has drifted round. For players 1 and 2.
A right Joy-Con also has a Ring-Con line: slide it onto the ring, and the line shows the
ring's reading and a bar that follows your squeeze and pull, measured from where the ring
was at the last Recenter.

To change who is which player, go Left onto a controller's lights and press OK to grab it.
Up and Down then move it, one step a press, and OK or Back lets go where it is. It takes
that grab on purpose, so that a stray push of a stick doesn't reassign the players. Back
returns to the categories. Controllers in use
can feel sluggish while you are in the list, because the search shares their radio.

For the skipping, YouTube's program asks the SponsorBlock service (sponsor.ajay.app) about
each video you watch, by the first characters of a hash of its ID, not the ID itself. With
every switch off, it asks nothing.

They are saved in `~/.config/tvshell/tvshell.conf`, except YouTube's (below):

| Group | Key | Meaning |
|---|---|---|
| `[idle]` | `screensaverAfter`, `screenOffAfter`, `suspendAfter` | seconds, 0 = never |
| `[screensaver]` | `albums` | album IDs |
| | `interval` | seconds per photo |
| | `transition` | `slide`, `fade` or `none` |
| | `background` | `blur`, `gradient` or `black` |
| | `showInfo` | date and place on or off |
| `[viewer]` | `transition`, `background` | as above, for the photo viewer |
| `[apps]` | `youtube/closeAfter`, `jellyfin/closeAfter` | seconds an app left with nothing playing stays loaded, 0 = until closed |

The web app suspend time is `suspendAfter` (seconds, 0 = never) in
`~/.config/tvshell/apps/webapp.conf`, which every web app reads. YouTube's own are in
`~/.config/tvshell/apps/youtube.conf`:

| Key | Meaning |
|---|---|
| `backgroundKinds` | what keeps playing when you leave YouTube: `song`, `music-video`, `podcast`, `video` |
| `skipCategories` | SponsorBlock categories to skip, e.g. `sponsor, intro` |

## Install

Needs Docker on your computer and SSH access to the Steam Machine as `deck@gabecube`.

    docker build -t tv/toolchains/steamos-3.9:dev ../toolchains/steamos-3.9   # once
    ./build.sh            # build
    ./make-places.py      # the place list for photo captions (once, or to refresh)
    ./deploy.sh           # copy to the Steam Machine and (re)start it
    ./deploy.sh --enable  # start it with Gaming Mode from now on (--disable to undo)
    ./deploy.sh --stop    # stop it; Steam gets the screen back
    ./deploy.sh --log     # follow its log

## Limitations

- Music counts as music when YouTube marks it so, or offers "Switch to audio" for it; music
  it does neither for counts as an ordinary video.
- With the screen black for music, changing the volume turns the picture back on.
- After sleep from the machine's power button or Steam's power menu, the remote can't
  wake the machine; a controller or the power button can.
- While a game is running, Home, the YouTube and Netflix buttons and the power dialog don't
  bring up the shell. Use the controller's Steam button, or quit the game.
- Jellyfin: text can't be typed with the remote (search, sign-in), and its interface has one
  size, made for a 4K screen.
- "On this day" stays empty: the TV account owns no photos.
- Finer place names ("Blackeberg, Stockholm") only for Sweden and the Czech Republic so far;
  elsewhere the names come from Immich ("Stockholm, Sweden").
- The Assistant button doesn't do anything with your voice yet.
