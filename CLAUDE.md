# GabeTV: working on this project

A TV experience of our own on a Valve Steam Machine (`gabecube`), published as
https://github.com/Dethware/GabeTV. The repository is public: no secrets go into it
(keys, tokens, sign-in details). Device addresses and host names in the log are fine: the
log is a reference with the reasons why things were done, and such details help. What it is and does:
`README.md`. How the parts fit, the decisions, the hardware and what is planned:
`DEVELOPMENT.md`. This file is only about how to work on it.

## Working style

- The system's constraints are still being discovered. **Ask questions and discuss design decisions before writing larger amounts of code.** Don't fill gaps with assumptions.
- Prefer reasoning the design upfront, then a **scaffold** (a file layout resembling the end result) or one clear example to follow, then the implementation.
- After a stretch of building by trying things on the device, go over the code for refactoring and architecture before building further on it.

## Where things are written down

The split between the kinds of document is in the global `~/.claude/CLAUDE.md`.

- For users: `README.md` at the top, and one in each part (`shell/`, `controllers/`, `apps/eden/`, `apps/jellyfin-desktop/`, `apps/webapp/`, `toolchains/`, `system/steam-ui-watchdog/`, `system/wake-log/`).
- For developers: `DEVELOPMENT.md` at the top for what spans the parts (how they fit, constraints, decisions, hardware, apps, plans, open questions, ideas), and one in `shell/`, `controllers/`, `apps/eden/`, `apps/webapp/` and `shared/` for how each works inside.
- The log: `docs/gabecube-findings.md`. Dated findings, measurements, decisions and rejected alternatives go there; keep it updated as tests run. The first reconnaissance of the machine (how Gaming Mode starts, what persists, focus control, Waydroid's prerequisites) is at its start.

## Rules that follow from the decisions

The reasons are in `DEVELOPMENT.md`; these are the ones to keep in mind while working.

- Stock SteamOS, read-only root: everything we build lives in `/home`, in user systemd units, or in `/etc` with an entry in the update keep-list. Programs are plain folders, never Flatpak or similar.
- Build in the toolchain images (`toolchains/`), against SteamOS's own Qt. The shell's interface is Qt/QML, no web stack.
- Every screen must work with a d-pad or a TV remote alone. Places have stable positions and never reorder by themselves.
- The Nintendo controllers belong to our controller service and nothing else. Don't build on Steam's Joy-Con support, `joycond`, SDL's Joy-Con driver or an emulator's own driver; patch the emulator to fit instead.
- Mind licenses: yuzu/Suyu/Eden code is GPL-3. What links into Eden is GPL-3; don't copy its code into the other parts.

## On the machine

- `ssh deck@gabecube`. The user runs anything that needs sudo.
- Deploying restarts the shell or the controller service, and restarting that service ends the formed controllers (L+R again): ask before each deploy.
- Secrets stay on gabecube (the Immich key in `~/.config/tvshell/immich.conf`): never print them or pass them through the conversation.
