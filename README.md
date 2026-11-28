# GabeTV

A TV experience of our own on a Valve Steam Machine (the "gabecube"): one box in place of a
smart TV's apps, a streaming dongle and a game console. The TV itself stays a plain,
offline display.

It is a personal project, built for one machine and one living room. Looks and layouts are
placeholders, and nothing is packaged for anyone else's setup.

## What it does

- **A home screen for the TV** that runs inside Steam's Gaming Mode, next to Steam. YouTube
  in its TV interface, Jellyfin for films and shows, your Steam games, and photos from an
  Immich server, which also make the screensaver. Everything works with a TV remote or a
  controller.
- **Nintendo Switch games** in the Eden emulator, with real Joy-Cons, the Pro Controller and
  the Ring-Con. Ring Fit Adventure plays with the ring and the leg strap. Home parks a game
  and its tile brings it back where it was.
- **The Nintendo controllers behave as they do on a Switch**: hold the sync button to add
  one, press L+R or SL+SR to say how you hold it, and it is a controller for everything on
  the machine.

## What is where

| Folder | What | Read |
|---|---|---|
| `shell/` | The TV's home screen | [shell/README.md](shell/README.md) |
| `controllers/` | The service that owns the Joy-Cons and the Pro Controller | [controllers/README.md](controllers/README.md) |
| `apps/webapp/` | YouTube (and other websites) as programs of their own | [apps/webapp/README.md](apps/webapp/README.md) |
| `apps/eden/` | The Eden emulator, built for this machine | [apps/eden/README.md](apps/eden/README.md) |
| `apps/jellyfin-desktop/` | Jellyfin's player, built for this machine | [apps/jellyfin-desktop/README.md](apps/jellyfin-desktop/README.md) |
| `system/` | Small things on the machine itself: the remote's keys and pairing, a watchdog for Steam's interface, a log of what wakes the machine, a tone that keeps the soundbar from clicking | [system/steam-ui-watchdog/README.md](system/steam-ui-watchdog/README.md), [system/wake-log/README.md](system/wake-log/README.md), [system/soundbar-keepalive/README.md](system/soundbar-keepalive/README.md) |
| `toolchains/` | The build environment that matches the machine's SteamOS | [toolchains/README.md](toolchains/README.md) |

## Building and installing

SteamOS can't build anything itself and keeps its system read-only, so each part is built
on another computer in a container (Docker) and copied into the Steam Machine's home
folder. Every part has its own `build.sh` and `deploy.sh`, described in its README; the
toolchain image comes first.

How the parts fit together and why: [DEVELOPMENT.md](DEVELOPMENT.md).
