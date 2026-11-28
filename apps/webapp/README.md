# webapp

Shows a website full screen on the TV as a program of its own, which the TV shell starts and
shows from its tile. Each website is a site in `sites/`; YouTube in its TV interface
(`youtube.com/tv`) is the first.

Because it runs apart from the shell, a site stays loaded, signed in and where you left it
when the shell restarts.

## Use

- Open YouTube from its tile on Home, or with the remote's YouTube key. Home brings back the
  shell; Back on YouTube's "Who's watching?" screen, or its own Exit, returns to where you
  opened it from (Steam, if Steam had the screen).
- Leaving YouTube pauses a video, and opening it again continues it; meanwhile the shell's
  top bar shows it, and OK there opens YouTube. Songs, music videos and podcasts keep
  playing instead and show in the top bar, where OK pauses and continues them; paused
  there for ten minutes, they're done. Which kinds keep playing is up to you (Settings).
- When something else starts playing (a film in Jellyfin, a game), YouTube pauses and stays
  paused.
- Signing in to YouTube: it shows a code to enter at `youtube.com/activate` on a phone.

## Settings

In the shell's Settings:

- **YouTube**: what keeps playing when you leave YouTube (songs that show only their cover,
  music videos, podcasts, other videos), and what SponsorBlock skips.
- **Apps**: the web app suspend time, how long a web app stays open when you leave it with
  nothing playing before it is suspended (half an hour at first; opening YouTube then
  reloads the video where it was); and whether YouTube stays loaded, or closes after 10
  minutes, 30 minutes or an hour.

The shell keeps what applies to every web app in `~/.config/tvshell/apps/webapp.conf` and a
site's own settings in `~/.config/tvshell/apps/<site>.conf`.

## Install

Build with `./build.sh` (needs the toolchain image, `../../toolchains`), then
`./deploy.sh` copies it to `~/apps/webapp/` on the Steam Machine and installs its service.
Sign-ins and site data are kept in `~/.local/share/webapp/<site>/`.
