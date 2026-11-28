# gabecube: findings

The project's log: what we've learned about the Steam Machine and the shell by probing and
using them, with dates. Entries are added, not rewritten; a later entry corrects an earlier
one. Everything here was observed on the machine unless marked *inferred*. How things work
now is in [shell/DEVELOPMENT.md](../shell/DEVELOPMENT.md); how to use them in
[shell/README.md](../shell/README.md).

## System (2026-11-21)

- SteamOS 3.9.5, build 20261023.100, `steamdeck_stable` Steam client branch.
- Kernel `7.2.7-valve2-2-neptune-72`. Board `Fremont` (Valve), 16 GB RAM.
- gamescope 3.16.30.1, Mesa 26.1.2, steamos-manager 26.4.1, SDDM 0.21.
- Output: `HDMI-A-1` to the old AMH monitor (EDID "A409U"), 60 Hz only, no HDR, no VRR.
- `deck` is in `wheel`, but sudo needs a password (none usable over SSH yet).

## How Gaming Mode starts

1. SDDM autologin: `/usr/lib/sddm/sddm.conf.d/holo.conf` sets `User=deck`,
   `Session=gamescope-wayland.desktop`, `Relogin=true`.
2. `gamescope-wayland.desktop` runs `start-gamescope-session`, which only does
   `systemctl --user --wait start gamescope-session.target`.
3. `gamescope-session.target` (in `/usr/lib/systemd/user/`):
   - `Requires/BindsTo=gamescope-session.service` → `/usr/lib/steamos/gamescope-session`:
     exports ~50 `STEAM_*`/`GAMESCOPE_*` feature flags, then
     `exec gamescope --xwayland-count 2 -e -R <socket> -T <stats> ...`.
     It writes the session environment to `$XDG_RUNTIME_DIR/gamescope-environment`.
   - `Upholds=steam-launcher.service` → `steam -steamos3 -steampal -steamdeck -gamepadui`
     (or `~/devkit-game/devkit-steam` if present).
   - `Wants=` mangoapp, ibus, steam-notif-daemon, galileo-mura-setup.
4. `steamos-manager` switches Game/Desktop mode by writing
   `zz-steamos-autologin.conf` / `zzt-steamos-temp-login.conf` into `/etc/sddm.conf.d/`
   (*inferred* from strings in the binary; the directory is empty now).
   `steamosctl get-default-login-mode` → `game`.

## What persists

- `/` is read-only btrfs (`steamos-readonly status` → enabled). `/usr/local` is read-only too.
- `/home`, `/var`, `/opt`, `/srv`, `/root`, `/nix`, `/var/lib/flatpak` are writable and persistent.
- `/etc` is an overlay (upper in `/var/lib/overlays/etc/upper`). An atomic update **drops all
  `/etc` changes** except the keep-list `/usr/lib/rauc/atomic-update-keep.conf`. Relevant kept paths:
  - `/etc/sddm.conf.d/*`
  - `/etc/systemd/system/*.service`, `*.service.d/**`, `*.wants/**`
  - `/etc/environment`
  - `/etc/atomic-update.conf.d/*.conf` (add our own keep entries here)
  - **Not kept:** `/etc/systemd/user/`.
- User unit search path puts `~/.config/systemd/user` before `/usr/lib/systemd/user`, so we can
  override or extend every Gaming Mode unit without root.
- logind has `KillUserProcesses=True`: processes started from SSH die when the SSH session ends.
  Start long-lived test processes with `systemd-run --user`.

## Custom session: options

- **User-unit drop-ins (no root):** extend or replace pieces of `gamescope-session.target`
  from `~/.config/systemd/user`. Autologin and the Desktop/Game switch keep working.
  Risk: a broken override makes the session restart-loop (SSH still works).
- **Own SDDM session (root):** both session dirs are read-only; a file in `/etc/sddm.conf.d/`
  could add a `/home` session dir, but would compete with steamos-manager's autologin files.

## gamescope focus control (tested 2026-11-21)

**Summary:** inside the stock session, with no changes to it, a program of ours can:
show itself by tagging its window `STEAM_GAME=<id>` and putting `<id>` first in
`GAMESCOPECTRL_BASELAYER_APPID`; launch games via `steam://rungameid/`; get the screen back
by re-claiming the base layer after Steam rewrites it on game launch/exit; and read the
controller from Steam's virtual Xbox pad. The Steam menu and QAM still work on top of it.

gamescope runs with `-e` (Steam integration) and reports
`Virtual connector strategy: SteamControlled`. Focus is decided by X properties on display `:0`.

| Step | Action | Result |
|---|---|---|
| 1 | Plain X11 window on `:0` | Mapped and resized to full screen, but **not focusable, not shown** |
| 2 | Set window property `STEAM_GAME=424242` (CARDINAL) | Listed in `GAMESCOPE_FOCUSABLE_APPS`, still not shown |
| 3 | Set root property `GAMESCOPECTRL_BASELAYER_APPID=424242,769` | **Shown immediately**; focus, input and keyboard focus on it |

- The first app ID in `GAMESCOPECTRL_BASELAYER_APPID` wins. 769 is Steam's own UI.
- Steam idle (screensaver): it did not rewrite the property during the 17 s we held it.
- Restoring the property to its previous value put Steam back on screen.
- Steam's normal value was `413091, 769`; 413091 is not identified yet.

### Game launch from our window (tested 2026-11-21)

Our window held the base layer (`424242, 769`), then we ran
`steam steam://rungameid/1233070` (Isles of Sea and Sky) from outside Steam, with the
environment from `gamescope-environment`. Focus log (0.5 s polling):

| Time | `BASELAYER_APPID` | Focused | Event |
|---|---|---|---|
| 14:47:19 | `424242, 769` | 424242 | our window on screen |
| 14:47:21 | | | `steam://rungameid/1233070` sent |
| 14:47:22 | `413091, 1233070, 769` | 769 | Steam **overwrote our value** at once |
| 14:47:28 | `413091, 1233070, 769` | 1233070 | game window up and on screen |
| 14:48:18 | | | game's scope stopped (`systemctl --user stop app-steam-app1233070-*.scope`) |
| 14:48:20 | `413091, 769` | 769 | Steam shows the game's library page; our window stays hidden |
| 14:48:55 | `424242, 769` | 424242 | we set it again: back on screen, held ≥ 15 s with Steam idle |

- The `steam://` URL works from outside Steam; the running client picks it up.
- Steam rewrites the base layer on launch and on exit, dropping our app ID both times.
  Our window stays focusable (`GAMESCOPE_FOCUSABLE_APPS` keeps 424242).
- So returning to our shell after a game means **watching the root properties and re-claiming**
  when the game's app ID disappears. Steam doesn't fight a re-claim while idle.
- The game ran on the second Xwayland (`:1`, `STEAM_MULTIPLE_XWAYLANDS=1`) in its own
  systemd scope `app-steam-app<id>-<pid>.scope`; stopping that scope is a clean way to kill a game.

### Steam button, Quick Access Menu, controller input (tested 2026-11-21)

Our window on the base layer; someone at the TV pressed controller buttons, the Steam button and the
**…** button (14:49–14:51).

- `GAMESCOPECTRL_BASELAYER_APPID` **did not change** during any of it: the Steam menu and
  the QAM are overlays above the base layer, not focus switches.
- Our window saw only X focus-out/focus-in pairs (8 of them) while the overlays were open.
  It got **no key or mouse events**: Steam Input doesn't map the controller to keyboard/mouse
  for an app ID it doesn't know.
- The Steam Controller's raw HID devices (`hidraw2-5`) are held by Steam. Its evdev nodes
  (`Steam Controller Puck`, event6/7/12/17) and Steam's virtual **"Microsoft X-Box 360 pad 0"**
  (`event19`, via uinput) are readable by `deck` through ACLs.
- Second round (14:51–14:52), evdev capture while our window had focus:
  - **Steam's virtual pad (`event19`) delivered everything**: buttons as `BTN_SOUTH/EAST/
    NORTH/WEST` (304/305/307/308), bumpers (310/311), Start (315); sticks on `ABS_X/Y/RX/RY`,
    triggers on `ABS_Z/RZ`, d-pad on `ABS_HAT0X/Y`. It also streams stick jitter at rest.
  - The controller's own evdev nodes (`Steam Controller Puck`) sent **nothing**: Steam has
    the hardware in its own mode.
  - The pad is recreated when the session restarts (it got `event19` again by chance), and
    has no `/dev/input/by-id` link: find it by name.
  - So our shell can read the controller as a standard gamepad (evdev or SDL) from `event19`,
    with Steam Input still active. *Inferred:* the same pad feeds games, so the shell must
    ignore input whenever `GAMESCOPE_FOCUSED_APP` isn't its own app ID.
  - Not checked: whether the pad keeps sending while the Steam menu/QAM is open, and whether
    `event19` stays the same device across Steam restarts.
- `cecd` exposes an evdev device (`cecd cros-ec-cec`, event13): TV remote keys over CEC may
  arrive as plain key events.

### Steam restart (tested 2026-11-21)

Our window held the base layer; then `systemctl --user restart steam-launcher.service`.

- Steam was back in ~3 s and **reset the base layer to `413091, 769`**, taking the screen.
  Our window stayed focusable.
- `gamescope-session.target` has `Upholds=steam-launcher.service`: stopping Steam is
  immediately followed by a start. To change Steam's files while it's down, act within ~2 s
  of the stop, or stop the whole session.
- Every Steam start (also each Gaming Mode restart) plays Steam's **startup movie**, loud,
  before Big Picture appears.
- Steam's own settings for this live in its config: `"CurrentScreensaver" "steam-bouncinglogo"`
  and a startup-movie list (`GetStartupMovies`).
- Re-claim rule that covers launch, exit and restart: claim the base layer whenever its list
  holds only Steam's own entries (`413091`, `769`), never while a game's app ID is in it.
- `413091` isn't an installed app; it appears only in another app's depot list in Steam's
  cache. Probably a Steam-internal placeholder (*inferred*).

### Our program as a non-Steam shortcut ("B-lite", tested 2026-11-21)

Added with `steam steam://addnonsteamgame/<url-encoded path>` (after `touch /tmp/addnonsteamgamefile`;
this is what `steamos-add-to-steam` does). Steam wrote `userdata/<id>/config/shortcuts.vdf` at once;
shortcut app ID 3564817749, launched with `steam://rungameid/<(appid << 32) | 0x02000000>`.

| Time | `BASELAYER_APPID` | Focused | Event |
|---|---|---|---|
| 15:12:22 | | | `steam://rungameid/15310775648188891136` (our shortcut) |
| 15:12:23 | `413091, 3564817749, 769` | 769 | Steam inserts the shortcut's app ID |
| 15:12:32 | `413091, 3564817749, 769` | 3564817749 | our window on screen (~9 s after launch) |
| 15:12:34 | | | `steam://rungameid/1233070` (real game) sent while ours runs |
| 15:12:39 | `413091, 1233070, 769` | 1233070 | game on top, no prompt |
| 15:12:49 | | | game's scope stopped |
| 15:12:52 | `413091, 3564817749, 769` | 3564817749 | **Steam put our window back by itself** |

- Steam started it via `reaper SteamLaunch AppId=3564817749 -- <exe>` in scope
  `app-steam-app3564817749-<pid>.scope`, on Xwayland `:1`, with `SteamGameId`, `SteamAppId`,
  `SteamOverlayGameId` set. gamescope derived the app ID itself; no `STEAM_GAME` property needed.
- **A Steam dialog popped up on screen** during the launch: several controller images and
  text about an "external controller"; it had to be confirmed with the controller.
  *Inferred:* a per-game controller prompt Steam shows for a game it has no controller info
  for (a new non-Steam shortcut has none). It's not in Steam's
  logs. It likely explains part of the ~9 s start. Unknown whether it comes back on every launch.
- Steam Input applied the gamepad template `controller_neptune_gamepad_fps.vdf`: no
  keyboard/mouse events in our window; reading the virtual pad would still work.
- So in B-lite, Steam handles returning to our shell after games; no property juggling.
- Things to weigh (*inferred*, not tested): Steam considers a game running all the time
  (Steam's "downloads during gameplay" setting, idle/suspend behavior, the Steam menu showing
  our shell as the running game); slow (~9 s) start; our shell lives inside Steam's library.
- Removing the shortcut: stop Steam and delete `shortcuts.vdf` (it didn't exist before);
  Steam keeps a playtime entry for the app ID in `localconfig.vdf`.

### Steam button, Quick Access and Home while the shell has the screen (tested 2026-11-21)

- Steam keeps the Steam and … buttons (they never reach SDL). Its main menu and Quick
  Access are drawn **in Steam's own window**, so with the shell on top they open unseen
  (only Steam's sounds play).
- The shell's window does **not** lose X focus when they open (a Tk test window did earlier).
- Steam button: Steam writes root `GAMESCOPE_BLUR_MODE` (value 0) each time its main menu
  opens; nothing when it closes. The shell uses that to step aside (works).
- Quick Access (…): **no X property change at all**, neither on the root nor on Steam's
  window (`STEAM_OVERLAY`, `STEAM_INPUT_FOCUS`, `STEAM_NOTIFICATION` unchanged). Only Steam's
  logs show it: `logs/webhelper.txt` `QuickAccess_uid2-'QuickAcces': WasHidden 0` (open) /
  `WasHidden 1` (closed); `logs/controller_ui.txt` `Guide button sent to JS` for the Steam
  button.
- Remote Home: a passive key grab on the root window (`xcb_grab_key`, keycode 180 =
  `XF86HomePage`) delivers it to the shell even while Steam has focus (works).
- Controller: **while Steam's own UI has focus, Steam doesn't feed its virtual pad** (no
  events at all reach SDL), so no controller gesture can reach the shell then. Buttons are
  seen only while the shell (or a game) has focus.
- Steam's Big Picture shortcut **Ctrl+2** toggles Quick Access (sent with XTest /
  `xdotool key ctrl+2` while Steam is focused; logged as `WasHidden 0`/`1`). Ctrl+1 is
  presumably the main menu (not tried).
- Handing the screen to Steam right after Quick Access opened: once Steam closed it again
  ~1 s later (focus change), twice it stayed open.
- Steam's UI is Chromium (CEF); Decky Loader hooks it via CEF remote debugging
  (`.cef-enable-remote-debugging`, not enabled here). Option for deeper integration.

### Native Wayland clients (tested 2026-11-21)

- gamescope's socket `gamescope-0` advertises `xdg_wm_base` v3 and `zwlr_layer_shell_v1` v4,
  although the session doesn't pass `--expose-wayland`. Full list: `wayland-info` with
  `WAYLAND_DISPLAY=gamescope-0`.
- A GTK3 window with `GDK_BACKEND=wayland` connects and maps without errors, but is
  **never focusable or shown**, even with `424242` first on the base layer. Tried giving it an
  app ID three ways, none worked:
  1. `SteamLaunch AppId=424242` in its own command line;
  2. the same on a parent process;
  3. running it in a systemd scope named `app-steam-app424242-tvprobe.scope`
     (gamescope logs "AppID %u derived from cgroup for pid %d", so it reads cgroups somewhere).
- An X11 window in a scope named the same way didn't get an app ID either; it has no
  `_NET_WM_PID`, which may matter. Setting `STEAM_GAME` is the only route that worked.
- **Consequence:** in the stock session our shell has to be an X11 client (e.g. Qt with
  `QT_QPA_PLATFORM=xcb`, Chromium/Electron on X11).

#### With `--expose-wayland` (tested 2026-11-21, then rolled back)

Added via a user drop-in, no root and nothing in `/usr` touched:
`~/.config/systemd/user/gamescope-session.service.d/*.conf` set `Environment=PATH=<dir>:...`,
where `<dir>/gamescope` is a wrapper running `/usr/bin/gamescope --expose-wayland "$@"`.
This works because `/usr/lib/steamos/gamescope-session` runs `gamescope` by name.
Restarting Gaming Mode = `systemctl --user stop gamescope-session.target` (run via
`systemd-run --user`); SDDM's `Relogin=true` starts it again in ~40 s, with Steam.

- Session came up normally with the flag: Steam UI, mangoapp, all units active.
- The GTK Wayland window is **still not** in `GAMESCOPE_FOCUSABLE_*` (those list X windows only).
- Wayland windows get **app ID 0**, also when run in an `app-steam-app424242-*` scope.
- `GAMESCOPECTRL_BASELAYER_APPID=0` → the Wayland window **is shown** (gamescope's focus
  window `0x2`, appID 0) and gets keyboard focus. `424242` doesn't select it.
- GTK draws client-side decorations (title bar) unless the window goes fullscreen.
- Caveats: app ID 0 isn't unique to us; any other app-ID-less Wayland window would compete.
  No known way yet to give a Wayland window a real app ID.
- gamescope also logs that it looks for scripts in `/etc/gamescope/scripts` and
  `~/.config/gamescope/scripts` (both absent); see "gamescope Lua scripts" below.

Useful commands (with `DISPLAY=:0`, `XDG_RUNTIME_DIR=/run/user/1000`, `GAMESCOPE_WAYLAND_DISPLAY=gamescope-0`):

```bash
xprop -root GAMESCOPE_FOCUSED_APP GAMESCOPECTRL_BASELAYER_APPID GAMESCOPE_FOCUSABLE_APPS
gamescopectl focus_info      # dumps focus state into the gamescope-session journal
gamescopectl screenshot /tmp/x.png
```

## gamescope Lua scripts and convars (checked 2026-11-21)

- gamescope runs `*.lua` from `/usr/share/gamescope/scripts`, `/etc/gamescope`, and
  `~/.config/gamescope/` (user dir, no root, persistent). README: `/usr/share/gamescope/scripts/README.md`;
  it calls the system experimental and liable to change between versions.
- What scripts can do:
  - Define **known displays** (`gamescope.config.known_displays.<name>`): HDR metadata,
    colorimetry, refresh-rate lists, mode generation, matched by EDID vendor/product.
    Valve ships these for handheld panels only; one for the LG G6 could go here if needed.
  - Set **convars** (`gamescope.convars.<name>.value`).
  - **One hook only:** `OnPostPaint` (every frame). No window, focus or input hooks, so
    scripts **can't** replace the base-layer approach.
- Convars can also be changed at runtime: `gamescopectl <convar> <value>`. Full list: `gamescopectl help`.

### Screen off without CEC (tested 2026-11-21)

`gamescopectl drm_sleep_external_screen 1` → `/sys/class/drm/card0-HDMI-A-1/dpms` went
`Off` and `enabled` → `disabled` (HDMI signal stops); `... 0` → back `On` within 3 s.
Steam kept focus throughout.

- This is a candidate for the idle policy (screensaver → dim → **signal off**), with no CEC.
  The shell uses it now (2026-11-21), and for the remote's Power.
- The shell takes the sleep key from logind with an inhibitor lock
  (`Inhibit("handle-suspend-key", …, "block")` over D-Bus, no root); logind still logs
  "Suspend key pressed short" but doesn't suspend.
- Root `GAMESCOPE_INPUT_COUNTER` increments on keyboard-type input through gamescope (seen
  for the remote's keys, including Power); a key press handled by a root grab bumps it too.
- Steam's own idle settings live in `~/.local/share/Steam/config/config.vdf`:
  `IdleBacklightDimACSeconds`, `IdleScreensaverACSeconds`, `IdleSuspendACSeconds` (also
  `…BatterySeconds` variants) and `CurrentScreensaver`. A value only appears once changed
  from Steam's default. Defaults seen: dimming off, screensaver after 5 min, sleep after
  60 min. Since 2026-11-21 all three AC values are `0` (off); the shell's idle chain replaces
  them. Steam's screensaver seemed to close open Steam menus (with sounds) when it started.
- Untested: what the LG G6 does on signal loss (standby after a timeout?), and how it
  wakes when the signal returns. Nothing wakes the output automatically: our shell would
  set it back to `0` on input.

## Switching off the Steam Controller (tested 2026-11-21)

- The controller connects through its USB puck, `28de:1305` ("Valve Software Steam Controller
  Puck"; SDL calls it the Nereid dongle, `1304` = Proteus dongle). It exposes four HID
  interfaces (`hidraw2`–`5`), all with a uaccess ACL for `deck`; Steam has them open too.
- SDL's driver for the new controller (`SDL_hidapi_steam_triton.c`, "Triton") uses the same
  feature-report messages as the classic Steam Controller. `ID_TURN_OFF_CONTROLLER = 0x9F` is
  defined there but unused.
- Sending `01 9F 04 6F 66 66 21` (report ID 1, type 0x9F, length 4, "off!"), padded to 64 bytes,
  with `HIDIOCSFEATURE` as `deck`: accepted on `hidraw2`, `EPIPE` on the other three (no
  controller in those slots). Steam's `logs/controller.txt` then showed the wireless link
  ending ("Disconnect (Spontaneous)"). No root, Steam kept running.

## Google TV remote over Bluetooth (tested 2026-11-21)

Remote: Chromecast with Google TV (2020) voice remote. Pairing mode: hold **Back + Home**
until the light pulses.

- Paired from SSH with `bluetoothctl --agent NoInputNoOutput` (`pair`, `trust`, `connect`).
  The first `pair` timed out after 20 s, the second succeeded. No root needed.
- Re-pairing after using the remote with the Chromecast again (2026-11-21, after a reboot):
  bluetoothd logged `Failed to add device FC:A8:9B:28:99:BE (1): Failed (0x03)` four times,
  and in pairing mode the remote's light only came on for a few seconds and went out.
  Pairing with the Chromecast itself was just as flaky. **Pulling the batteries and putting
  them back fixed it**: the remote paired right away and the shell found its voice service.
  So the remote's own state is the likely culprit, not gabecube (inferred).
  `system/pair-remote.sh` forgets the old pairing, waits for the advert and retries
  pair/trust/connect; written for this but not needed in the end (untested).
- Shows up as BLE HID "Chromecast Remote" (`18D1:9450`, hid-generic via uhid), one evdev
  device, plus Battery Service (battery level readable via `bluetoothctl info`).
- Stayed connected to gabecube for 5+ minutes after pairing; the dongle didn't take it back.
- udev doesn't classify it as a keyboard (`ID_INPUT_KEY` only, no `ID_INPUT_KEYBOARD`), so
  it gets **no `uaccess` tag**: `deck` can't read its evdev node; only root can.
- gamescope does pick it up (it opens devices via logind). Keys our X11 window received:

  | Button | X11 keysym (keycode) |
  |---|---|
  | Ring up/down/left/right | `Up` (111) `Down` (116) `Left` (113) `Right` (114) |
  | Back | `XF86Back` (166) |
  | Home | `XF86HomePage` (180) |
  | Mute | `XF86AudioMute` (121) |
  | Volume rocker (side) | `XF86AudioRaiseVolume` (123) / `XF86AudioLowerVolume` (122) |
  | Power | `XF86ScreenSaver` (160) |
  | **OK (centre)**, Assistant, YouTube, Netflix, Input (bottom right) | nothing |

- Raw codes, read with `sudo evtest` (pressed OK, YouTube, Netflix, Assistant, Input, Power):

  | Button | HID usage (`MSC_SCAN`) | evdev key (code) |
  |---|---|---|
  | OK | `c0041` | `KEY_SELECT` (353) |
  | YouTube | `c0077` | `KEY_CAMERA_ACCESS_DISABLE` (588) |
  | Netflix | `c0078` | `KEY_CAMERA_ACCESS_TOGGLE` (589) |
  | Input (*inferred*, HID "Media Select TV") | `c0089` | `KEY_TV` (377) |
  | Power | `c019e` | `KEY_SCREENLOCK` (152) |
  | Assistant | — | nothing over HID (*inferred:* voice uses Google's vendor GATT service) |

  The device also declares `KEY_PLAYPAUSE`, `KEY_SEARCH`, `KEY_KBDILLUMUP` (unused by this remote).
- **Confirmed:** codes above 247 can't become X11 keycodes (evdev + 8 ≤ 255), so gamescope
  can't deliver OK, YouTube, Netflix or Input to X11 clients (Steam's UI included).
- Power arriving over Bluetooth means the shell can react to it (e.g. screen off / sleep),
  besides whatever the remote's IR side does to the TV.
### Key remap (installed 2026-11-21)

Files in this repo under `system/etc/`, installed with root to the same paths:
- `udev/hwdb.d/90-chromecast-remote.hwdb`: OK → `enter`, YouTube → `prog1`, Netflix → `prog2`,
  Input → `prog3`, Power → `sleep`.
- `atomic-update.conf.d/tv-remote.conf`: keeps the `.hwdb` file across SteamOS updates.

Install (as root): copy both, then `systemd-hwdb update` (writes `/etc/udev/hwdb.bin`, which
shadows `/usr/lib/udev/hwdb.bin`) and `udevadm trigger --action=change --subsystem-match=input`.
The trigger put the `KEYBOARD_KEY_*` properties on the device, but the keys only changed
**after the remote reconnected** (here: a suspend/resume cycle).

After an OS update, `/etc/udev/hwdb.bin` is dropped (not on the keep-list) and the `.hwdb`
stays. `systemd-hwdb-update.service` (`ConditionNeedsUpdate=/etc`, runs when
`/etc/udev/hwdb.d/` isn't empty) should rebuild it at boot. **Unverified until the next update**;
if it doesn't, the remap is just missing until `systemd-hwdb update` runs again.

Result in our X11 window:

| Button | Now arrives as |
|---|---|
| OK | `Return` (36) |
| YouTube / Netflix / Input | `XF86Launch1` (156) / `XF86Launch2` (157) / `XF86Launch3` (210) |
| Power | logind: "Suspend key pressed" → **suspends the Steam Machine at once** (`HandleSuspendKey=suspend`, remote is tagged `power-switch`) |

- Input confirmed as `c0089` (it produced `XF86Launch3`). Assistant sends no key (see below).
- Games can use it too: Isles of Sea and Sky was playable with the remote (keyboard input).
- Suspend test: Power at 15:46:26 → s2idle. **Pressing the remote didn't wake it**; the
  Steam Controller's Steam button did (15:46:41). bluetoothd logged
  "Controller resume with wake event 0x0" (not a Bluetooth wake). The remote reconnected
  18 s after resume, on its next button press. Our window got one `XF86WakeUp` on resume.
- Wake path looks allowed: the BT adapter is Valve's USB module `28de:1401` (btusb) on the
  same xHCI (`0000:07:00.3`) as the Steam Controller puck, both with `power/wakeup=enabled`;
  bluez reports `WakeAllowed: yes` for the remote; Valve set `ScanIntervalSuspend=2240`,
  `ScanWindowSuspend=224` in `/etc/bluetooth/main.conf`. Why the remote doesn't wake it
  is unknown.
- `btmon` trace across a suspend (2026-11-21 15:56–15:57; adapter reports as Qualcomm):
  - Suspend: kernel disconnects the remote ("terminated by local host for suspend"), sets
    LE passive scan, filter policy "ignore not in accept list", 1M PHY 1400 ms interval /
    140 ms window (+ Coded PHY 4200/420 ms), scan enabled; MGMT "Controller Suspended:
    page scanning and/or passive scanning". So wake-on-LE *is* configured.
  - 10 ms later the remote advertises (`ADV_IND`, connectable, appearance Keyboard,
    LE Limited Discoverable) and the controller reports it (still during suspend entry).
  - Then **no events for 40 s** while OK was pressed repeatedly on the remote; resume came
    from the Steam Controller ("wake event 0x0"); the remote reconnected 10 s after resume.
  - Running `btmon` from an SSH session dies with the suspend; start it with `systemd-run`.
- Second trace (16:01–16:02) with `ScanWindowSuspend=2240` (= interval, continuous scan;
  temporary edit of Valve's `/etc/bluetooth/main.conf`, reverted afterwards):
  - Remote is on the controller's accept list (`LE Add Device To Accept List` at startup).
  - Suspend scan: window = interval 1400 ms, accept-list filter, **`Filter duplicates: Enabled`**.
  - The remote's `ADV_IND` is reported 3 ms after the suspend scan starts, then **nothing for
    50 s** of button presses; still no wake.
  - On resume (Steam Controller) the kernel re-enables scanning and the remote's advert is
    reported **at once**, connection 47 ms later: it had been advertising during suspend.
  - **Conclusion (*inferred*, fits both traces):** the controller's duplicate filter already
    saw the remote in that first report, so later adverts are dropped as duplicates and never
    reach the host to wake it. The kernel sets that filter; no bluez setting changes it.
    Not the scan duty cycle.
- Wake with the remote is therefore not possible on stock SteamOS as is. Options: USB IR
  receiver learning the remote's IR power code; CEC; a kernel fix (report upstream/to Valve).
- For the shell to decide what Power does (screen off vs. suspend), it can hold a logind
  `handle-suspend-key` inhibitor lock; logind then leaves the key to us.

Alternative not used: a udev rule adding `uaccess` so our shell reads the remote's evdev directly.

### GATT services and the Assistant button (tested 2026-11-21)

Services (via bluez D-Bus `GetManagedObjects`, readable as `deck`):
standard GAP/GATT/Device Info/Battery (`180f`)/Scan Parameters (`1813`)/HID (`1812`), plus
- `ab5e0001-5a21-4f05-bc7d-af01f617b664`: **Android TV Voice** ("ATVV"). Chars: `ab5e0002`
  TX (write), `ab5e0003` audio (notify), `ab5e0004` control (notify).
- `f000bbc0-0451-4000-b000-000000000000`: TI OAD (*inferred:* firmware update).
- `d343bfc0-5a21-4f05-bc7d-af01f617b664`, `6c6f6701-5a21-…`: Google vendor
  (*inferred:* settings, maybe the IR code setup).

Subscribed (`StartNotify` over D-Bus as `deck`, **no root**) to ATVV control + audio while
pressing Assistant 4×, without any voice handshake:

- Press → control `04 03 02 <n>` (`n` counts up: `23`, `24`, `25`, `26`), then audio packets
  (160 bytes each, ~40/s) for as long as the button is held.
- Release → control `00 02`, audio stops.
- *Inferred from the Android TV voice protocol:* `04` = audio start, `00` = audio stop; the
  audio is the remote's microphone (likely ADPCM).
- That behaviour (hold-to-talk) was the remote's state left over from the dongle. After the
  remote reconnected a few times it sent **nothing** for Assistant until the host sent a
  capabilities request; the host has to pick the interaction model.

#### Voice protocol details (2026-11-21, evening)

Reference: Google's Android TV reference remote firmware in AOSP,
`platform/hardware/telink/atv/refDesignRcu` @ `47d234ba…`, `application/audio/gl_audio.{c,h}`.
The Chromecast remote runs different firmware (its replies differ in layout) but the
commands below work with it.

- Host → remote on `ab5e0002` (write without response):
  - GET_CAPS `0A <ver hi> <ver lo> <codecs hi> <codecs lo> <model>`. Version `01 00` selects
    protocol 1.0. **Model is a plain value**: `00` on request, `01` press-to-talk, `03`
    hold-to-talk; any other value (e.g. `07`, `04`, `02`) makes the remote ignore the button.
  - MIC_OPEN `0C <mode>` (2 bytes; on-request model), MIC_CLOSE `0D <stream id>`,
    MIC_EXTEND `0E <stream id>`.
- Remote → host on `ab5e0004` (notify): CAPS_RESP `0B …`, AUDIO_START `04 <reason> <codec>
  <stream>`, AUDIO_STOP `00 <reason>`, START_SEARCH `08`, SYNC `0A …`, MIC_OPEN_ERROR `0C <code>`.
- The Chromecast remote's CAPS_RESP for `0a 01 00 00 03 03`: `0b 01 00 02 03 00 a0 01 00 1a`
  = version 1.0, codec `02` (ADPCM 16 kHz), model `03` (confirms the request; it stayed `00`
  for invalid values), frame size `00 a0` (160), then `01 00 1a` (unknown).
- Hold-to-talk: press → `04 03 02 <stream>` + audio, release → `00 02`. No host replies.
  On request: press → `08`; the host must send MIC_OPEN; the stream ends with MIC_CLOSE or a
  15 s timeout (`00 08`).
- Audio (reference firmware): IMA ADPCM, 240 samples per packet (15 ms at 16 kHz), sent as
  raw chunks of the frame size; predictor/step index travel only in SYNC messages on the
  control characteristic.
- Audio (Chromecast remote, captured and decoded 2026-11-21): **160-byte notifications of raw
  IMA ADPCM, no header**, ~50/s = 16 kHz mono; **high nibble first**. No SYNC messages were
  sent, so each press decodes as one stream from predictor 0 / step index 0. (Low nibble first
  drifts away; a smoothness metric favoured the wrong order, the waveform picture didn't.)
  Microphone level is low: speech peaks ~7% of full scale. Sounds clear when amplified.
- The shell (2026-11-21) sends GET_CAPS with hold-to-talk on every (re)subscribe, decodes the
  audio while Assistant is held (ring follows the decoded level) and plays it back on release
  through miniaudio (picked "HDMI / DisplayPort Digital Stereo" via its PulseAudio backend).
  Works.
- With two D-Bus clients subscribed (StartNotify) to the same characteristic, each client
  received every notification twice.

### IR programming service (2026-11-22: a test code loaded, the remote sends it)

The remote sends no infrared now (phone camera: the soundbar's remote shows up, this one
doesn't). *Inferred:* no IR codes are loaded. It exposes the service the reference firmware
uses to load them:

| Characteristic | Flags on the remote | Reference firmware name |
|---|---|---|
| `d343bfc1-5a21-4f05-bc7d-af01f617b664` | write | Program Control |
| `d343bfc2-…` | write | Key ID |
| `d343bfc3-…` | write | Code |
| `d343bfc4-…` | write | Suppress |
| `d343bfc5-…` | notify | Key Event |

(`6c6f6701-…` has one readable characteristic, `6c6f6705-…`; not looked into.)

From the reference firmware (`refDesignRcu` `main`, `vendor/827x_ble_remote/app_ir.c`,
`app_ir.h`, `app_att.c`). The UUID values are defined in a header not read; the order of the
five characteristics matches. The Chromecast remote (TI chip, other firmware) follows it
as far as tested, see below.

- Program Control, 1 byte: `01` starts programming (clears the table), `00` ends it and
  commits to flash. 30 s timeout between writes.
- Key ID, 2 bytes big-endian, Android key codes: `0018` volume up, `0019` volume down,
  `00A4` mute, `001A` power, `00B2` input. At most 5 keys.
- Code, at most 300 bytes, may arrive in several writes (the total length is computed from
  the header): byte 0 type (`0` off, `1` one sequence, `2` repeating sequence, `3` first
  plus repeating sequence, other: two sequences alternating per press), byte 1 duty cycle in
  percent, bytes 2 to 3 carrier in units of 100 Hz, bytes 4 to 5 number of mark/space pairs;
  type 2 then has a repeat delay (2 bytes), types 3 and up the second sequence's pair count
  (2 bytes); then the pairs, 4 bytes each: mark and space as big-endian counts of carrier
  cycles. The Key ID is consumed by each Code write.
- Suppress: a list of key IDs (2 bytes each) that go over Bluetooth instead of IR; an empty
  write puts all programmed keys back on IR.
- Key Event (when notifications are on): 3 bytes per press of a programmed key, `00` down
  or `01` up, then the key ID. So the host still hears about a key that went out as IR.
- Codes are kept in flash and reloaded at start.

Test at 18:00:38, over bluez's D-Bus API as `deck` (MTU 247, so a code fits in one write;
script kept nowhere, the sequence is all there is to it):

1. `StartNotify` on Key Event.
2. Program Control `01`.
3. Key ID `00 b2` (Input, which the shell doesn't use).
4. Code `01 21 01 7c 00 08` + 8 × `0e d8 07 6c`: one sequence, 33 % duty, 38.0 kHz, 8 pairs
   of 3800 cycles on (100 ms) and 1900 off (50 ms). Not a real protocol, just easy to see.
5. Program Control `00`.

All writes were acknowledged. Pressing Input then **flickers in a phone camera** (reported),
and the remote notified `00 00 b2` on press and `01 00 b2` on release. Not checked: whether
Input still also sends its Bluetooth key while it carries a code, whether a code survives a
battery change, the other code types, Suppress.

Clearing, 18:04:12: Program Control `01`, then `00`, with no key in between (the reference
firmware keeps an empty table then). Acknowledged; Input **no longer flickers** (reported)
and its presses are no longer notified on Key Event.

### Waking the machine with the remote (2026-11-22)

Tried again after a Bluetooth mouse turned out to wake the machine. Result: not possible
over Bluetooth with this remote. Connected at suspend, it can't wake the machine (above);
disconnected at suspend, it wakes the machine by itself.

- A Bluetooth mouse (JLab GO Mouse, also LE HID like the remote) connected at suspend and
  lying still wakes the machine when used (16:11). So wake-on-LE works on this hardware.
- `btmon` trace, 16:36 to 16:48, remote held off with `bluetoothctl block`, discovery on
  (`bluetoothctl scan on`; one report per discovery cycle, about every 6 s):
  - **Left untouched for 10 minutes the remote never stopped calling.** 100 `ADV_IND`
    reports plus scan responses, no gap over 16 s.
  - **Every call is identical**: `ADV_IND`, connectable and scannable, flags `0x05` (LE
    Limited Discoverable, no BR/EDR), appearance Keyboard, data
    `02 01 05 03 19 c1 03 05 02 12 18 0f 18 10 ff e0 00 ff…`.
  - **Three OK presses (after 16:46:40) changed nothing**: same type, same data. The rate
    of its calls can't be read from this trace.
- So the adapter, which lets devices wake the machine by address only, can't tell a button
  press from the remote's idle calls.
- 16:30:19, suspended with the remote paired, not blocked and disconnected: the machine
  woke by itself at 16:30:30 (no button pressed, reported). The Bluetooth adapter (`1-5`)
  counted one wake event and the remote connected 2 s after the resume.
- 16:21:14, suspended by the power button while the remote was blocked: woke after 3.7 s,
  also with a wake event on the Bluetooth adapter.
- Both fit: a disconnected remote is always calling, and a call that reaches the suspend
  scan wakes the machine. The duplicate filter (above) is what keeps a remote that was
  connected at suspend from doing the same: its first call is spent before the machine
  sleeps. *Inferred:* the few seconds before the self-wake are the slow call rate meeting
  the suspend scan's 10 % duty cycle.
- Consequence: keep the remote connected when the machine suspends (the normal case).
  Waking with the remote needs another path: its IR side with a USB IR receiver, or CEC.
- bluez behavior met on the way (5.87):
  - `bluetoothctl disconnect` doesn't keep the remote away; it was connected again 5 s
    later. `block` does.
  - After a block of some minutes, `unblock` didn't bring the remote back although it was
    calling (16:29 and 16:48); `bluetoothctl connect` did at once, and so did a resume.
    After a 3 s block, `unblock` alone was enough.
  - `bluetoothd` crashed with SIGSEGV at 16:24:08, one second after the remote was blocked
    while `bluetoothctl scan on` was starting; systemd restarted it. Seen once; blocking
    with the scan already running was fine.
  - `dbus-monitor` on the device's path shows nothing while the device is blocked.
  - The daemon logs a `wake-policy` plugin looking for `/etc/bluetooth/wake-policy.conf`
    (absent); not looked into.
- The trace file can contain the remote's link key (it connected once during the trace); it
  was deleted after reading.

## Immich (photos.peterix.cz, checked 2026-11-21)

- Server: Immich **3.0.3** (`/api/server/version`, public), with smart search, faces, map,
  reverse geocoding, OCR enabled. API description per release:
  `open-api/immich-openapi-specs.json` in the immich repository at tag `v3.0.3`.
- Auth: header `x-api-key`. Each operation names its permission (`x-immich-permission`):
  `GET /albums`, `/albums/{id}` → `album.read`; `GET /memories` → `memory.read`;
  `GET /timeline/buckets`, `/timeline/bucket`, `/assets/{id}` → `asset.read`;
  `GET /assets/{id}/thumbnail` → `asset.view`; `GET /people…` → `person.read`.
- `GET /assets/{id}/thumbnail?size=` `thumbnail` | `preview` | `fullsize` | `original`.
- `GET /albums` gives name, `assetCount`, `albumThumbnailAssetId`, but **no assets**; an
  album's photos come from `GET /timeline/buckets?albumId=` (month buckets with counts), then
  `GET /timeline/bucket?albumId=&timeBucket=` per bucket, which answers in **columns**
  (`id[]`, `ratio[]`, `isImage[]`, `thumbhash[]`, …).
- `GET /memories?for=YYYY-MM-DD` gives that day's memories with their assets (width, height,
  type); `data.year` for "on this day".
- Orientation: some photos (e.g. in "London 2025") showed sideways in the viewer until Qt's
  `Image.autoTransform` (EXIF orientation; off by default) was turned on, so at least some
  `fullsize`/`preview` images keep the EXIF orientation flag. (A sample of 300 photos from
  other albums had none.) The thumbnail endpoint also defaults to `edited=false`, i.e. without
  rotations/crops made in Immich; the shell asks for `edited=true`.

### Place names (checked 2026-11-21)

Survey of everything the TV account sees: 13 albums, 7439 assets, 3776 with GPS.

- **Cause confirmed.** 1566 assets are labelled "Rinkeby". None is within 3.8 km of Rinkeby
  (median 5.2 km); about 1240 lie within roughly a kilometre of Blackeberg centre, the rest
  towards Grimsta, Vällingby and Hässelby. Another 134 labelled "Sundbyberg" are really in Södra
  Ängby and Blackeberg.
- Immich's lookup (`server/src/repositories/map.repository.ts` at v3.0.3): the nearest row of
  `geodata_places` within 25 km, no boundaries involved. The table is GeoNames `cities500.txt`
  (population ≥ 500 or an administrative seat) **minus feature codes `PPLX` and `PPLH`**
  (`PPLX`, "section of populated place", is kept only for Australia).
- Blackeberg is **not in `cities500`**. Of the area's entries there, Bromma, Tensta and
  Hässelby Villastad are `PPLX` and dropped, so the nearest surviving points are Rinkeby,
  Sundbyberg, Kista, Solna and Stockholm. (With `PPLX` kept, the same photos would read
  Bromma / Hässelby Villastad / Tensta.)
- Recomputing "nearest `cities500` point without `PPLX`" from each asset's GPS reproduces
  Immich's label for 3355 of 3776 assets, among them all but one of the 2749 Swedish ones.
  The rest are mostly Czech places (Prague's districts, Vranov) where today's GeoNames dump
  differs from the snapshot inside the Immich image.
- Where Blackeberg does exist:
  - full GeoNames (`SE.zip`, 28 471 populated places): `PPLX`, population 0, like Råcksta,
    Södra/Norra Ängby and Vällingby;
  - OSM: a `place=suburb` **node**. No boundary polygon exists for it; the only polygon in the
    area is the borough, "Bromma stadsdelsområde" (`admin_level=9`).
- Nearest-point lookup against either finer set fixes the labels: the 1566 "Rinkeby" assets
  become Blackeberg ×1260, Grimsta ×118–139, Hässelby gård/strand ×110–130, Vällingby ×24–36,
  Råcksta ×16. For the Swedish assets the median distance to the chosen point is 0.3–0.4 km
  (Immich's labels, all countries: 2.8 km). Side
  effect: the inner city splits up too ("Stockholm" → Södermalm, Riddarholmen, Norrmalm… with
  GeoNames; Zinkensdamm, Skanstull, Klara… with OSM), so a label needs the city next to the
  district.
- `GET /timeline/bucket?…&withCoordinates=true` adds `latitude[]` / `longitude[]` columns, so
  a lookup in the shell costs no extra requests.
- Immich re-imports its place data on start when `geodata-date.txt` in `/build/geodata`
  (`IMMICH_BUILD_DATA`) differs from the date stored in the database; existing assets keep
  their names until metadata extraction runs again.
- Options weighed: a custom `cities500.txt` mounted into the Immich container (fixes every
  client, but splits Explore → Places into districts for everyone and is unofficial);
  Immich ReverseGeo (matches by boundary polygon, so it can't produce Blackeberg, and needs
  database write access); a lookup in the shell. **Chosen: the shell** (`src/PlaceLookup.*`,
  `make-places.py`): all GeoNames populated places of Sweden and the Czech Republic, each
  with its municipality, labelled "District, City".
- Result over the same albums (the shell's client code run headless on gabecube): no
  "Rinkeby" left; Blackeberg, Stockholm ×1293, Södermalm, Stockholm ×318, Grimsta,
  Stockholm ×117…; Prague and Brno show their districts; Austria, Poland and Denmark keep
  Immich's names. 45 091 places load in 18 ms; a lookup takes about 4 µs.
- GeoNames models the two countries differently: a Swedish municipality is `admin2` and
  its record is named "Stockholms Kommun"; a Czech one is `admin3` with a plain name
  ("Brno"), and Prague is a region of its own (`admin1` 52).

## Video playback in the shell (2026-11-21)

- SteamOS ships ffmpeg 8.1.2 and Qt Multimedia 6.11.1 with the ffmpeg backend; mpv and
  gst-libav are not installed. Mesa provides VA-API for the GPU.
- Immich `GET /assets/{id}/video/playback` answers Range requests (206); for a phone video it
  served a transcoded MP4 (104 MB vs. 81 MB original).
- Qt's media player can't send custom headers, so the shell proxies through 127.0.0.1
  (`ImmichMediaProxy`). Played fine, sound included: 1080p30 clips and a 3840x2160 h264
  at 77 Mb/s.
- Hardware decoding not confirmed: `libva` was loaded in the shell, but
  `radeonsi_drv_video.so` wasn't mapped when checked after playback. Check during playback if
  4K ever stutters. Harmless log noise: VDPAU probing for `libvdpau_nvidia.so`, and a
  `spaVisitChoice: parse error` from Qt's PipeWire audio about the HDMI sink's IEC958 formats.

## The shell in use

- 2026-11-21: enabled as the default interface (`./deploy.sh --enable`). Confirmed: launching
  a game from the shell, quitting it and landing back in the shell; suspend and resume; a full
  Gaming Mode restart (Steam restarting included). Steam recreates its virtual pad after a
  game and after resume; the shell picks it up again.
- 2026-11-21, 23:30: after waking from 1.5 h of sleep (no network yet), Steam's menu opened
  and its UI stopped responding: a frozen, partly blurred screen. Steam's watchdog killed the
  webhelper at 23:32:25 ("killing unresponsive browser", core dump) and reloaded its UI at
  23:32:33. The shell had stepped aside normally. *Inferred:* a Steam bug, possibly tied to
  waking without network.
- 2026-11-22: the screensaver sometimes started with a black screen, seen when it started
  over YouTube. Cause: it covered the screen with black at once, then fetched its order and
  the first full-size photo (never cached: each run starts on a new random photo). Fixed by
  preparing the next run when one ends and fading in once the first photo is up.
- 2026-11-22: Steam's UI hang is a pattern, not a one-off. Four times so far (11-21 17:17 and
  23:32, 11-22 10:32 and 10:45) Steam's webhelper logged "killing unresponsive browser" and
  dumped core, each 45 s to 2 min after Steam's main menu was shown (`MainMenu … WasHidden 0`)
  following a stretch with the shell on screen (11 min to hours). After the reload Steam works,
  and reopening it within seconds is fine (10:46:53). Not tied to resume: 10:45 came 55 min
  after waking. Steam never logs its Big Picture window as hidden (`SP BPM … WasHidden 1`) while
  the shell covers it, nor did it for a game launched from the shell. Cause unknown.
  `~/steam-hang-watch.sh` (transient user unit `steam-hang-watch`, gone after a reboot) records
  the busiest threads and the webhelper processes' wait states for 60 s whenever the menu is
  shown, into `~/steam-hang-watch.log`. Stacks need root (`ptrace_scope` 1; `gdb` and
  `eu-stack` are installed).
- 2026-11-22, evening: the watcher caught two hangs (12:13:25, killed 12:18:18; 21:00:08,
  killed 21:01:07) among 20 menu openings that day; the other 18 were fine.
  - In a healthy opening Steam's webhelper processes sit idle (waiting in `poll` or `futex`)
    and nothing of Steam's is among the busiest threads.
  - In both hangs three webhelper processes run flat out for the whole recorded minute: the
    UI's renderer at up to 100 % CPU, and the browser and GPU processes at 60 to 99 %, mostly
    not waiting on anything. So Steam's UI is spinning, not blocked.
  - Steam ends it itself: "killing unresponsive browser" (the renderer gets SIGSEGV and dumps
    core), then a new renderer starts and the UI is back. `coredumpctl info <pid>` shows the
    killed thread's stack, all inside `libcef.so` without symbols; the core file itself needs
    root.
  - The 21:00 one was the first opening from a managed app (the Steam button with Jellyfin
    on screen); the shell stepped aside as designed and Home brought it back 14 s later while
    Steam was still spinning. The 12:13 one was opened from the shell. No common trigger
    found: not the time since the last opening (longer gaps were fine), not resume.
  - Reported by the user the same evening: it also happens inside Steam, with neither the
    shell nor a managed app involved, and seems to start when the side menu opens. That
    fits the recordings (every hang begins at `MainMenu … WasHidden 0`) and takes the
    shell's screen handoff out of the list of suspects: *inferred*, a bug in Steam's own
    menu.
- 2026-11-22, late: a watchdog for it, `system/steam-ui-watchdog` (decided with the user:
  detect the hung process and kill it sooner than Steam does).
  - The recordings separate cleanly. 24 menu openings recorded that day, 3 of them hangs
    (12:13, 21:00, 21:31; the last one from inside Steam). In each hang the main thread of
    one webhelper page process was at 85 % or more in 26 to 30 of 30 consecutive samples
    (about a minute). In the 21 healthy openings no webhelper thread reached 85 % in any
    sample; the highest was 58 %.
  - The watchdog reads the main thread's CPU time of every `steamwebhelper` process once a
    second and kills (SIGKILL) a page process that stays at 90 % of a core or more for 15 s.
    Page processes are the helpers started by another helper that have no children of their
    own: not the main browser process, not the zygotes. Processes younger than 30 s are left
    alone, and after a kill it waits 20 s.
  - Tested in a container against a fake tree of processes named `steamwebhelper` (main,
    zygote, one spinning and one idle page process): only the spinning one was killed, an
    idle tree was left alone. Installed on gabecube at 22:13 as a user service. Not yet seen
    acting on a real hang.
  - The diagnostic watcher is gone (the machine was rebooted at 21:57); its log,
    `~/steam-hang-watch.log`, is kept on gabecube as material for a report to Valve.
- 2026-11-22: with the screen off (`drm_sleep_external_screen 1`, HDMI still "connected") the
  HDMI audio sink is gone: PipeWire has only "Dummy Output" (`auto_null`). Sound can't play
  while the shell has the screen switched off this way.

## YouTube (2026-11-22)

- SteamOS ships Qt WebEngine 6.11.1 (`qt6-webengine`, `/usr/lib/qt6/QtWebEngineProcess`);
  added to the toolchain image.
- `youtube.com/tv` with a Tizen smart-TV user agent gives the TV interface in a
  `WebEngineView`; sign-in with a code at youtube.com/activate worked, and the "Who's
  watching?" account picker comes up at start.
- The account picker's address is the same as the home screen's (`https://www.youtube.com/tv#/`;
  only `/tv` and `/tv#/` were logged on the way there). Inspected with Chromium's DevTools
  (`QTWEBENGINE_REMOTE_DEBUGGING=127.0.0.1:9222` through an SSH tunnel): the picker is a
  visible `ytlr-account-selector` element (custom elements are `ytlr-*`/`yt-*`), absent
  once an account is chosen.
- The remote's YouTube key reaches X11 as `XF86Launch1`, keycode 156 (after the hwdb remap
  to `prog1`); a root grab on it works like Home.
- SponsorBlock: Qt WebEngine 6.11 can load Chrome extensions (`QWebEngineExtensionManager`,
  since 6.10), but the SponsorBlock extension targets the desktop site; the TV-interface
  projects (TizenTube, youtube-webos) carry their own SponsorBlock code. Decided: our own
  script. The API answers `GET /api/skipSegments/<4 hex of sha256(id)>?categories=[…]&actionTypes=["skip"]`
  with a list of `{videoID, segments: [{segment: [start, end], category, …}]}` for every video
  under that prefix (checked with curl). Injection and skipping not yet seen working on
  gabecube; a web view wouldn't load in the toolchain container to test it there.
- YouTube Music has no TV website of its own that we found; on TVs it is a Music tab inside
  the YouTube app (YouTube's blog), so presumably inside `youtube.com/tv` too (not checked).
- Volume: the remote's volume keys change the sound while the shell is on screen, without
  the shell doing anything (confirmed by use). *Inferred:* Steam handles them. Mute did
  nothing (reported), so the shell now grabs it and mutes the output itself.
- SponsorBlock seen working on gabecube (11:31): a video with 3 segments, two sponsor
  segments skipped, logged by the injected script. So the user script, the application-world
  hand-over and the `#/watch?v=` address all work as assumed.
- The shell's web engine has no separate GPU process (zygotes, one renderer, one utility
  process): Qt WebEngine runs the GPU thread inside the shell. YouTube's interface looks as
  if some effects were missing (reported); cause not looked into yet (the TV user agent we
  send, or how the web view renders).
- YouTube's interface looked as if effects were missing because of the user agent. Seen with
  Chromium's DevTools on the page: `ytcfg.data_.LIVING_ROOM_APP_QUALITY` was
  `TV_APP_QUALITY_LIMITED_ANIMATION` and `environment.feature_switches.enable_animation` false
  (brand Samsung, model SmartTV, from our Tizen 7.0 user agent). Drawing was fine: WebGL
  reports ANGLE on radeonsi (hardware), 2560x1440 at device pixel ratio 1, `backdrop-filter`
  supported. Loading `/tv` under other user agents (DevTools override), all got
  `TV_APP_QUALITY_FULL_ANIMATION`: PlayStation 5 (Sony), Xbox Series X (read as "Xbox 360"),
  LG webOS 23 (adds LG styles and a pointer remote), a Cobalt-format one naming Valve /
  SteamMachine (generic TV, plus Lottie animations), and a Chromecast one, which switched the
  client to `TVHTML5_CAST` and came up signed out with a Cast prompt. Chosen: the Cobalt-format
  Valve one. Sign-in survived the change.
- What the page knows about the video playing: `document.querySelector(".html5-video-player")`
  has `getPlayerResponse()`, whose `videoDetails.musicVideoType` was `MUSIC_VIDEO_TYPE_UGC` for
  a music video from a mix (`#/watch?v=…&list=RD…`), and `navigator.mediaSession.metadata`
  has title and artist. An ordinary video (a 17-minute science video) has no
  `musicVideoType` at all, and its `mediaSession` metadata has the title and the channel.
- Playback is capped at 720p under the Valve identity: `getAvailableQualityLevels()` ends at
  `hd720` and the debug text says `optimal_format: 720p`, although the stream list offers up
  to 2160p60 (VP9 and AV1), the player knows the viewport is 2560x1440,
  `MediaSource.isTypeSupported` accepts everything up to 2160p60 and
  `mediaCapabilities.decodingInfo` says smooth and power-efficient for all of it. No dropped
  frames at 720p. *Inferred:* YouTube caps by the device it thinks this is. Not known whether
  the Tizen identity had the same cap.
- 4K on the current monitor (2026-11-22): the EDID that reaches the Steam Machine (EDID 1.3,
  "A409U", made 2015) offers 3840x2160 only at 30 Hz (297 MHz) and 2560x1440 at 60 Hz
  (241.5 MHz); the CTA block stops at 1080p60 and has an HDMI 1.x vendor block only (no HDMI
  Forum block, no 4K60 modes). The kernel lists a 3840x2160 mode; gamescope's list tops out at
  2560x1440@60. So over this path 4K60 isn't on offer: either the monitor input (or its mode
  setting) or the HDMI switch is limited to HDMI 1.4.
  Later the same day, with the HDMI switch taken out (reported): the same EDID, so the limit
  is the monitor's input, not the switch. While re-plugging, the connector was detected and
  dropped twice within a second (12:12), then stayed "disconnected" until the cable or input
  was sorted out (12:20); gamescope picked 2560x1440@60 again by itself.
  13:18, monitor moved to DisplayPort (`DP-1`), soundbar plugged into the HDMI port: over
  DisplayPort the monitor offers 3840x2160 at 60 Hz (533 MHz), and gamescope's mode list now
  starts with `3840x2160@60`, but it selected 2560x1440@60 (as before). gamescope composes a
  full 2560x1440 frame (screenshot), while the monitor shows it as a smaller rectangle in the
  middle (reported): the monitor isn't scaling the 1440p signal up on this input. Sound goes
  to the monitor over DisplayPort (one sink, "HDMI / DisplayPort Digital Stereo (HDMI 4)").
  The soundbar on `HDMI-A-1` isn't detected at all ("disconnected", no EDID). *Inferred:* its
  HDMI port is an output for a TV's ARC, not an input.
- The 720p cap, found and fixed (2026-11-22, on the 4K screen). Not the identity: the old
  Tizen one and a PlayStation 5 one were capped the same. Wrapping the page's capability calls
  showed the player's questions (about 80 while starting a video): besides real ones it asks
  `MediaSource.isTypeSupported` things no device can do, `…; width=99999`, `…; height=99999`,
  `…; eotf=catavision`, `…; cryptoblockformat=invalidformat`, `…; decode-to-texture=nope`,
  and Chromium said true to all of them (it ignores parameters it doesn't know). With a
  wrapper that answers those parameters (false beyond the screen's pixels, 60 fps, bt709) the
  quality levels went from `hd720` to `hd2160`. Deployed as `qml/mediacaps.js`: a video
  reached 3840x2160 (format 401, AV1) within 45 s, 0 dropped of 1500 frames, the shell at
  8 % CPU and the page's process at 5 %, libva loaded in the shell. *Inferred:* decoding runs on
  the GPU. Widevine isn't available (`requestMediaKeySystemAccess` rejected), which only
  matters for bought or rented films. Shell memory while playing 4K: 574 MB plus 358 MB for
  the page's process.
- Background music (decided 2026-11-22). Music keeps playing when YouTube is left, anything
  else pauses; what is music is YouTube's `musicVideoType`, not the Music section (music
  uploaded as ordinary videos carries the mark too, as far as YouTube recognises it); if the
  mark can't be read the video pauses. The remote has no play/pause key, so the top bar gets a
  now-playing item (OK toggles pause) and the YouTube button returns to it. Screen off with
  music is a black picture, because switching the signal off drops the audio sink (see
  above). Starting a game from the shell asks (pause the music, keep it, cancel). Not built:
  telling apart "other sources" outside the shell (a game's own sound, Steam). Open: whether
  YouTube really keeps playing with the page item at opacity 0, and what the hidden 4K
  decoding costs under a game.
  13:56, deployed: it does keep playing at opacity 0 (music from a mix, the shell on the
  Photos place, title in the top bar), with the shell at 10 % CPU and the page's process at
  5 %. The idle log showed "music is playing" / "music stopped" flipping several times within
  two minutes: the player reports not playing for a moment between tracks. Hence a 20 s
  grace before the music counts as stopped (otherwise a black screen would lose its signal
  between two tracks).
- Shell at other resolutions (decided 2026-11-22): scale the whole UI with Qt's scale factor
  from the X screen height (design height 1440) rather than a scale transform in QML, which
  would blur the web view and text. Not yet seen at 4K: whether gamescope's X screen follows
  the output resolution when Steam switches it is the open point (at 1440p output the X screen
  is 2560x1440).
  13:29, output switched to 3840x2160@60 in Steam's settings (over DisplayPort): the X screen
  is 3840x2160, the shell started with scale 1.50, and a gamescope screenshot is 3840x2160
  with the UI in the same proportions as at 1440p. So the X screen does follow the output,
  and the picture fills the monitor (reported).
  The restart on a resolution change wasn't exercised (the switch happened before the deploy).
- Soundbar over Bluetooth (2026-11-22, until the TV arrives): it is the default sink
  (`bluez_output.…`), next to the DisplayPort audio sink. "Turn off controllers" leaves it
  alone: it only disconnects bluez devices whose icon is `input-gaming`.
- Steam's menu opened at 11:17 and 11:43 without a hang (the shell had been on screen for 7
  and 19 minutes); `~/steam-hang-watch.log` has those as a healthy baseline.

## Suspend ends after 5 seconds: the wireless mouse receiver (2026-11-22)

Seen while testing background music: the black screen with sound, the signal going off 20 s
after the track ended and the suspend 2 minutes later all happened as intended, then the
machine resumed by itself and the shell came back on screen.

| Suspend | Resume | Slept | Setup | Requested by |
|---|---|---|---|---|
| 01:57:06 | 02:14:13 | 17 min | monitor on HDMI | shell (idle) |
| 03:52:05 | 09:50:55 | 6 h | monitor on HDMI | not the shell |
| 13:02:49 | 13:02:54 | 5 s | monitor on DisplayPort | Steam (power button) |
| 15:38:08 | 15:38:14 | 5 s | DisplayPort, soundbar on Bluetooth | shell (idle) |
| 15:47:09 | 15:47:15 | 5 s | same | shell (idle) |
| 15:50:41 | 15:50:47 | 6 s | DisplayPort cable pulled | `systemctl suspend` over SSH |
| 15:51:39 | 15:51:45 | 5 s | cable pulled, soundbar disconnected | same |
| 15:53:42 | 15:53:48 | 6 s | cable pulled, soundbar back on Bluetooth; wake sources snapshotted | same |
| 15:55:50 | 15:56:17 | 26 s | receiver unplugged 3 s earlier; ended by the power button | shell (idle) |
| 15:56:38 | 16:01:59 | 5 min 21 s | as 15:53, **mouse receiver unplugged**; ended by the power button | same |
| 16:08:28 | 16:11:43 | 3 min 14 s | receiver unplugged, mouse on Bluetooth, switched on, lying still; ended by using the mouse | same |

- Suspend is `s2idle`. Of the 5 seconds about 3 are the kernel going down and coming up, so
  the machine really sleeps for about 2.
- **Not the monitor:** it also happens with no display connected. (The first guess was the
  monitor toggling hot-plug on DisplayPort; gamescope does see `DP-1` disconnect and
  reconnect after each resume, but that is an effect.)
- **Not the soundbar's Bluetooth link:** it also happens with the soundbar disconnected, and
  the Bluetooth adapter (`1-5`) counted no wake event in the 15:53 snapshot. (`bluetoothd`'s
  "Controller resume with wake event 0x0" proves nothing: it says the same after the
  Bluetooth mouse woke the machine at 16:11.) The soundbar
  reconnects by itself after the resume.
- The 13:02 one is a different case: the power button (`PNP0C0C:00`) has an event inside
  that suspend, so a second press ended it.
- `/sys/power/pm_wakeup_irq` after the short sleeps, in order: 1 (`i8042`), 7
  (`pinctrl_amd`), 9 (ACPI), 9, 9. It doesn't single out a device.
- Wake sources (`/sys/class/wakeup/*`, readable without root) that counted events during the
  15:53 suspend, in order of their last change: `serio0` (the `i8042` keyboard port, +1),
  the USB controller `0000:07:00.3` and its ACPI device (+2, +1), then one second later the
  bridge to the Wi-Fi card `0000:00:02.2` (+2) and **USB device `1-2`, the JLab GO Mouse
  receiver (+1)**. Interrupts: IRQ 1 +8, IRQ 7 +2, IRQ 9 +5, GPIO pin 58 +1.
- **The cause: the wireless mouse receiver** (JLab GO Mouse, USB). With it unplugged and
  nothing else changed, the machine stayed asleep until the power button was pressed, and
  the USB controller counted no wake event in that suspend. *Inferred:* it signals a
  wake-up right after the bus is suspended. It sits on that USB controller, has wake-up enabled, and counted one event
  in that suspend (5 before it, after 4 short sleeps). It was plugged in during the long sleeps too, so something about
  it changed around midday: the mouse was switched on then (reported). The receiver stays
  unplugged; mouse and keyboard use Bluetooth instead. With the mouse connected over
  Bluetooth and lying still the machine stays asleep, and using the mouse wakes it: the
  Bluetooth adapter (`1-5`) and the USB controller count a wake event. To keep the mouse
  plugged in, its `power/wakeup` would have to be set to `disabled` (root, e.g. a udev rule);
  not done.
- Also wake-enabled: the Steam Controller Puck (`1-3`), the Bluetooth adapter (`1-5`) and
  CEC (`cros-ec-cec`, with `wake_tv`/`suspend_tv`/`allow_standby` set in
  `~/.config/cecd/config.d/`).
- The timer of `systemd-run --on-active=3` fired 48 s late (timer accuracy); a plain
  `systemd-run --user sh -c "sleep 3; systemctl suspend"` is on time.

## Screen stayed dark after a wake-up (2026-11-22)

- 17:00:31 the shell suspended the machine from its screen-off state (signal off through
  `gamescopectl drm_sleep_external_screen 1`). 17:42:04 it was woken; the shell went to
  Active and sent `drm_sleep_external_screen 0` within 10 ms of the kernel's "suspend exit".
  **The screen stayed dark.**
- For the next five minutes `DP-1` showed `connected` but `enabled=disabled`, `dpms=Off`:
  gamescope picked the connector and a mode again and again (the monitor dropping off and
  coming back: 17:42:04, 17:42:48, 17:42:50 to 17:43:14, 17:46:59, 17:47:02) without ever
  driving it. A power-button sleep in between (17:44:21 to 17:44:27) changed nothing; the
  shell was Active by then and sent nothing on that resume.
- 17:47:15, `gamescopectl drm_sleep_external_screen 0` sent by hand: the connector was
  enabled as soon as the monitor came back (17:47:27) and the picture returned (reported).
- *Inferred:* the first "screen on" went unheeded because it arrived while gamescope was
  still coming out of suspend. The command's exit code was not checked, so whether it failed
  outright is unknown. The other wake-ups from screen-off today (all after sleeps of
  seconds to minutes, same monitor on DisplayPort) brought the picture back.
- Change: the shell sends "screen on" on waking and after every resume, repeats it three
  times two seconds apart, and logs a failing `gamescopectl`.

### The same evening: the monitor's own deep sleep (19:39)

With the repeated "screen on" deployed (18:42), the screen again stayed dark after a wake-up,
this time without any suspend in between.

- 19:23:19 the shell switched the signal off (idle). `DP-1` stayed `connected` for 15 minutes,
  then at 19:38:43 to 19:38:46 dropped, came back for 2 s and dropped for good: `disconnected`.
- 19:39:34 Home was pressed; the shell went to Active and sent "screen on" (no failure
  logged). Nothing to drive: gamescope had no connected output.
- 19:41:28 `DP-1` came back and was `enabled` at once; the picture stayed.
- So the monitor (CRO A409U on DisplayPort) goes into a deep sleep after 15 minutes without
  a signal, drops off the link and only shows up again now and then. Until it does, nothing
  on the machine can wake it without root (forcing the connector on in sysfs; untried).
  Whether a button on the monitor was pressed at 19:41:28 is not known.
- This also changes the reading of the 17:42 case above: the long wait was probably the
  same deep sleep. What still differs is that there the monitor was `connected` for 43 s and
  24 s at a time without being driven, while here it was driven the moment it appeared;
  that part fits the lost "screen on" and its repetition having helped. Not proven.
- Within the first 15 minutes of screen-off the monitor wakes at once (earlier wake-ups
  today). Over HDMI on the old switch this was never seen.

## Jellyfin Desktop as a plain folder (2026-11-22)

Decided by the user: no Flatpak or anything like it; binaries in a folder. Recipe in
`apps/jellyfin-desktop/`.

- The project's AppImage (v2.1.0-pre2, built with Qt 6.12) doesn't run on SteamOS's Qt
  6.11.1: the binary and its MpvQt library need 6.12 (tried by the user). The source only
  asks for Qt 6.0.
- Built from source (tag `v2.1.0-pre2`, MpvQt from the bundled submodule) in an image on top
  of the toolchain image, with `mpv` 0.41.0, `extra-cmake-modules` and `patchelf` added from
  SteamOS's repositories. No source changes needed.
- gabecube has no mpv. Of everything the player and `libmpv` link to, SteamOS 3.9.5 lacks the
  packages `mpv`, `mujs`, `libcdio`, `libcdio-paranoia`, `libsixel`, `libxpresent` and
  `uchardet`; their libraries go into `lib/` (about 4 MB). `libmpv` names `libmujs.so` by its
  full path, so that entry is rewritten with `patchelf`. The binary gets an RPATH (not
  RUNPATH) of `$ORIGIN/lib`, which also covers what `libmpv` loads.
- Result: a 6 MB folder, `~/apps/jellyfin-desktop` on gabecube. `ldd` finds every library and
  `--help` works. Started with `--tv --platform offscreen` it runs until stopped: Qt 6.11.1,
  all components up except `display` (expected without a screen), the web view created, its
  web extension loaded from the binary's resources.
- Not tested yet: on the TV at all (window, TV layout with the remote, sign-in, playback,
  hardware decoding, HDR, audio), and how the shell starts it and gets the screen back.

### On the TV (2026-11-22, evening)

Started inside the session (`systemd-run --user` with the session's environment file,
`--platform xcb`) and given the screen by hand, the way the shell takes it: its window tagged
`STEAM_GAME=2113929218` with `xprop`, that id put first in `GAMESCOPECTRL_BASELAYER_APPID`.
gamescope focuses it and resizes the window to 3840x2160.

- TV layout, signed in, 1080p H.264 with E-AC3 5.1: **plays** (reported). `--scale-factor 2`
  makes the interface a sensible size at 4K; without it it is tiny.
- **Typing doesn't work in `--tv`**: the player takes every key and turns it into a remote
  action (`resources/inputmaps/keyboard.json`): letters, digits and `. : _` pass, Backspace
  is "back", `/` and `-` aren't mapped. Signing in with `--desktop` and a keyboard works, and
  the sign-in carries over to `--tv`.
- Remote: arrows, OK and Back work; Left and Right seek during playback (reported).
- Steam Controller: SDL opens the raw controller through HIDAPI as "Steam Controller" (22
  buttons), for which the player has no key map, so nothing happens. With
  `SDL_JOYSTICK_HIDAPI=0` and `SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD=1` (what the
  shell sets for itself) it sees "Microsoft X-Box 360 pad 0" (11 buttons, 6 axes), Steam's
  virtual pad; no built-in map matches that name either. A map in the profile's `inputmaps/`
  (`apps/jellyfin-desktop/inputmaps/steam-virtual-gamepad.json`) makes it work (reported,
  bumpers seek). The sticks are left unmapped: a sideways push also fires "up" or "down" and
  focus jumps to another control.
- **Home doesn't return to the shell.** The key reaches the shell, but the shell counts any
  foreign id in the base layer list as a running game and never takes the screen from one.
  Switching by rewriting the list by hand works both ways (shell first, or player first).
- The player registers on MPRIS (`org.mpris.MediaPlayer2.JellyfinDesktop.profile_<id>`):
  `PlaybackStatus` reads "Playing"/"Paused" and `Pause` pauses it. Usable by the shell for
  Home and for idle.
- Logs: at `info` and `debug` the web client's console line "Stored JSON credentials" (the
  stored sign-in) is written to the profile's log. A level passed once is remembered
  (`"logLevel"` in `jellyfin-desktop.conf`), so it has to be started with
  `--log-level error`. The log files that held it were deleted.
- Not done: the shell starting it, a tile, Home, pausing, idle. It runs as a transient user
  unit until stopped or the session ends.

### In the shell (decided 2026-11-22)

Jellyfin becomes a "managed app": its own user service, shown by the shell (design in
`shell/DEVELOPMENT.md`). Decided with the user:

- Home pauses it and keeps it running; ten minutes later it is quit (the rule YouTube has).
  Rejected: quitting at once (slower to return, loses the place in the menus).
- The screensaver starts over it when it sits paused or untouched (OLED). Rejected: treating
  it like a game and never interrupting.
- Sign-in stays a manual one-time step with a keyboard. Not built: a Settings entry for it.
- A service rather than a child process of the shell, so a deploy doesn't end a film; the
  shell picks a running one up again.

Added the same evening, asked for by the user: the remote's Netflix button opens Jellyfin, a
dot on a tile marks a loaded app, and Back on Jellyfin's start screen returns to the shell.

- Back at the top: Jellyfin's web client calls `appHost.exit()` when Back is pressed and
  `appRouter.canGoBack()` is false, but only if the host lists the feature `exit`
  (`jellyfin-web`, `src/scripts/inputManager.js`). Jellyfin Desktop lists `exitmenu` only, so
  nothing happened. Chosen: patch its `native/nativeshell.js` to list `exit` and to run a
  profile script from `exit()`, which calls the shell over the session bus; the player stays
  loaded. Rejected: letting `exit()` quit the player (no shell change needed, but a few
  seconds to reopen and unlike Home). The menu's "Exit" goes the same way now. Confirmed on
  the TV.
- Controller presses in the shell reached the hidden player (A resumed a paused film in the
  background, reported): the player reads the virtual gamepad through SDL's joystick API,
  which knows nothing of window focus. While the shell or Steam is on screen the X input
  focus is on their window (`xdotool getwindowfocus`), so the player can tell. Fixed with a
  second patch: input from anything but the keyboard and the socket is dropped while the
  player has no focus window. Freezing the hidden player instead was rejected: its unread
  controller events would be delivered when it thaws.
- Left stick, asked for by the user. The player turns each axis into a key on its own (pressed
  beyond half deflection, released under about a third), which is why a sideways push also
  fired "up". Third patch: within a stick (axes 0/1 and 3/4) an axis isn't pressed while its
  partner is, or while the partner is pushed further. The key map has the left stick again.

## Photo videos were silent (2026-11-22)

Reported by the user after the soundbar had gone back to Bluetooth: videos in Photos played
without sound. The viewer used QML's `Video`, whose built-in audio output takes the default
device once, when it is created; the shell had started while the monitor's DisplayPort sink
was the default (which that monitor doesn't play), and the Bluetooth sink became the default
later. *Inferred* from that, not seen in a trace: no video was playing when I looked. Changed
to an `AudioOutput` that follows `MediaDevices.defaultAudioOutput`. Not confirmed on the TV at
the time of writing.

## Screensaver over a playing YouTube video; activity model (2026-11-23)

Reported by the user: the screensaver came up while a YouTube video was playing.

- The log: a video started at 09:32:18, the screensaver at 09:37:18, 09:42:32 and 09:47:38,
  each exactly five minutes after the last input. So nothing held the idle time back during
  that video, while earlier videos that morning did.
- A playing video wasn't a state in the idle logic. A 30 s timer in `Main.qml` called
  `activity()` while `WebEngineView.recentlyAudible` was true for YouTube. *Inferred, not
  measured:* that flag drops in every quiet stretch of a video and each drop restarted the
  timer, so in a video with pauses in the sound it never fired.
- The user's model, adopted: **input**, **foreground** activity (something being watched)
  and **background** activity (music). `IdleController` now has `watching` for the
  foreground next to `audioPlaying`; the timer is gone. For YouTube it follows the page's
  player state (`playing`, read every second), no longer the sound.
- Input and foreground stay separate clocks. The use the user named for that: a sleep timer
  that counts from the last input even while something plays. Not built.
- Also changed: screen off (Power) now pauses a playing YouTube video, as it already did for
  Jellyfin. Music is left playing.

## One-handed controller, video seeking (decided 2026-11-22)

Asked for by the user:

- The right stick navigates like the left one, in the shell (YouTube included, it gets the
  shell's key events) and in Jellyfin (its key map): held as a remote, the right stick is on
  the side of the A button, so one hand does.
- Photo videos get a progress bar and seeking. Chosen: while a video is loaded, Left and
  Right seek, and Back first leaves the video; before, Left and Right went to the next
  photo even during a video. Not asked for explicitly in that form; easy to change back.

## Albums by year (decided 2026-11-22)

Asked for by the user: albums in groups by the year of their newest photo, sorted within a
group, the newest album at the top left; modelled on MultiMC's groups. And rows keep an
"intended column": from column 5 of a full row, Down into a row of three lands on its
third, Down again into a full row lands on column 5 again; only Left and Right change the
intended column.

- This gives up "positions never reorder" for albums on purpose: an album moves to the front
  when it gets a new photo.
- Immich's `/albums` gives `endDate` (and `startDate`, `lastModifiedAssetTimestamp`) for every
  album with photos: ISO with milliseconds, UTC. 13 albums on the TV account, 9 with their
  newest photo in 2026 and 4 in 2025.
- The screensaver's album picker in Settings lists the albums in the same new order.

## Joy-Con: who takes it when it connects (2026-11-23)

Start of the Nintendo controller work. The Switch, its dock, the dongle and the HDMI switch
are disconnected; the Joy-Cons sit on a standalone charger.

What the system has, before any controller:

- Kernel 7.2.7-valve2: `CONFIG_HID_NINTENDO=m`, `CONFIG_NINTENDO_FF=y`, `CONFIG_HIDRAW=y`,
  `CONFIG_UHID=m`, `CONFIG_HID_BPF=y`. `hid_nintendo` is loaded at boot on purpose:
  `/usr/lib/modules-load.d/hid-preload.conf` lists it and `hid_playstation` "so they have
  first claim", against a race that makes Steam fall back to evdev instead of hidraw.
- Every hidraw node is open to the `deck` user: `70-steam-jupiter-input.rules` has
  `KERNEL=="hidraw*", TAG+="uaccess"`. `60-steam-input.rules` names the Pro Controller
  (057e:2009) but no Joy-Con; it doesn't matter because of the blanket rule. A program of
  ours needs no udev rule and no root.
- `/etc/udev/rules.d` (empty) and `/etc/modprobe.d` exist, so a rule or a blacklist of our
  own has a place, with sudo.
- No `joycond`. `hidapi` 0.15.0, SDL 3.4.12 and `sdl2-compat`, bluez 5.87, `inputplumber`
  0.78.0 installed but inactive.
- Bluetooth adapter: Valve's own (USB 28de:1401, `btusb`), central and peripheral roles.
- No emulator is installed on gabecube. Under `~/Games/Emulators/Switch/`: `ringfit/ROM.xci`
  (3.3 GB, with the card's ID and certificate files), `keys/22.5.0`, `firmware/Firmware.22.5.0.zip`
  and `sdcard/Nintendo` (43 GB). The brief says Ryujinx 1.3.3 runs the dump; no Ryujinx
  is on this machine now, and where that test ran isn't recorded.

The right Joy-Con (057e:2007), paired from the command line (sync button held, then
`bluetoothctl --agent=NoInputNoOutput pair`, `trust`, `connect`; classic Bluetooth HID, no
PIN):

- **Three programs take it at the same moment**, within two seconds of the connection:
  1. The kernel's `nintendo` driver binds it: reads the calibration, sets the player 1 light,
     and creates two input devices, `Joy-Con (R)` (buttons and stick, with a `js` node) and
     `Joy-Con (R) (IMU)` (motion), plus `hidraw5`.
  2. Steam opens `hidraw5` with its own driver ("Controller using HIDAPI driver … Nintendo
     Controller subtype 2"), loads its `controller_switch_joycon_right` layout and adds one
     more virtual `Microsoft X-Box 360 pad`.
  3. Our shell opens the kernel's `Joy-Con (R)` event device as a gamepad ("gamepad added:
     Nintendo Switch Joy-Con (R)"), next to Steam's virtual pad. Not yet checked on screen:
     whether a Joy-Con button then reaches the shell twice.
- In the shell (the user, Joy-Con alone, Steam Controller off): "dead zone issues" and a
  "weird" button mapping. What the shell has open then: `Microsoft X-Box 360 pad 0` (Steam's
  virtual pad, here made for the Joy-Con) and the kernel's `Joy-Con (R)`. The kernel device
  has the stick as the *right* stick (`ABS_RX`/`ABS_RY`, flat 500) and no left one; Steam's
  SDL mapping for the same Joy-Con is `leftx:a0,lefty:a1` with its own button order, the
  layout for a Joy-Con held sideways. *Inferred, not measured:* one push of the stick
  reaches the shell as two sticks that disagree about the direction, and one button as two
  different buttons; `GamepadInput` keeps one set of stick values for all pads. The user
  also saw Home act as the Steam button (Steam's mapping has `guide:b5`) and A/B/X/Y
  "behave strangely". Changed: the shell opens only Steam's virtual pads (28de:11ff), so a
  Joy-Con works in the shell the way Steam maps it, held sideways.
- Home on the Joy-Con is Steam's button (it opens Steam's menu); the user wants it to be
  ours. Found, nothing tested: no setting that rebinds it (`controller_guide_button_focus_steam`
  in `steamui.so` is the desktop client's "Guide button focuses Steam"). `steamclient.so`
  has the strings `controller_blacklist` and `SDL_GAMECONTROLLER_IGNORE_DEVICES`, two
  possible ways to make Steam ignore a controller altogether; `config.vdf` has no
  `controller_blacklist` entry now and Steam's environment has no such variable. Steam's
  environment can be extended without root by a drop-in for the user units (see the session
  notes above). The kernel's `Joy-Con (R)` device reports Home itself (`BTN_MODE` is in its
  key bitmap), whoever else reads the controller.
- `controller_blacklist`, from Steam's community forums (no Valve documentation): a line in
  Steam's `config/config.vdf`, `"controller_blacklist" "57e/2006,57e/2007"` (hex, no
  leading zeros, commas), edited while Steam is stopped or Steam overwrites it. One report
  there says a blacklisted controller then doesn't work in games outside Steam while Steam
  runs. Untested here.
- Talking to the Joy-Con ourselves works next to the kernel driver and Steam, as `deck`,
  over `/dev/hidraw5` (plain read and write, Python): 68 input reports a second (type
  `0x30`), battery full, device info by subcommand `0x02` (firmware 4.25, "Joy-Con (R)"),
  and the player light moved to 2 and back by subcommand `0x30`, each acknowledged. Neither
  Steam nor the kernel logged anything about it. `/dev/uinput` is open to `deck` as well,
  so a program of ours can make virtual controllers.
- Direction set by the user: full control of the Joy-Cons is ours, "what a Switch would
  normally do with them", including forming two into one unit; Home on a Joy-Con brings
  up the shell; the emulator may be customised for it; perhaps shown to Steam as virtual
  devices. Their experience of Joy-Cons on Linux: a poor stack and no UX to speak of.
- What exists elsewhere today (looked up, nothing tried):
  - Steam combines two Joy-Cons itself. This Steam has the setting ("Combine Pairs of
    Nintendo Joy-Con Controllers" in Settings → Controller, "Combine Joy-Con Pairs" in the
    quick access menu), a `controller_switch_joycon_pair` layout and gyro options per
    Joy-Con. The ArchWiki says it makes `joycond` unnecessary, and that `joycond`'s udev
    rule stops Steam's own driver.
  - SDL's own Joy-Con driver (hidraw) combines a pair into one controller by default
    (`SDL_JOYSTICK_HIDAPI_COMBINE_JOY_CONS`, since SDL 3.2), has a vertical mode for a
    single one, motion and the Home light. Any SDL program that may open the hidraw node
    gets that, emulators included.
  - Ring-Con: a third-party project ported "the Ring-Con input logic used by Eden" into
    Ryujinx because the original Ryujinx has none; a yuzu-derived fork carries a
    `joycon_protocol/ringcon.cpp`. So Eden reads a real Ring-Con itself. Whether Ryubing
    does was not found.
  - `joycond` (pairs through the kernel driver's devices, shoulder buttons pressed together)
    still exists; no change this year found. Linux 7.4 has a driver for Switch 2
    controllers (USB only), from Valve; gabecube runs 7.2.7.
  - InputPlumber on gabecube ships a mapping for the Pro Controller only
    (`60-switch_pro.yaml`) and isn't running.
- **Decided by the user: "We override everything."** Their reading of the above: everyone
  does it their own way, there is no single cohesive solution, and glue between other
  people's pieces breaks with a random update. So one service of ours is the only thing
  that talks to a Joy-Con; Steam, the emulator and every other program get virtual
  controllers that the service makes. Not chosen: Steam's pair setting plus Eden's own
  Ring-Con driver; Steam's `controller_blacklist` as the way to keep Steam out (a setting
  of Steam's that Steam may change).
- The user's controllers: two pairs of Joy-Cons, a Pro Controller, the Ring-Con. Decided by
  the user for the Pro Controller (057e:2009, same protocol): for now it is handled as a
  generic controller, since its form factor isn't unusual; anything special it needs is
  done later. So it stays with Steam, and the rule and the service cover Joy-Cons only.
- How to keep everyone else out, proposed and not yet tested: a udev rule of ours takes the
  user-access tag off a Joy-Con's hidraw and input nodes and gives them to group `input`
  (which exists, and `deck` is not in it); the service runs as a system unit as `deck` with
  that one extra group. Steam and SDL programs run as `deck` without it and can't open the
  nodes. It needs root once. `/etc/systemd/system/*.service` is on SteamOS's default keep
  list; the rule needs an entry in `/etc/atomic-update.conf.d/` like the remote's. Rule order
  on this system: `70-steam-jupiter-input.rules` and `70-uaccess.rules` set the tag,
  `73-seat-late.rules` applies it, so ours sorts at 72.
- **The rule hides the Joy-Con.** Installed by the user as
  `/etc/udev/rules.d/72-tv-joycon.rules` (Joy-Cons 057e:2006 and 2007 over Bluetooth:
  `TAG-="uaccess"`, group `input`, mode 0660, for the hidraw node and the kernel driver's
  input nodes). After the next reconnect: all four nodes `root:input 660` with no ACL for
  `deck`, opening them as `deck` fails, nothing holds them, Steam's controller log has no
  line for the connection and Steam made no virtual pad, and the shell logged no gamepad.
  The kernel driver still binds and the Joy-Con stays connected. The rule is not yet on the
  update keep list.
- **And the extra group gets in.** The same Python test, run by the user as
  `systemd-run --uid=deck --gid=deck -p SupplementaryGroups=input`: 67 reports a second,
  device info, and the player light moved to 2 and back, all acknowledged. So a system
  unit with `User=deck` and `SupplementaryGroups=input` is all the service needs; being
  `deck`, it can also be restarted without root by ending its process (`Restart=always`).
- The weak link shows again with nobody but the kernel driver reading: "compensating for 7
  dropped IMU reports", `delta=135 avg_delta=14`, 70 to 100 such lines a minute around 13:00.
  The user: the Joy-Con was in another room earlier, later "a few meters" away. Measured
  after that, three windows of 40 s: 1, 2 and 1 late-packet lines, worst gap about 100 ms.
  The middle window had the switched-off Bluetooth mouse blocked, to rule out the
  background search for it; no difference. Wi-Fi is on 5 GHz and the soundbar wasn't
  connected over Bluetooth. *Inferred:* the bad numbers, the -86 dBm and probably the first
  dropped connection were distance and walls, not the machine.
- Scaffold of the service written: `controllers/`, binary `tvcontrollers`, C++ with Qt Core
  and D-Bus, the hidraw node read directly (no hidapi; my choice, offered to the user twice
  without an objection). Working: finding Joy-Cons, the protocol's reports and subcommands,
  the D-Bus interface. Outlined only: units, virtual controllers, Bluetooth. How it fits
  together is in `controllers/DEVELOPMENT.md`. Built and started in the toolchain image.
- Installed by the user (`system/install.sh`: rule, unit, keep-list entry) and running as
  `deck` with the groups `steamos-log-submitter,input,wheel,deck`. It reached the session
  bus at once, found the right Joy-Con on its next connection ("ready, firmware 4.25
  battery 4 of 4") and lists it over D-Bus as unassigned. Seen at its start: a Joy-Con
  that had gone quiet but whose node still existed gave "no reply to subcommand 2" and 3,
  then "lost: Input/output error" 14 s later; the service doesn't yet give up on a node
  that never answers. (It does now.)
- The kernel driver's writes do land on ours. Seen by the user on a reconnect: the sweep
  (the Joy-Con itself), two flashes (the service's "unassigned" pattern), then light 1
  alone: `hid_nintendo` ends its setup with "assigned player 1 led pattern", after the
  service had opened the node. Changed: the service opens a Joy-Con only once the driver's
  input devices exist (it creates them last) and one more 250 ms interval has passed.
  Restarting the service by ending its process, without root, works.
- That wait wasn't enough. The user: the lights now end either flashing or dark, with a
  hiccup in the sweep when it works. The kernel log of one reconnect: input devices at
  13:22:03.67, then `joycon_enforce_subcmd_rate: exceeded max attempts` at 04.02, 04.33 and
  04.58, so `hid_nintendo` still sends for about a second after its input devices exist,
  and the service's traffic (it opened the node at about 04.0) upsets its rate limiter.
  What it sends there, and which write leaves the lights dark, was not found out.
- **Decided: the kernel driver goes.** The user asked whether it can be masked and whether
  we need it at all. We don't: the service does everything itself. `blacklist hid_nintendo`
  plus `install hid_nintendo /bin/false` in `/etc/modprobe.d/tv-no-hid-nintendo.conf`
  (`/etc/modprobe.d` is not on the default keep list; the service's keep file adds it).
  Per device it can't be done: `hid-generic` refuses a device a loaded special driver
  claims, and an unbound device has no hidraw node. So the Pro Controller loses the driver
  too; Steam reads it through hidraw itself. Not in the initramfs; nothing else uses the
  module. Not yet installed, and the Pro Controller without the driver is untested.
- Installed by the user at 13:28:04: `rmmod` took the driver out, `hid-generic` bound the
  connected Joy-Con at once ("input,hidraw5: BLUETOOTH HID v80.01 Gamepad"), and the
  service read it there (ready at 13:28:05). `/sys/bus/hid/drivers` no longer has
  `nintendo`.
- A bug of the service's own, seen in its debug log: the lights command went out twice,
  50 µs apart ("subcommand 30 f0 try 1", "try 2"), because a reply's callback queued a
  command and both it and the reply handler sent the queue's head. Fixed.
- **The Joy-Con stopped calling the machine.** It was lost at 13:28:55 (cause not known;
  possibly switched off by the user for a reconnect test) and from then on a button press
  gives "a faint flash from the LED closest to the connect button", then dark, where
  before it gave the sweep. On the machine: no kernel or bluetoothd line, the Joy-Con still
  paired, bonded and trusted, the adapter up with page scan on (`hciconfig`: `UP RUNNING
  PSCAN`). A 45 s `btmon` trace taken by the user while pressing buttons holds no HCI
  event at all, so nothing reaches the machine: the Joy-Con doesn't page it. Why is open.
- With the Joy-Con held in sync mode (sweep), `bluetoothctl connect` from the machine
  succeeded at 13:39:45 with the pairing it had, so the keys are intact on both sides.
  The kernel line differs from a Joy-Con-initiated connection: "BLUETOOTH HID v0.01"
  against "v80.01". The service then asked for the device info three times within 750 ms
  of the node appearing, got no answer and gave up; the Joy-Con disconnected within the
  next half minute with nobody talking to it. Earlier the service only ever started a
  second or more after the node appeared (it was waiting for the kernel driver). Changed:
  the first subcommand is tried for 5 s, and a Joy-Con given up on is looked at again
  while its node exists.
- Without the kernel driver the outcomes were still "sweep ending with nothing" or four
  flashing lights, while the service's log was the same each time (device info answered on
  the second try about 0.4 s after the node, full reports on, lights `f0` acknowledged). A
  read-back was added (subcommand `0x31`, 0.4 s after setting; set again if different): it
  answered `f0` on all of four reconnects.
- **What the user was doing, and what the outcomes are** (their report after many tries):
  a short press of the sync button on a Joy-Con that is off gives a very short flash of
  the nearest light and nothing else, which is the "faint flash" above and not a fault; a
  long press gives the sweep (pairing mode) and connects only sometimes; a normal button
  connects about half the time, "sweep → nothing" or "sweep + visible hiccup + connected
  and flashing". So the dark outcome is a failed connection, not lights. In the log, three
  of four reconnects have "lost: Input/output error" and the new node 0.3 s apart: the
  machine only gave up the old connection when the new one came in. *Inferred, not
  measured:* a Joy-Con that is switched off just stops, the machine keeps the dead link
  until its timeout, and a button press in that time collides with it. Changed: 1.5 s
  without a report (they come every 15 ms) counts as gone and the service has bluez
  disconnect the Joy-Con (`org.bluez.Device1.Disconnect`).
- Home is seen by the service ("Home on Joy-Con (R)").
- That version in the user's hands, four switch-offs: each "went quiet", the disconnect
  asked for, bluetoothd's "No matching connection for device" 4 s later, and the next
  connection about 10 s after the quiet. The user: after a disconnect one has to wait a
  few seconds before a reconnect works. So the dead link is real and blocks the Joy-Con:
  about 1.5 s to notice plus about 4 s for bluez to drop a link whose other end is gone.
  Left alone, the machine kept such a link about 14 s (one measurement, 13:14:46 to
  13:15:00).
- **The user on dropping a quiet Joy-Con:** there is a reason to keep the connection up,
  interference or people taking the Joy-Cons out of range; one that went off is hard to
  tell from one that isn't reachable, "so we need some leniency". The forced disconnect is
  out again. The service now only marks a Joy-Con quiet after 1 s without a report and
  logs what could tell the two cases apart: the longest gap and the number of gaps over
  40 ms in the second before, the start of the last report, how long it was away if it
  comes back, and bluez's own connect and disconnect events with their reason. No policy
  chosen yet.
- **Switched off and out of reach look different.** One sample each, by the user:
  - Switched off with the sync button (13:58:43): "longest gap 28 ms, 0 gaps over 40 ms"
    in the second before, then nothing. bluez ended the link 20.03 s after the last report
    with `org.bluez.Reason.Timeout, Connection timeout`: the link supervision timeout is
    Bluetooth's default 20 s, and the Joy-Con says nothing when it switches off. A button
    press 7 s after that connected.
  - Carried out of reach (13:59:21): "longest gap 252 ms, 8 gaps over 40 ms" before the
    first silence, back after 1.3 s, quiet again, back after 1.2 s, then away for 14.9 s
    and back **on the same connection**, no disconnect: inside the 20 s, Bluetooth's own
    leniency works.
  - The last report carries no mark of a switch-off (byte 2, battery and connection, is
    `8e` in both).
- **Decided with the user:** an abrupt stop (no gap over 40 ms among the last 64, then 1 s
  of silence) is taken as switched off and the service has bluez disconnect the Joy-Con;
  any other silence is left to Bluetooth's 20 s. The user added that the battery level is
  known too; it is logged with the stop, so an empty battery can be told from a switch-off
  (the action is the same). Resting on one sample of each case.
- First run of the rule, user's verdict "seems fine", but the log disagrees for the
  switch-offs: two of them (14:05:27, 14:05:42) had a longest gap of 44 and 45 ms, one and
  two reports over the 40 ms limit, and were classed as a fading link and left alone. The
  link then timed out 10.4 and 10.5 s after the last report (not 20 s as at 13:59; why it
  differs is not known) and the Joy-Con, whose button was being pressed, connected 0.23 s
  after that. The walk out of reach was classed right (255 ms, 9 late) and came back four
  times, after up to 7.3 s, on the same connection. The limit for a late report is now
  100 ms; not yet tried.
- With the 100 ms limit, two switch-offs (14:08:32, 14:08:49; longest gaps 59 and 29 ms)
  were classed as abrupt. The sequence each time: quiet 1.07 s after the last report, the
  disconnect asked for, the node gone 20 ms later, bluez's "disconnected" 4.6 and 4.2 s
  after that (reason given as `org.bluez.Reason.Remote`), so the machine is free again
  about 5.7 s after the Joy-Con stopped. The user's button presses came about 10 s after
  the stop and connected within 0.6 s. A walk out of reach in the same run (883 ms, 6
  late) was left alone and came back after 2.0 s and after 16.2 s on the same connection.
  Not measured: how long a Joy-Con keeps trying when a button is pressed inside those
  5.7 s (at 14:05:36 one got in 0.23 s after the old link ended).
- Units and the virtual controller written. Chosen: the Switch's gestures (SL+SR for one
  Joy-Con sideways, L+R across a left and a right one for a pair); the virtual controller
  presents itself as an Xbox 360 pad (045e:028e, xpad's layout) so nothing needs a mapping,
  with the guide button declared but never pressed; buttons mapped by position. The right
  Joy-Con's factory stick calibration reads as centre 2023/1871, below 1352/1134, above
  1257/1038; it has no user calibration.
- First unit formed by the user (14:16:27): SL+SR gave "player 1 is Joy-Con (R) sideways"
  and the virtual controller `TV Controller 1`. Steam found it in the same second ("Local
  Device Found, type: 045e 028e, Product: Xbox 360 Controller", SDL mapping with
  `guide:b8,leftstick:b9,rightstick:b10`), opened it and made its own virtual pad; the
  shell ignored ours ("not one of Steam's") and added Steam's. So the chain Joy-Con →
  service → virtual controller → Steam → shell holds. The user: "seems OK", and it works
  in YouTube too.
- Home on a Joy-Con: the shell's `ShellService` now listens for the service's
  `HomePressed` D-Bus signal and passes it on as the remote's Home. Built; not deployed.
  (Deployed afterwards and confirmed by the user.)
- Pairing from the service, through bluez's D-Bus API: `SetDiscoveryFilter` (classic
  Bluetooth only) and `StartDiscovery`; a device named "Joy-Con…" that isn't paired gets
  `Pair`, `Trusted = true` and `Connect`. The service registers its own agent
  (`NoInputNoOutput`, not the default one) that says yes only for a device it is pairing.
  First use: the left Joy-Con (70:F0:88:04:6D:7D), held in sync mode by the user, was found
  10.7 s into the search, paired 4.4 s later and ready 0.6 s after that (firmware 4.25,
  factory calibration centre 2009/2327). All of it as `deck`, no root, no bluetoothd
  complaint. The search is started by hand for now:
  `busctl --user call local.tvcontrollers /local/tvcontrollers local.tvcontrollers StartSearch`.
- The other two Joy-Cons paired in one search (14:36): the right one 48:A5:E7:6B:35:8C
  found 1.5 s in and paired 6.2 s later, the left one 48:A5:E7:6B:1A:56 right after it in
  6.4 s. All four are now paired with gabecube.
- First pairs formed by the user with L+R: player 1 is 70:F0:88:04:6D:7D (L) with
  70:F0:88:03:DD:CE (R), player 2 is the 48:A5… two, formed three seconds after the second
  one connected. Two virtual controllers (`TV Controller 1` and `2`), and Steam made a pad
  for each (`Microsoft X-Box 360 pad 0` and `1`). The user's photo: one light on each of
  the first two, two lights on each of the others.
- The user on the pairs: A/B/X/Y "might be mapped incorrectly". They were mapped by
  position (the Joy-Con's bottom button B as the pad's bottom button, Xbox A), so B was OK
  in the shell and A was Back. Changed for a pair: by letter, Joy-Con A is the pad's A and
  so on, which swaps the positions against an Xbox pad but makes A confirm and prompts
  match the printed letters. *My reading of the report; not yet confirmed.* A sideways
  Joy-Con stays by position (bottom button OK), which the user had called OK; a Switch
  uses the right-hand button there.
- **The rule, from the user with three photos** (a right Joy-Con upright, a right one
  sideways, a left one sideways, the thumb on the right-hand button each time): "The
  mapping of buttons is all about the orientation", that button is the same one from the
  player's view and "should probably be whatever we call A". So in every grip the right
  one of the four is A, the lower B, the upper X, the left Y, which is the Switch's own
  rule. The upright pair already did that after the change above; the sideways tables are
  changed (they had the lower button as OK), which also overrides the "stays by position"
  above. The pad's buttons are now named A/B/X/Y in the code, by the letter programs see.
  The user afterwards: "This works so well. I am in a Steam game, I connected a joy con,
  and it immediately works with no issues." So a controller formed while a Steam game runs
  is picked up by the game through Steam's pad, with no setup. The game itself only
  looked at the first controller, which the user takes to be the game's limit.
- **The Pro Controller joins the service**, decided by the user after this ("at this point,
  I think we should do the pro controller too"), reversing "generic controller for now"
  above. It speaks the same protocol, so it is a `JoyCon` with side `Both`: both sticks
  and both calibrations, the full button map of a pair, and a unit of its own as soon as
  it connects. The rule now covers 057e:2009 over Bluetooth (the input rule got
  `ATTRS{id/bustype}=="0005"`, which a Joy-Con's input device shows); a Pro Controller on
  USB is left to Steam, since over USB the protocol needs a handshake the service doesn't
  do. The search pairs a device named "Pro Controller" as well. Built; the rule needs the
  user's sudo, and nothing of it is tried.
- First run with the Pro Controller (98:41:5C:39:1A:2F), after the user installed the
  wider rule: found 1.5 s into the search, paired in 5.1 s, both factory calibrations read
  (left centre 1980/1931, right 2005/2048), "ready, firmware 4.33 battery 3 of 4", and at
  once "player 1 is Pro Controller" with `TV Controller 1`. Its node is `root:input 660`
  on `hid-generic`; Steam's log has only our virtual pad ("045e 028e, Xbox 360
  Controller"), not the Pro Controller. The user: "Pro controller seems good", and the
  shell's power menu entry that turns controllers off (it disconnects Bluetooth gamepads
  through bluez) worked on these controllers too.
- A controllers screen in the shell, design agreed with the user: a Controllers entry in
  Settings; the search runs only while the screen is open (it slows the controllers in
  use); a list of controllers with player number and battery; OK offers "Change grip"
  (the service's `EndUnit`) and "Switch off". Forgetting a controller is left out of this
  version. Built and started headless; not deployed.
- Deployed, and the user on its shape: unsure what the UI should be, but a Controllers
  section holding "a choice control that has no choices, but acts as a button to open the
  real thing" is convoluted. Changed: the Controllers category shows the controllers
  directly where the other categories show their options, and the search runs while the
  focus is in that list. Also: a settings line that only opens something (the album
  picker) no longer shows the ‹ › of a value that can be stepped. Deployed; the user:
  "Yes, better". Whether "Change grip" was tried is not known.
- The user on the list: its first line was "Player 2", the second "Player 1"; the number
  could be drawn like the light pattern on the controller; and players should be
  reassignable without unbinding everything and redoing it in order. They pointed to
  Nintendo's own screens as "a lot more visual"
  (https://www.nintendo.com/sg/support/switch/controller/index.html; the page is mostly
  screenshots; its text says the order on a Switch is the order in which the controllers
  are pressed on "Change Grip/Order", and that the grip is chosen with L/R or SL/SR).
  Changed: the service lists units in player order and can make two trade numbers
  (`SwapPlayers`); the shell's line shows four lights as the controller does, and Left and
  Right trade places with the neighbour. When two units trade, their virtual controllers
  are exchanged too. *Assumed, not checked:* that Steam orders controllers by when they
  appeared, so that the first virtual controller is the game's first controller. Built;
  not deployed.
- Deployed. The user: it swaps, but the selection should follow the moving controller, the
  rate is far too high, and they had in mind "a handle you have to grab (separate
  focusable control per row)" that then moves up or down until the move is ended, with
  both confirm and cancel leaving it where it ended up, "because it is easy to mess up the
  input of a d-pad or stick and reassign players unintentionally". Causes of the first
  two: the list reset its own index to the first line on every model change and that was
  taken for a selection; and a held Left or Right repeated, each repeat a swap. Rebuilt
  that way: a line has a handle (the lights) and a body; Left goes to the handle, OK grabs
  it, Up and Down move one step per press, OK or Back lets go. Built; not deployed.
  Deployed afterwards; the user: "This is much better. Could be iterated on if this
  becomes a bigger project, but for my use, it is perfect already."
- The service also got `EndUnit(player)`: ends a controller, its Joy-Cons stay connected
  and wait again. Built, not tried.
- The user also: it "felt a bit slow when the second set of joycons was pairing", while
  the first pair was in use. *Inferred, not measured:* the search (Bluetooth inquiry)
  takes radio time from the connected Joy-Cons. Nothing changed for it; a search is short
  and only runs when asked for.
- The first connection ended after 5 s (Steam: "Controller device closed after hid_read
  failure"), with nothing in the kernel log. The Joy-Con then reconnected by itself on a
  button press and stayed for the 45 s watched. A sleeping Joy-Con can't be connected from
  the machine (`br-connection-create-socket`); it connects when a button is pressed.
- The radio link is weak where the Joy-Con was: RSSI -86 dBm when found, and the kernel
  driver logs late motion packets (`delta=89 avg_delta=14`, up to 150 ms against 14 ms).
- bluez shows `WakeAllowed: yes` for it. Not tested: whether a button press on a paired
  Joy-Con wakes the suspended machine. It connects by calling the machine (classic
  Bluetooth), unlike the remote's advertising, so it may.

## What the emulators expect from controllers (looked up 2026-11-23)

The user wants motion, the Ring-Con and the IR camera verified on our side first, with a
test screen, before any emulator is involved; stick calibration is left for much later.
Their question before designing it: what do the emulators expect? From Eden's source
(GitHub mirror `eden-emulator/mirror`, read through a fetch; files named below), nothing
run:

- **Motion:** `src/input_common/drivers/udp_client.cpp` is a client of the cemuhook (DSU)
  UDP protocol. Servers come from the setting `udp_input_servers` (`host:port`, comma
  separated); it asks each for pads 0 to 3 and subscribes to their data every 3 s. Per
  pad it takes gyro pitch, roll and yaw (multiplied by 1/312), the accelerometer, two
  touch points, sticks, 16 buttons and the battery. A controller's motion is bound with a
  string like `motion:0,pad:0,port:26760,guid:…7f000001,engine:cemuhookudp`
  (`docs/user/GyroControls.md`, a Steam Deck guide; port 26760 on localhost). Community
  notes say Eden samples motion at a fixed 200 Hz and that an irregular stream feels
  stuck. Whether it also takes SDL's sensors on Linux was not confirmed.
- **Ring-Con:** `src/yuzu/configuration/configure_ringcon.cpp` treats the ring sensor as a
  bindable analog input: any device's axis (`axis_x`, `invert_x`) or two buttons
  (`analog_from_button`), with a `deadzone` (default 0.15). The expected range isn't
  stated there. Separately it can use a real Ring-Con through its own "Direct Joycon
  driver", which needs the Joy-Con itself and so isn't for us.
  `src/input_common/helpers/joycon_protocol/ringcon.cpp` gives that driver's steps, as
  facts for our own: full 60 Hz reports; MCU on; MCU mode standby; "get external device
  info" repeated until the device is the Ring-Con (up to 42 tries); "set external format
  config" with a fixed 37-byte block; "enable external polling" with `04 01 01 02`. Where
  the flex value sits in the report is in a file not read yet.
- **IR camera:** `configure_camera.cpp` and `src/input_common/drivers/camera.cpp`: Eden
  takes IR images from a PC camera through Qt Multimedia (the device id is the setting
  `ir_sensor_device`), resampled to 320x240, 160x120, 80x60, 40x30 or 20x15 at one byte a
  pixel, handed in through `SetCameraData(width, height, pixels)`. No path for a real
  Joy-Con camera in those two files.
- **Ryujinx:** not looked up today. From memory, not checked: motion through cemuhook as
  well, no Ring-Con (a third-party port adds Eden's), no IR camera.

What follows for the service: motion can go out as a cemuhook server, a standard several
emulators read, with no patch; the ring's flex as an ordinary axis of a virtual device,
bound in Eden's ring settings, with no patch; the IR camera needs either a camera device
Eden can open or a small patch at `SetCameraData`.

Agreed with the user on that basis: motion first, then the Ring-Con, then the IR camera
as a bonus (they rarely use it; their side thought: IR lights in the room to help
positioning); a 3D view on the test screen; the Ring-Con and the leg strap are at hand.

- Motion built into the service (2026-11-23): calibration read, sensor on, three readings
  a report converted to g and degrees a second, and served by a cemuhook server on
  localhost:26760 with each Joy-Con as its own pad. First run, the Pro Controller lying
  still, read by a small test client for 6 s: 1200 packets, 200 a second, gap median
  5.2 ms, 95 % under 5.7 ms, longest 20.3 ms, no bad checksum; acceleration
  (-0.03, +1.00, +0.20) g, length 1.02; rotation (-0.2, -0.1, -0.3) degrees a second. Its
  factory calibration: accelerometer origin 26/204/197, sensitivity 16384; gyroscope
  origin -10/5/2, sensitivity 15335.
- gabecube had gone to sleep before the deploy (unreachable, then "resumed" at 17:08:03).
  The kernel log at the resume has Bluetooth connection lines and the Pro Controller was
  connected at once; whether it was what woke the machine isn't established.
- The motion test screen written in the shell: a "Motion test" action on a controller in
  Settings → Controllers, a client of the service's cemuhook server, with a 3D shape per
  Joy-Con (Qt Quick 3D) and the readings. The toolchain image got `qt6-quick3d` 6.11.1-1,
  the version on gabecube, as one layer on the existing image (`verify.sh`: 360 shared
  packages equal; gabecube now reports SteamOS 3.9.6, build 20261112.101). Which way the
  model turns depends on the axis mapping, which is a guess until seen. Built; not
  deployed.
- **The controllers wake the machine.** The user, asked what woke it at 17:08: "Both the
  joycons and the pro controller can wake it". So a button on a paired Joy-Con or Pro
  Controller brings gabecube out of sleep, which the remote can't do (its Bluetooth is
  low energy and advertises all the time; these are classic Bluetooth and call the
  machine). bluez had them as `WakeAllowed: yes` from pairing; nothing was set up for
  it.
- The motion test on the TV, the user: "Yep. Seems to be right" (the shape follows the
  controller, so the axis mapping holds as far as the eye can tell; they tried the right
  Joy-Cons, whose sensor is mounted the other way round and turned in software, and the
  Pro Controller; the left ones not yet), and
  "It's hard to judge the pull": the acceleration was three numbers. Added: the push
  (acceleration with gravity taken out, in the room's frame) as an arrow from the shape
  and as a bar from 0 to 4 g with a mark at the strongest lately. Deployed.
- The user on that: the arrow is drawn inside the Pro Controller's shape and mostly
  hidden; and "I was just trying two joycons and they suddenly disconnected". From the
  logs:
  - A bug of mine in the dialog: `ChoiceDialog` closes first and reports the choice
    after, and the pane remembered "Motion test" in the choice handler to act on it in the
    close handler. So the test opened one dialog late: nothing on choosing it, then on the
    next dialog's close, whatever was chosen there. At 17:28:47.99 the dialog on the Pro
    Controller's line was closed with "Switch off" (a stick push down, then A, in the
    shell's log): the Pro Controller was disconnected as asked, and the stale request
    opened the pair's motion test at the same moment.
  - The left Joy-Con 48:A5:E7:6B:1A:56 then dropped by itself at 17:29:07.27: bluez's
    reason `org.bluez.Reason.Unknown, Unspecified`, its last report 196 ms earlier, no
    request from the service and no key in the shell for 19 s before. Cause not known. Its
    pair ended with it, as designed, and the right Joy-Con went back to waiting, still
    connected; to the user that is both gone.
  - "cannot search: Operation already in progress" several times: a search asked for
    right after a stop, while bluez was still ending the last one.
  Changed: the test opens straight from the choice; the arrow starts outside the shape and
  the camera stands further back; a refused search start is tried again for 3 s. The user
  also saw the test always open for the same set of controllers whatever line was chosen:
  the same late request, which carried the unit of the choice before. Deployed.
- The user's design for showing movement, in place of the arrow: "Let the controller move
  in a bounding box and move the center of the bounding box to the position of the
  controller. The bounding box doesn't move relative to the screen. You just see the
  motion of the controller represented on screen." Built that way: a wire box fixed in
  the view; the shape's place inside it is the push (gravity removed, room's axes) added
  up twice and held by a critically damped spring back to the middle (4 rad/s), limited
  to 0.3 m each way. An accelerometer alone can't give a place, only movement: the spring
  keeps what a hand does within about a second and drops the drift. The arrow is gone;
  the push bar stays. Deployed. The user: "The easing is too aggressive I think. It's fine
  if they bump into the edges of the bounding box." Changed: the spring from 4 to
  1.2 rad/s, and, so that a push that doesn't go away can't park the shape at a wall,
  the push's own 1.5 s average is taken out before adding up. Deployed. The user allowed
  further tuning of this screen to be deployed without asking each time ("no need to ask
  for now").
- The user on the result: it shows the force as movement; moved quickly from one point to
  another, "the force affects its speed and its speed over time affects its position",
  but on screen the shape "kinda snaps back" when the controller stops. Cause: the spring
  was critically damped, so it also braked the speed all the time (2.4 a second); the
  speed was gone before the hand slowed down, and the slowing-down then drove the shape
  backwards. Rebuilt as they describe: push → speed → place, with the speed fading only
  slowly (3 s). What keeps it from running away is stillness: when the controller turns
  under 12°/s and its push hasn't changed by 0.06 g for 0.12 s, the speed is set to zero
  and the push still measured is learned as error and taken out from then on. The shape
  drifts back to the middle over about 6 s (the box following the controller). Deployed.
  The user: "Let's just turn off the easing altogether. The recenter button should be
  fine." Done: no drift back to the middle and no fading of the speed; the shape stays
  where a movement left it, up to the walls, and OK puts it back. The correction at
  stillness stays, since it doesn't move the shape, only stops it. Deployed.
- The user's test that found the real fault: moved quickly up by about a metre, the shape
  starts up, "but then moves back down when I stop moving it. The stopping force makes it
  travel backwards instead of stop." The box is 0.3 m each way; the shape reached the wall
  early in the move and the code set its speed to zero there, so the hand's slowing down
  was taken off zero and became travel the other way. The earlier "snap back" was at
  least partly this too, not only the spring. Changed: at a wall only the place is held,
  the speed is kept. Stillness must now last 0.25 s (was 0.12 s), so that the calm middle
  of a long smooth move is less likely to be taken for rest. Deployed.
- The user, after asking to see the code: in a 1 g world, a controller at rest that shows
  1 g on one axis and something on another doesn't add up; what does the Switch do for
  games, or does each calibrate; how does the measured force relate to orientation; and
  the stillness test is most likely upsetting slow movements. Measured at rest, all three
  lying on the table (4 s each, 200 readings a second):

  | Controller | Protocol x, y, z (g) | Length | Factory accelerometer origin (x, y, z) |
  |---|---|---|---|
  | Joy-Con (L) 48:A5…1A:56 | +0.01, +1.07, -0.07 | 1.07 | -140, -29, 770 |
  | Joy-Con (R) 48:A5…35:8C | -0.02, +1.03, -0.06 | 1.03 | 105, -42, 526 |
  | Pro Controller | -0.01, +1.00, +0.19 | 1.02 | 26, 204, 197 |

  (sensitivity 16384 on every axis; gyroscope sensitivity 15335; rotation at rest within
  0.3°/s.) So the strength at rest is up to 7 % over 1 g, most where the z origin is
  largest; with the origin left out of the divisor (reading / 4096) the same readings
  would be 1.02, 1.00 and about 1.01. The Pro Controller's 0.19 on z is tilt: it doesn't
  lie flat.
- How others convert, read from their source (Eden's `joycon_protocol` and the kernel's
  `hid-nintendo.c`): both leave the accelerometer's origin out of the reading and put it
  in the divisor, g = reading × 4 / (sensitivity − origin), as the service does; both
  subtract the gyroscope's origin. They differ on the gyroscope's scale: Eden uses
  936 / (sensitivity − origin) degrees a second per count (0.0610 at 15335; then divides
  by 360), the kernel a fixed 14.247 counts per degree a second (0.0702; its comment cites
  a 15 % STMicro correction). The service has Eden's. Eden uses one of a report's three
  readings, the kernel all three, 5 ms apart. The kernel turns only the right Joy-Con's y
  and z, as the service does.
- Changed in the test screen (shell only): the stillness test is gone, so the place is
  purely push → speed → place; recentring with the controller lying still now also takes
  the accelerometer's reading as 1 g and the gyroscope's as zero for that controller; two
  checks are shown: "Strength" (should be 1 g at rest) and "Turned" (degrees about the
  vertical since recentring; a full turn flat on the table should read 360, which would
  settle whose gyroscope scale is right). Deployed.
- The user: errors add up quickly; the Pro Controller at rest drifts on screen, "every
  time I reset it with A, it seems to drift in a different direction". *Inferred:*
  recentring took one reading for level and for 1 g; its noise sets the level a fraction
  of a degree off in a random direction, that much gravity counts as a sideways push
  until the tilt correction has caught up (about half a second), and the speed gained
  meanwhile is kept for good, since nothing brakes it any more. Changed: recentring uses
  the sensors' average over about half a second. Deployed. A constant drift from the
  remaining error is expected all the same; that is what position from this sensor is.
- The user: the drift is very small when recentring with the remote (so pressing A on the
  controller itself shook it); "It seems it just isn't meant to be precise at all. You
  couldn't use it for VR tracking". **Decided by them:** forget tracking position and
  speed; show the acceleration as displacement inside the cube; and have a motion
  calibration like the Switch's: catch the worst drift by recording what a controller
  reports at rest on a table, per controller, and use that to correct what is reported to
  games. Controllers are told apart by Bluetooth address.
- Built that way. Service: `CalibrateMotion(address)` measures two seconds at rest,
  refuses if it moved, and stores the strength at rest and the gyroscope's reading in
  `~/.config/tvcontrollers/motion.ini`; every reading is corrected by them before it goes
  anywhere. Shell: the shape's place in the box is the push times 0.15 m a g; the test
  screen has Recenter and Calibrate, each acting 0.9 s after the press. First run, on the
  Pro Controller lying still: "at rest it showed 1.01882 g and turned 0.356388 degrees a
  second"; read back through the motion server afterwards: length 1.00 g, rotation
  0.0/0.0/0.0. Deployed. Still open: whose gyroscope scale is right (the full-turn check
  has not been done).
- The full-turn check, by the user: both Joy-Cons turned 360° on the table, and "Turned"
  was off and different every time; they asked whether precision is lost, the value being
  shown as a whole number. It was rounded only for showing. The fault was in the time
  between readings:
  - The server stamped each packet with the time it was sent, in whole milliseconds, and
    sent a report's second and third reading by a timer each. Two packets in the same
    millisecond had equal stamps, which the test screen took as "no time known" and
    counted as 5 ms: time added that never passed, by an amount that depended on the
    radio's jitter.
  - Measured after stamping readings with their own time: of 3998 steps, 3021 exactly
    5 ms, but the shortest -25 ms and the longest +15 ms. The timers delivered readings
    out of order whenever several reports arrived at once.
  Changed: `JoyCon` gives every reading its own time, 5 ms after the one before across
  reports (the arrival is only used to notice lost reports, a jump over 30 ms, and to
  nudge the clock by 1 % of its gap to ours); the server sends that time and sends the
  readings from a queue, one every 5 ms in order, two at a time when behind. Measured
  then, 20 s on the Pro Controller: every step between 4.989 and 5.158 ms, none out of
  order, 19.987 s of readings in 19.996 s. "Turned" shows one decimal. Deployed; the
  full turn not yet repeated.
- The user with a jig (two Joy-Cons either side of a remote on a mat, as a fixed starting
  point): the scale seems correct, so the 936/(sensitivity − origin) figure stays; the
  result depends on how fast they are turned; and at rest they slowly accumulate rotation
  although calibrated (48:A5…1A:56 at 1.0717 g, 48:A5…35:8C at 1.0319 g, the Pro at
  1.0187 g, gyroscope errors of about 0.2°/s each, all from 18:53–18:54). After turning
  them: it could be missed readings indeed.
- Lost reports weren't accounted for: the clock above only jumped on a gap over 30 ms, and
  one lost report is 15 ms; it also jumped backwards on arrival jitter (steps of -25 to
  -39 ms and +35 to +110 ms seen with three controllers connected). The report's second
  byte is a timer of the controller's own. Measured over 2000 reports each: Pro Controller
  3: 1997, 6: 2; Joy-Con (L) 2: 13, 3: 1979, 4: 2, 5: 3, 6: 2; Joy-Con (R) 2: 14, 3: 1975,
  4: 2, 5: 3, 6: 4, 13: 1. So it counts one per reading (5 ms), three per report, and 6
  means one report lost. Changed: a report's readings follow the last ones by 5 ms plus
  15 ms for every report the timer says is missing (our own clock only for silences over
  a second, since the timer wraps at 1.28 s); the count of lost reports is logged when a
  controller leaves. Measured then, 25 s on the Pro Controller: 5000 of 5000 steps exactly
  5 ms, 25.000 s of readings in 25.001 s. The test screen counts missed readings
  ("Missed") from steps longer than 5 ms. Deployed.
- The user with the counter: "The right joycon is missing a lot of measurements. And it's
  the one that ends up more wrong." So the turn error follows the lost readings. Measured
  right after, all three lying untouched for 30 s: 6000 readings each (Pro Controller,
  Joy-Con (L) 48:A5…1A:56, Joy-Con (R) 48:A5…35:8C) and none lost. *Inferred:* the losses
  come with handling: the hand around the Joy-Con, or its turning relative to the machine,
  weakens the link. At the time: the three controllers and the remote connected, the
  soundbar not; Wi-Fi on 5180 MHz at -40 dBm; the Steam Controller's puck plugged in.
  Why the right one more than the left is not known.
- The user: "I'm not even touching it and the number goes up every once in a while." A
  second 30 s run again lost nothing, but 150 s did: Pro Controller 30001 readings, none
  lost; Joy-Con (L) 29472 readings, 528 lost (1.76 %) in 138 gaps; Joy-Con (R) 29958
  readings, 33 lost (0.11 %) in 10 gaps; never more than two reports in a row. So the
  Joy-Cons lose reports in spells even lying still, the Pro Controller doesn't, and which
  Joy-Con is worse changes. No search was running (`Discovering: no`). The machine leads
  all three links (`hcitool con`: `lm CENTRAL`), so it isn't a scatternet; link policy
  `RSWITCH HOLD SNIFF`. *Inferred:* the Joy-Cons' own weak radio in this spot; nothing
  found in software that causes it.
- The user moved the jig to the floor, "much closer to the machine". 150 s from 19:28:05:
  Pro Controller none lost, Joy-Con (L) none, Joy-Con (R) 459 lost (1.53 %) in 151 gaps.
  90 s from 19:31:08: none lost on any. So it isn't distance: it comes in spells of
  minutes, about one lost report a second, on one Joy-Con at a time (the left one at
  19:24–19:27 on the table, the right one at 19:28–19:30 on the floor), and never on the
  Pro Controller. No search ran in that time and the shell's test screen was closed
  (the only motion clients in the service's log were the measurements). That regularity
  doesn't look like a weak signal; cause not known.
- Correction, from the user: during those runs they were "alternating between them and
  turning them". So the Joy-Con losing reports was the one in the hand being turned, the
  one lying still lost none, and about one lost report a second is about one per turn.
  That is consistent with the first reading after all: the link fades at some angle of
  the Joy-Con (its antenna, the hand around it), once a turn. It also means the "at rest"
  figures of the 150 s runs above were not at rest. The Pro Controller, lying still
  throughout, says nothing either way. The user also notes the Steam Controller's puck is
  built into the machine (the USB device 28de:1305), a possible source of interference on
  2.4 GHz; not tested.
- A seven-minute watch in ten-second steps while the user handled the Joy-Cons in turn:
  mostly nothing lost, then windows with 249 and 324 readings lost of 2000 (12 % and
  16 %) on the one in the hand; the Pro Controller none.
- The user asked whether "bridging" meant filling in the missing reports by
  interpolation. It didn't: the next readings only got a later time. Now it does: when
  the report after a gap arrives, the missing readings are filled in on a straight line
  between the last reading before and the first after, and sent in order, for gaps up to
  8 reports (120 ms). The count of lost reports goes out with `Describe` and the test
  screen shows it ("Lost reports", since recentring).
- The gyroscope's creep. Noise lying still, in 1 s windows (30 s each): the spread of the
  worst gyroscope axis 0.18 to 0.31°/s on all three controllers, the acceleration's
  strength 0.002 to 0.003 g; the gyroscope's mean up to 0.03°/s on two and 0.08 to
  0.09°/s on the right Joy-Con, 40 minutes after its calibration. Built in the service:
  after a full second in which no gyroscope axis varied by more than 0.6°/s, the strength
  by no more than 0.01 g and the mean stayed under 1°/s, the correction moves a fifth of
  the way towards what the gyroscope shows. Not while held (a hand shakes far more), and
  switched off during a calibration. It swallows a turn slower than 1°/s made by
  something steadier than a hand. After the deploy, Pro Controller at rest: mean 0.00 to
  0.01°/s. Not yet seen on a Joy-Con (none was connected), and the fill-in not yet seen
  with a real gap.
- The user, with the Joy-Cons at rest after that: "a tiny amount of jitter, but it seems
  good. Fine for an exercise game, not usable for hitting targets on the moon with
  lasers." On lost reports: interpolation for now, and a pin in it: there are probably
  better ways someone has patented or written up, worth some research or some prediction
  of the missing readings later; minor, but interesting.

## The Ring-Con (2026-11-23)

Read by the controller service through a right Joy-Con, shown in the shell's motion test.
The sequence is Eden's (`joycon_protocol/ringcon.cpp`, `common_protocol.cpp`,
`joycon_types.h`), tried on the Joy-Con (R) with firmware 4.25:

- Subcommand `0x22 01` switches the Joy-Con's second processor on (the one that also
  drives the IR camera and NFC); `0x21` with a 38-byte block (`21 01 01`, zeros, and a
  CRC-8 with polynomial 7 over bytes 1 to 36 in the last byte) puts it in standby. `0x59`
  then asks what is on the rail: the answer's first two bytes were `2000` (the Ring-Con)
  on the first ask, a second after the two commands; no waiting for the processor was
  needed. `0x5c` with Eden's fixed 37-byte block and `0x5a 04 01 01 02` start the polling.
  It worked on the first try.
- Where the value is. Eden reads the flex as a signed 16-bit value at byte 39 of the
  full report, which is inside the third motion reading (bytes 37 to 48). Measured: while
  the rail is polled, that reading's accelerometer half is not the accelerometer's:
  `00 00`, the flex, `00 20` (the ring's id), then the gyroscope's three values as usual
  (they match the second reading's). The first two readings are untouched. In the motion
  test this showed as the Push value "going crazy" (the user), since one accelerometer
  reading in three was nonsense. Fixed in the service: a third reading with that pattern
  takes its accelerometer from the second. The user: "Push is good now".
- Taking the Joy-Con off the ring: the same six bytes are all zeros from then on (polling
  stays on), and `0x59` answers `fe`. Slid back on: two asks went unanswered (about
  2 s), then `2000` again; the service sends the format block and the polling command
  again (whether that is needed a second time wasn't tried). The first version noticed
  the removal by a second of zero flex; it now goes by the id missing for ten reports
  (150 ms).
- The values, 207 s recorded while the user squeezed and pulled (66.7 readings a second,
  one per report): at rest about 3700, lowest 1474, highest 6217. At rest it varies by
  about 5 counts within two seconds, but the resting value itself moved between sittings:
  3750 held in the hand, 3637 and 3704 lying still. Which way is the squeeze wasn't
  asked; the first six excursions were all upwards and the user began with squeezes, so
  probably up.
- Decided by the user: "The game has its own calibration for the ring con squeeze. It
  needs to account for different people's arm strength, and what we show seems good. I
  would just pass raw values for squeeze on." So the service scales nothing and keeps no
  zero point; what goes to an emulator later is the raw value.
- The second processor costs battery (how much wasn't measured), so the service reads a
  ring only while it is asked to: `WatchRing(address, true)` holds for five seconds, and
  the motion test repeats it every second while it is open. A Ring Fit unit will need it
  for as long as it exists.
- The leg strap needs nothing: it holds a left Joy-Con, whose motion is already served.

## Eden for Ring Fit (2026-11-23)

After the Ring-Con worked on our side, the user chose the emulator next ("Emulator
next"), ahead of the IR camera.

What is there:

- gabecube, `~/Games/Emulators/Switch/`: `ringfit/ROM.xci` (3.3 GB, with the card's
  certificate and id files), `keys/22.5.0/prod.keys` and `title.keys`,
  `firmware/Firmware.22.5.0.zip` (325 MB), `sdcard/Nintendo` (43 GB: contents, saves,
  album). 86 GB free on `/home`. Graphics: RADV on "AMD Steam Machine (NAVI33)", Vulkan
  1.4.350, Mesa 26.2.0-devel. The processor ("AMD Custom CPU 1772") has AVX-512.
- Eden: the latest release is v0.2.1 (2026-06-01), published on its own site
  (`git.eden-emu.dev/eden-emu/eden`, files on `stable.eden-emu.dev`); the GitHub
  `eden-emulator/Releases` repository answers 451, "Repository access blocked". Linux
  builds are AppImages only (about 68 MB; amd64, legacy, Steam Deck and Zen 4 variants,
  each plain or profile-optimised). The source mirror on GitHub (`eden-emulator/mirror`)
  is readable.

What Eden's source says about input (v0.2.1 and the mirror's master, read, not run):

- Its cemuhook client (`src/input_common/drivers/udp_client.cpp`) offers buttons and
  sticks for binding as well as motion: 22 buttons, four stick axes (0 to 3: left x, y,
  right x, y) scaled `(byte - 127) / 127`, so 8 bits. The protocol's analog trigger
  fields aren't used. A pad is `pad` 0 to 3 of a server, its guid made from the server's
  address only, the port a separate parameter. It is off by default
  (`enable_udp_controller`); the server list is `udp_input_servers`, default
  `127.0.0.1:26760`.
- The ring: the binding is the setting `ringcon_analogs`, any engine's stick axis
  (`configure_ringcon.cpp` accepts every input source), with `invert_x` and `deadzone`
  (0.15 by default in the dialog). The emulated ring
  (`src/core/hle/service/hid/hidbus/ringcon.cpp` and `.h`) is always player 1's, and
  turns the axis into the sensor value as `force * 2500 + 2280`: rest 2280, range 2500
  either way. It tells the game a factory calibration of its own (`os_max` 4900, `hk_max`
  -340, zero between 2160 and 2400) and a user calibration (2500, -2500, 2280) that the
  game can overwrite (`SaveCalData`). So Eden's ring isn't our ring's raw value: ours
  rests at about 3700 and reaches 1474 and 6217 (-2230 and +2520 from rest, the same
  span). `os_max` is the high end, which fits squeezing being upwards on ours.
- Command line (`docs/user/CommandLine.md`): `-f` fullscreen, `-g <file>` the game,
  `-c <file>` another configuration file. With a folder named `user` next to the program
  everything Eden keeps (settings, keys, system files, saves) stays in there
  (`src/common/fs/path_util.cpp`). Its window class is `eden`.

Decided with the user, from three questions:

- **Eden is built from source**, not unpacked from the AppImage: "I assume we will want
  to do some small adjustments on top of it anyway, so let's set up for patching". So
  `apps/eden`, built like Jellyfin Desktop: a pinned version, our patches on top, a plain
  folder for `~/apps/eden`.
- **The shell starts it and shows its window**, like Jellyfin; no Steam shortcut.
- **The ring goes to Eden in full precision through a patch**, not as an 8-bit cemuhook
  stick axis.

The build (`apps/eden`), first findings:

- The toolchain image has what Eden asks for: GCC 16.1.1 (it wants 16), CMake 4.4, Qt
  6.11.1, SDL3 3.4.12, ffmpeg, Vulkan headers 1.4.350 from SteamOS's repositories. Added
  to the build image from those repositories: git, patchelf, zip, unzip, boost, enet,
  libzip, fmt, nlohmann-json, vulkan-headers, vulkan-utility-libraries, spirv-headers,
  qt6-charts. Eden's own build fetched the rest as source while configuring (xbyak,
  unordered_dense, httplib, frozen, SimpleIni, cubeb, sirit, VulkanMemoryAllocator, the
  time zone data, gamemode, QuaZip), into `build/src/.cache/cpm`.
- gabecube lacks enet, fmt, qt6-charts and QuaZip; it has boost-libs, libzip, libusb,
  opus, lz4, zstd, SDL3, ffmpeg, Qt Multimedia and OpenSSL in the versions built against.
- v0.2.1 doesn't compile against the repositories' fmt 12.2.0: `std::strncmp` without
  `<cstring>` in `src/common/dynamic_library.cpp`, and `fmt::format` unknown in
  `src/common/settings_setting.h` (60 files). Eden pins fmt 12.1.0; with its own copy
  built in (`-Dfmt_FORCE_BUNDLED=ON`) all 1304 steps build, no patch needed. Built for
  `znver4`, without tests, multiplayer room, web service, update check and the SDL
  frontend, with Qt Multimedia (Eden's camera input). The folder is 43 MB: the binary and
  the libraries of enet and qt6-charts. In the plain toolchain image all 207 libraries it
  names are found. Not yet run, not yet on gabecube.

### First run on gabecube (2026-11-23, evening)

The user agreed to a first run without any patch, and chose the driver design: "our own
driver sounds right" (an input driver of ours inside Eden that talks to the controller
service, in place of SDL, cemuhook and separate routes for ring, rumble and camera).

- Deployed to `~/apps/eden` (v0.2.1, all libraries found). Keys copied to `user/keys/`,
  the firmware's 238 files unpacked into `user/nand/system/Contents/registered/`.
- **The `user` folder is looked for in the current directory on Linux, not next to the
  program** (`path_util.cpp`; the handbook's "portable" tip is about Windows). Started
  from elsewhere, Eden made `~/.local/share/eden`, `~/.config/eden` and `~/.cache/eden`,
  found no keys and asked "Decryption keys are missing. Install them now?". Those three
  folders were removed again; started with `~/apps/eden` as working directory it uses
  `user/` for everything.
- Started by hand as a transient user unit (`systemd-run --user`, gamescope's environment
  file, `QT_QPA_PLATFORM=xcb`, `eden -f -g …/ringfit/ROM.xci`), its windows tagged
  `STEAM_GAME=0x7D000001` and that id put first in `GAMESCOPECTRL_BASELAYER_APPID`. The
  id is deliberately not one of the shell's: the shell takes it for a Steam game and
  leaves the screen alone. Side effect, seen by the user: the shell's power dialog came
  up "but the focus was on the screen behind it", because the shell doesn't know this
  window as its app. It goes away once Eden is a managed app of the shell.
- **Ring Fit Adventure 1.0.0 boots**: the window title names it, with RADV; the Suit-Up
  Guide video and the title logo were on screen (3840x2160 screenshots).
- Eden creates nine top-level windows of class `eden` (two at 3840x2160 besides the main
  one, bars, 100x30 stubs); the shell's "largest window of that class" needs a look.
- Stopping the unit with SIGTERM while a game runs hung until my command's 90 s limit;
  SIGKILL ends it at once. The service unit needs its own stop.
- Eden's own Joy-Con driver is on by default and tries every five seconds to open the
  Joy-Cons: "Yuzu can't gain access to this device: ID 057E:2006" and `2007`. The udev
  rule keeps it out. To be switched off in the settings (`enable_joycon_driver`).
- Eden's cemuhook client is always running and connects to 127.0.0.1:26760 by default,
  so it found our server unasked ("a program at 127.0.0.1:… reads motion"). It dropped
  our first packet as "stale": it expects the packet counter to differ from its initial
  0, and ours starts at 0. Harmless (one packet); the counter should start at 1.
- By default player 1 is a Pro Controller on the keyboard. SDL (asked through a small
  script, HIDAPI off) sees our virtual controllers as two "Xbox 360 Controller" gamepads
  whose GUIDs differ in the name's checksum: `030065335e0400008e02000010010000` is "TV
  Controller 1", `030025325e0400008e02000010010000` "TV Controller 2". So an SDL binding
  in Eden follows the player number of the moment, one more reason for the own driver.
  Steam's virtual pads were not in that list.
- A temporary binding for the first try (a script, not kept in the repository; the
  settings before it are in `/tmp/qt-config.first.ini` on gabecube): player 1 as two
  Joy-Cons, buttons, sticks and d-pad from "TV Controller 2" through SDL by raw index (A 0,
  B 1, X 2, Y 3, L 4, R 5, minus 6, plus 7, stick buttons 9 and 10, ZL and ZR as axes 2
  and 5, hat 0, sticks 0/1 and 3/4), motion from cemuhook pads 2 and 3, and the ring from
  the right stick's x axis. The key for the ring in `qt-config.ini` is `ring_controller`
  (the setting inside Eden is called `ringcon_analogs`); a key only counts when its
  `…\default` line says `false`.
- Around the first start the user saw "the joycons were going crazy for a bit". The
  service's log for that time: the machine had just woken, the pair was formed at
  20:50:17, a search ran from 20:50:33 to 20:50:35, and at 20:50:41 the right Joy-Con
  dropped (bluez: reason Unknown; 89 reports lost on the radio before, last report 214 ms
  earlier), came back at 20:50:57 and the pair was formed again at 20:51:11. What exactly
  they saw (lights, rumble) wasn't asked yet; Eden can't have sent the Joy-Cons anything.

### The motion server's directions were wrong for Eden (2026-11-23, late)

- With the temporary binding the game got to its Attachment Guide ("Insert the Joy-Con
  (L) into the leg strap pocket", "Wrap the leg strap around your left thigh … and stand
  still") and stayed there. The user: "It needs the motion data to continue from this
  screen. And the joycons are on player 2" (the Pro Controller was player 1; the binding
  was made for player 2's virtual controller and motion pads 2 and 3).
- Eden's log shows the game asking for the ring on player 1's right rail (polling mode 4
  on the right Joy-Con, `EnableJoyPollingReceiveMode`) and also opening the left rail.
- The cause, from Eden's source, not from trying: what its cemuhook client makes of a
  packet (`udp_client.cpp`, `OnPadData`: acceleration `(x, -z, y)`, rotation `(pitch,
  roll, -yaw)` times 1/312) against what its own Joy-Con driver gives
  (`joycon_protocol/poller.cpp`). That driver swaps the sensor's first two axes and then
  flips signs per side; written in the service's axes (x to the controller's top, y to
  its left, z out of its face, the right Joy-Con already turned) it comes to the same for
  left, right and Pro: acceleration `(y, -x, -z)`, rotation `(-y, x, z)`. The server's
  mapping, a guess until now, gave Eden the acceleration with all three signs reversed
  and roll and yaw reversed; only pitch was right. To the game the Joy-Con was upside
  down.
- Changed: the server now sends acceleration `(y, -z, x)` and pitch, yaw, roll as
  `(-y, -z, x)` of ours, so a controller lying face up shows -1 g on the protocol's y;
  the packet counter starts at 1. The shell's motion client turns the packets back into
  its own axes, so the motion test is unchanged. Built, not deployed: the deploy needs
  the user's go-ahead. Not yet confirmed in the game.
- Eden's 1/312 on the rotation means it takes the protocol's degrees a second as 15 %
  more than a turn per 360; our own driver will hand it the unit it wants.

- Deployed with the user's go-ahead (21:00:42). The Pro Controller lying still then read
  (0.00, -0.98, -0.19) g on the protocol's axes, as intended. The game stayed at the leg
  strap screen all the same ("It is stuck").
- Why, this time: the two Joy-Cons' motion was nonsense, (0, -9.90, 0) g and
  (0, -61.97, 0) g. At the restart both were already connected and answered slowly, and
  the service's log has for both "factory motion calibration: accelerometer origin -1 -1
  -1 sensitivity -1 -1 255 gyroscope origin 0 0 0 sensitivity 0 0 0", next to "answered a
  subcommand that isn't ours" lines. A memory read (subcommand `0x10`) was answered after
  its 250 ms were over and the next read had gone out, and the late answer, for the
  user's stick calibration (all `ff`), was taken as the motion calibration's: answers
  were matched by subcommand only. At the connects before that (20:50) the calibration
  had been read right, so the first failed attempt was the directions and this one the
  calibration. Fixed: a memory read's answer must repeat the address and length that
  were asked for. Built, not deployed yet.

- The user agreed to deploys as needed while Ring Fit is being got going ("Yes, let's
  keep going"). Deployed 21:04:42: both Joy-Cons' calibrations read right, the late
  answers now logged as not ours and ignored. With the pair formed, the left Joy-Con
  worn in the strap read (-0.18, -0.17, +0.88) g on the protocol's axes (its top up),
  the right one lying face up (0.03, -1.00, 0.02).
- The game still waited. Third cause, in Eden's log and source: **Ring Fit takes player
  1's pair apart.** It calls `SetNpadJoyAssignmentModeSingle` for npad 0 with the right
  Joy-Con as the one to keep, and Eden (`hid_core/resources/npad/npad.cpp`,
  `NPad::SetNpadMode`) then makes player 1 a right Joy-Con alone and gives the left one
  to the first player that isn't connected, as a dual controller with only its left half.
  The game then opens the left rail of that second npad (`GetBusHandle npad_id=1
  bus_type=0`). Eden's player 2 had no bindings, so the leg's Joy-Con didn't exist for
  the game. With player 2 given the same bindings as player 1 (and left unconnected), the
  game went past the leg strap to "Press in on the Ring-Con." So the motion directions
  hold for the game's leg check, and our own driver has to serve the left Joy-Con to
  whichever player Eden moves it to.

### Decided: replace Eden's controller input (2026-11-23, night)

- The stand-in for the ring (the right stick bound as Eden's ring axis) did nothing at
  "Press in on the Ring-Con." Why wasn't looked into: the user's call was "I say we clean
  up whatever they are doing with input and replace it". Eden was stopped and the screen
  given back to the shell.
- What Eden's controller input is, from its source: `hid_core`'s `EmulatedController`
  (one per player) is built entirely from binding strings in the settings
  (`ReloadFromSettings`, `LoadDevices`): one string per button, stick and motion sensor,
  each naming an input driver ("engine") of `input_common` and a device by GUID, port and
  pad; battery, colour, camera, ring, rumble and lights are derived from the strings of A
  and d-pad right. Type and "connected" also come from the settings. Drivers are
  registered in `input_common/main.cpp` (keyboard, mouse, touch, GameCube adapter,
  cemuhook, TAS, camera, virtual amiibo, virtual gamepad, SDL, Joy-Con). A driver is an
  `InputEngine`: it reports buttons, axes, motion, battery, colour and camera frames per
  device, and takes rumble, lights, the polling mode (ring, IR, NFC) and the camera
  format. Eden's own Joy-Con driver delivers a real ring as axis 100 of its device.
- The design put to the user (the contract is `controllers/protocol/emulator.h`, written
  as the scaffold):
  - The service gets a second interface next to the virtual pads: a Unix socket on which
    an emulator gets every physical controller by itself (buttons, stick, the three
    motion readings of each report with their times, the ring's raw flex, battery, how
    it is held and whose it is), and asks for the ring, lights and rumble. What it asked
    for ends with its connection.
  - In Eden, a driver of ours is the only source of controllers. The players' bindings,
    types and "connected" in the settings are no longer read: each player's controller is
    built from what the service says, live, and when a game takes a pair apart the left
    Joy-Con goes with the player Eden moves it to. Eden's other controller drivers (SDL,
    its Joy-Con driver, cemuhook) are not started; their source stays, so that the patch
    stays small against new Eden versions.
  - The ring is passed raw. Eden's pretend ring gets our ring's numbers in place of its
    own (rest 3700 and a range that covers 1474 to 6217, in place of 2280 and 2500), so
    the game calibrates against the real thing.
  - Only the controllers the service owns can play in Eden (Joy-Cons, the Pro
    Controller), as decision 5 of the brief has it.

### Built: the service's emulator socket and Eden fed from it (2026-11-23, night)

- The user, on the design: "There is no point adapting to something that expects buggy
  linux drivers in the way". Taken as: don't plug a driver of ours into Eden's input
  layer either (binding strings, GUIDs, its dead zones and stick filtering); write into
  the part of Eden that is the Switch's controller for the game. They also said their
  messages often wait in a queue and arrive outdated, so that line may predate the
  design summary; it fits it either way.
- **Service** (`controllers/protocol/emulator.h`, `EmulatorServer`): a `SOCK_SEQPACKET`
  Unix socket at `/run/user/1000/tvcontrollers/emulator`. A small test client got both
  Joy-Cons at 67 reports a second, 124 bytes each, the three readings of a report 5000 µs
  apart, and with the ring asked for its raw value (3685 to 3696 at rest), which the
  service stopped reading when the client left.
- **Eden** (`apps/eden/driver/tv_controllers.*`, placed in `src/hid_core/frontend/`, and
  `patches/tv-controllers.patch`: 43 lines in 7 files). `TvControllers` belongs to
  `HIDCore`, connects to the socket from a thread of its own and is a friend of
  `EmulatedController`: it sets style and connection of players 1 to 8 from the service's
  units and writes buttons, sticks, motion, battery and the ring's force straight into
  the state the HID services read. `ReloadFromSettings` hands over to it (the bindings
  are emptied, so every input device is a dummy), `SetPollingMode` and `SetLedPattern`
  go to it, and `NPad::SetNpadMode` and `MergeSingleJoyAsDualJoy` tell it when a pair is
  taken apart or put together, so the halves follow. Motion goes in as Eden's own
  Joy-Con driver would give it, acceleration `(y, -x, -z)` in g and rotation `(-y, x, z)`
  in turns a second, but all three readings of a report with 5000 µs each, where that
  driver passes one reading per report as 15000 µs. The pretend ring's constants
  (`hid_core/hidbus/ringcon.h`: "obtained from a real ring controller", rest 2280, range
  2500) are replaced by ours (3700, 2600), and the force is the raw value's share of
  that range, so the game gets the raw value back.
- First start with it: Eden's log has "Player 1 holds controller style 5: connected"
  (two Joy-Cons), then `SetNpadJoyAssignmentModeSingle` and "The game took player 1's
  Joy-Cons apart: the left one is now player 2"; the service's has "Eden wants the ring
  on Joy-Con (R)" and "has a Ring-Con attached". The game went past both attachment
  checks by itself to "Press In and Start", drawing the ring and the leg strap.
- The user pressed in and got into the game's setup: "It was asking about being in
  Canada or outside Canada. We are not in America. I said outside Canada and now the
  units are all imperial." Eden's default region is USA (`region_index=1`). Set to
  Europe (`2`) and the language to British English (`language_index=12`); the game then
  asks "UK" or "Outside the UK", the latter being kilometres and kilograms.
- **The service crashed** (SIGSEGV) when I killed Eden for that: dropping a client gave
  up its ring, that announced the change to all clients, the send to the dying one
  failed and dropped it a second time from inside the first. (My first fix, not deleting
  the socket notifier inside its own signal, was right but not the cause: it crashed
  again.) Now the client is taken out of the list before anything else. Checked by
  killing a ring-reading client three times: the service stayed up.
- A consequence seen on the way: the crash restarted the service, the user formed the
  pair again after the game had started, and the game stayed at the leg strap screen. It
  had asked for single Joy-Cons while nobody was connected, and Eden doesn't apply that
  to a pair that connects later. Open: a pair formed after the game started, and units
  that don't survive a service restart.
- Eden's log file is written late (lines only up to 3.8 s after a minute), so it can't
  be read for what just happened; its early lines can be missing as well.
- Not built yet: lights and rumble from the game (the service accepts the messages and
  ignores them), a Joy-Con held sideways in a game (its buttons are passed upright), the
  shell's part (tile, Home, stopping: Eden doesn't end on SIGTERM while a game runs),
  the IR camera.

- The user, on the game after that: "Looks locked up." Eden was alive, on the game's
  "Warm-up" question with the right half of the page blank; two screenshots three seconds
  apart were identical; the thread of the first emulated core (`CPUCore_0`) and the
  display threads (`vi`, `VSyncThread`) used no more processor time, cores 1 and 2 a
  little, and our `TvControllers` thread kept running. Every thread waits on a futex.
  SteamOS has gdb and eu-stack, but `ptrace_scope` is 1, so a running process can't be
  looked into; and a core dump would go to Valve's `steamos_log_submitter` (the
  `core_pattern`), so that wasn't provoked. Eden was restarted as a child of gdb (SIGSEGV
  passed through, the JIT lives on it), with a watcher that interrupts it when core 0
  stands still and has gdb print every thread's stack. Cause not known yet.
- The capture (51 s after the restart, 70 threads): no deadlock. All four cores sit in
  `PhysicalCore::Idle`, the HID and display services wait normally, `TvControllers` is in
  its `poll`, and one thread is `QtSoftwareKeyboardDialog::StartInputThread`: **Eden's
  on-screen keyboard is open.** The game asked for typed input, Eden opened a dialog
  window of its own for it and the game waits for the answer. The window wasn't on the
  screen because the by-hand start had only tagged the windows Eden had when it started.
  The "Warm-up" stop was most likely the same (the next question wants a number). The
  watcher's gdb ended that Eden with the capture. Restarted with a helper that tags every
  window of class `eden` once a second while Eden runs. For the shell this means: every
  window of a managed app has to be tagged, not only its largest.
- A follow-up from the user for later: "when we have user profiles, they should pass
  through to eden" (its user, and so the saves, follow who is using the TV). Noted in
  `apps/eden/DEVELOPMENT.md`.

### Ring Fit played (2026-11-23, late night)

- With every Eden window tagged, the keyboard dialog showed and the user got through the
  setup: "It is working. Played through the first level and finished a very short
  session." So buttons, the leg's and the ring Joy-Con's motion, the ring's raw value
  and the game's own calibration of it all hold up in play.
- "It was very confusing to type with the stick directions rotated in a weird way." Eden's
  keyboard dialog (`yuzu/applets/qt_software_keyboard.cpp`) moves by the stick's direction
  bits as the Joy-Con has them, and Eden turns nothing for a single Joy-Con. In the ring
  the Joy-Con sits on its rail, a quarter turned: its top to the player's left (worked
  out from where the rail is; not checked against the user's hands). Changed: while
  player 1's right Joy-Con has a ring attached, the dialog takes the stick as the player
  sees it (the Joy-Con's right is up). Only the dialog: the game gets the stick as it is,
  as on a Switch. Built and deployed; not tried yet.
- "There is a few graphical glitches that are hard to describe. Just some random geometry
  that shows up maybe once or twice in the first level." Not looked into. Eden's graphics
  settings are its defaults (`gpu_accuracy=1`, `resolution_setup=3`, Vulkan).
- `apps/eden/try.sh start|stop` now does the by-hand start that was typed out all
  evening (transient unit from Eden's folder, the window tagger, the base layer entry),
  until the shell starts Eden itself.

### Home and a parked game (2026-11-23/24, night)

- The user: "Home button needs hooking up for sure. And no idea how the emulator handles
  persisting game state across suspends and so on. On the switch, the game can keep
  running for a very long time. It's basically suspended in the background until you
  bring it back."
- Facts: Eden has no save states, so a game is only kept by keeping the process and
  pausing it; that survives Home, other apps and the machine's sleep, not a reboot, an
  update or a crash. Its own pause (`MainWindow::OnPauseGame`, `emu_thread->SetRunning`)
  stops the emulated clock; freezing the process from outside (SIGSTOP, the cgroup
  freezer) would let the host's monotonic clock run on and the game see the gap. Ring Fit
  running: 8.0 GB resident (5.6 GB of it shared memory, the emulated machine's RAM) of
  the machine's 15 GB, and 4.3 of 8.6 GB video memory in use. `pauseWhenInBackground`
  exists but goes by Qt's window focus. The shell ignores controller input while it isn't
  on screen.
- Decided by the user: **a parked game stays until it is closed, and the TV asks when
  another game starts** (not closed by itself); **after sleep or a dropout the Joy-Cons
  need L+R again**, as now (not rejoining by themselves). So Eden has to cope with a pair
  that is formed while a game runs; not built yet.
- Built, not deployed (the deploy needs the user's go-ahead: it stops the running Eden
  and restarts the service and the shell):
  - **Eden** speaks MPRIS as far as the shell uses it (`apps/eden/driver/tv_player.*`:
    `org.mpris.MediaPlayer2.eden`, `PlaybackStatus` with its change signal, `Pause`,
    `Play`), hooked to the main window's own pause and continue, and tells the controller
    service when a game is being played (`Playing` in the protocol).
  - **The service** holds its virtual pads neutral while an emulator's game is being
    played, so the Joy-Cons are the game's alone; Home still reaches the shell.
  - **The shell**: Eden is a managed app (`eden.service`, class `eden`, app id
    0x7E000003) with every window of its class tagged; a "Ring Fit Adventure" tile on
    Home; leaving with Home pauses it and it stays (no quit timer); opening the tile
    continues it; starting a Steam game from the shell with it parked asks "Close it and
    play", "Keep it open and play" or "Cancel"; it is paused before the machine sleeps.
- Not there: closing a parked game from its tile (only the question above closes it); a
  Steam game started from Steam's own screens isn't asked about; an Eden started outside
  the shell isn't shown or paused by it; the tile belongs in the Games place, which only
  holds Steam's library so far.

- Deployed with the user's go-ahead (22:11). Checked from outside first: started as
  `eden.service`, Eden's `PlaybackStatus` went to "Playing", `Pause` made it "Paused",
  the service logged "Eden is playing" and "Eden is not playing", the shell "is running",
  "window … tagged with app ID 2113929219", "is playing". Stopping the unit left it
  "failed" (killed after the stop timeout), so the unit now ends by SIGKILL and counts
  that as clean.
- The user then tried it, as the logs show: the tile started Eden and the shell put its
  id first in the base layer; "Home on a Joy-Con" → "pausing eden.service", the shell's id
  first again, "is not playing"; the tile → "continuing", playing; Home → parked again.
  Twice round, no errors.
- Seen in the service's log on the way: the ring stayed wanted while the game was parked.
  Changed in `TvControllers::SetPlaying`: a paused game gives its ring up and asks for it
  again when it carries on.
- **A pair formed while the game runs** (needed because Joy-Cons come back needing L+R).
  `NPad::SetNpadMode` now tells `TvControllers` what the game last asked for a player's
  Joy-Cons, also when nobody is connected at that moment; when a whole pair is applied to
  a player whose Joy-Cons the game wants apart, `TvControllers` has the HID service do
  the split again (a callback the service sets, which calls `SetNpadMode` as the game's
  own request does), and the halves follow as before. A new start forgets it. Built and
  copied to gabecube; the parked game still runs the build before it. Not tried.
- Root-level windows of class `eden` with a game parked: 3. (xdotool had counted 10,
  including windows inside the main one.)

- The user's go-ahead to close the parked game ("Ok. Go ahead."): `eden.service` stopped
  and ended "inactive"; the shell logged the window gone, the service "Eden disconnected"
  and the ring given up.
- Two follow-ups from the user with it:
  - "That tile should really get art from the game somehow. Especially when there are
    more games. I wanted ring fit to work first because that's the one I play a lot. But
    there is the whole Zelda collection." So: Switch games as a library with their own
    art, in the Games place, not one tile on Home. Not started. Their remark on that
    plan: "Probably a Zelda library to be honest". To ask when designing it: whether
    that collection is only Switch games, and whether Zelda wants a place of its own.
  - "Ringcon in the home interface: probably should be treated just like a rotated
    joycon?" Built in the service, not deployed: a right Joy-Con knows whether it sits in
    a Ring-Con (`JoyCon::inRing`: what its rail said last, asked while the ring is read
    and once by a probe when a pair is formed, which switches the second processor on
    for that moment only). A pair whose right Joy-Con is in the ring gives the virtual
    pad that Joy-Con as held a quarter turned, its top to the player's left: the stick as
    the player sees it on the pad's left stick, and the four buttons by the same rule as
    every grip (printed B is on the right and is A, Y below is B, A above is X, X on the
    left is Y). The leg's Joy-Con keeps its buttons. The game is not affected: an
    emulator gets the Joy-Cons as they are.

- Deployed with the user's go-ahead (22:23). Both Joy-Cons were ready and waiting for
  L+R; no pair had been formed a minute later, so the probe for the ring wasn't seen yet.
- An idea from the user, "a silly followup", for later: the Ring-Con itself as the way
  to steer, everywhere: "Squeeze to confirm, Pull to cancel, Rotate left/right for going
  left/right, Point up/down for up/down. And then it can apply everywhere..." It would be
  one more way a pair with a ring feeds the virtual pad. Its cost: the ring would have to
  be read all the time the pair is in use outside a game, which keeps the Joy-Con's
  second processor on.

- The user formed the pair: the service logged "Joy-Con (R) … sits in a Ring-Con" right
  after "player 1 is … as a pair", so the probe works. Their verdict on the directions:
  "It is reversed". So in the ring the Joy-Con's top points to the player's **right**,
  not left as I had worked out from the rail: the same quarter turn as a Joy-Con held
  sideways. Changed to that in both places: the pair's virtual pad (stick as for the
  sideways right Joy-Con; printed X is A, A is B, Y is X, B is Y) and Eden's keyboard
  dialog. Built and deployed (22:26; the service restarted, so L+R again). Not confirmed
  yet.

- The user on the turned grip: "All good." Seen in the service's log from their session
  with the newer Eden: on Home, "Eden is not playing" and "Eden no longer wants the ring";
  on opening the tile, "Eden is playing" and "Eden wants the ring" again. So a parked
  game gives its ring up.
- The user: "The same fix we did with the home button press affecting YouTube needs to be
  done with the emulator. Maybe in a more general way?" That fix (commit 2edabf1) kept
  the repeats and the release of the press that opened YouTube from its page. Here it is
  the press on the game's tile: still down when the game carries on, so the game sees it.
  Made a rule in place of a special case: **a press belongs to whoever had the
  controller when it went down.** When a controller changes hands, what is held at that
  moment stays released for the new owner until it is let go; a stick that is pushed
  stays centred until it has been centred.
  - TV → game: in `TvControllers` (Eden), at the moment a game is about to carry on
    (`SetPlaying(true)`, now called before the main window resumes it).
  - Game → TV, and at forming: in the service's `Unit`, when its virtual pad gets the
    controller back from a game and when it is created. So the L and R that form a pair
    no longer reach the pad as shoulder buttons either.
  Built. Eden copied to gabecube; the service not deployed, because the user was in the
  middle of a game ("Playing") and a restart breaks the pair up.

- The user corrected their wording ("Or rather confirm/open button press on home"),
  which is what was built, and said "Yes, deploy": the service was deployed at 22:34
  with the game parked. They formed the pair again ("player 1 is … as a pair", "sits in a
  Ring-Con"; Eden had reconnected). Whether Eden took the pair apart again can't be read
  yet: its log file is written late.
- A follow-up from the user: "'Right click'/long press on tiles should bring up a menu
  that has 'close' and 'force close' in it." Built, the shell not deployed yet:
  - On the Home place OK now opens a tile when it is let go; held for 0.6 s it brings up
    the tile's menu: Open, and for a loaded app Close and Force close. `ChoiceDialog`
    ignores the repeats of a key held from before it opened.
  - Close ends the program its own way: YouTube's page is unloaded; Jellyfin's and
    Eden's units are stopped (SIGTERM). Force close kills the unit's processes
    (`ManagedApp::kill`, systemd's `KillUnit` with SIGKILL).
  - For Close to work on Eden: Eden reacts to SIGTERM by closing, which asks "Are you
    sure you want to close Eden?" in a dialog first (`ConfirmClose`); that hidden question
    was why stopping it hung earlier in the evening. Our build sets
    `confirm_before_stopping` to "never" at start, and `eden.service` goes back to the
    ordinary stop, with a kill after 15 s. Not tried yet.

- The user: "we should look into the various context menus from the browsers and apps
  and probably turn them off". Looked: the shell's YouTube view (Qt WebEngine) had
  nothing against the browser's own context menu, which a right click, a long touch or
  the Menu key (a controller's Menu button is that key) brings up; now refused
  (`onContextMenuRequested`). Jellyfin Desktop's page is a Qt WebEngine view too and
  doesn't refuse it either (the only "context menu" in its `webview.qml` is its tray
  icon's); whether one shows there in the TV layout wasn't tried, and turning it off is a
  one-line patch plus a rebuild. Eden shows none over a game.

- The user: "No menu in jellyfin." So Jellyfin Desktop stays as it is.
- A bug from the user: "Sliding the joycon out of the Ringcon keeps it in the rotated
  input mode". As built, the rail was only asked when the pair was formed. Looked for a
  sign that costs nothing: the right Joy-Con's reports through the evening have no
  button bit that changed when it went into or out of the ring (the "charging grip" bit,
  0x8000, never showed), so the ordinary report doesn't tell. Changed: while a pair's
  right Joy-Con is known to sit in a ring and is in use, the unit keeps the ring read
  (a fourth user of it, the grip), so the missing id is noticed within 150 ms and the pad
  goes back to the upright grip. After two minutes without any input from that Joy-Con
  the reading stops, and its next input asks the rail again. The other direction, a
  Joy-Con slid into the ring after the pair was formed, is still only noticed at the next
  L+R or when a game reads the ring.
- With that the ring's value would have gone over the session bus 66 times a second
  whenever the ring is read; `RingFlex` is now only sent while someone asked to watch
  (the shell's test).
- Built; not deployed: the classifier asks for the user's go-ahead for each restart of
  the service, and the shell's restart for the tile menu is still waiting for an answer
  too.

- With the user's "Yes": the service (22:45:25) and the shell (22:45:28) deployed. The
  shell came up on screen, found the parked Eden ("eden.service is running", its window
  tagged again) and left it parked. Not yet tried by the user: the tile menu, the ring
  being slid out, the change-of-hands rule, and a pair formed again during a game.

### Stick presses multiplied; a Joy-Con slid into the ring (2026-11-23, late night)

- The user, back from trying: the tile menu works and Close works (Force close not
  tried); YouTube's context menus are gone. And: "It seems after opening the game, the
  joycon has a very high repeat rate outside of it, or somehow it produces multiple
  events?"
- Not the buttons: the service's log and the shell's "button down" lines agree, one per
  press. The stick: the shell's "stick key" lines flip between a direction and none
  every 8 ms (629 lines in a quarter of an hour, 300 of them "none"). Cause, in the
  shell's `GamepadInput`: one stick position for all controllers. Measured with nothing
  touched for 4 s: "TV Controller 1" (ours) and "Microsoft X-Box 360 pad 0" silent,
  "Microsoft X-Box 360 pad 3" (another of Steam's virtual pads, presumably the Steam
  Controller's) sending about 200 events a second on each stick axis, values of a few
  hundred to a thousand: its noise at rest. Each of those overwrote the pushed stick,
  the direction dropped, the next report of ours raised it again, and every rise was a
  press. Nothing to do with the game as such; it shows whenever that controller is
  awake. Fixed: a stick position per controller, and the one pushed furthest decides.
- Steam's log for the same minutes, for the record: on every change of the focused
  window Steam reloads the controllers' configuration ("OnFocusWindowChanged to game
  window type: AppID 1040187393" for the shell, "…395" for Eden, the "Last Resort"
  gamepad templates each time). It doesn't switch to a desktop layout.
- The user also wants the other direction: "turning a joycon into a ringcon by sliding
  the joycon in". Nothing in the reports says so either. Built: a pair's right Joy-Con
  that isn't known to be in a ring and has lain on its side for a second (gravity along
  its width, more than 0.7 g, less than 0.5 g along its length: how it sits in a ring
  that is held up) has its rail asked, at most every six seconds, and not once the
  Joy-Con has had no input for two minutes. With the ring found the unit keeps reading
  it, as before.
- Both deployed with the user's "Yes" (service 23:03:55, shell 23:03:58). The shell is
  on screen and found the parked Eden again; both Joy-Cons are ready and wait for L+R.
  Not tried yet.

- The first real try of a pair formed again while a game is parked (the service's restart
  at 23:03, L+R at 23:04:06, the tile at 23:04:18). The user: "Reopened the game that was
  running in the background and it is confused about the joycons". The game showed
  "Attachment Guide: Hold the Joy-Con (L) in your hand and press any button." Eden's log
  has the sequence: "Player 1 holds controller style 5: connected", "Player 1's Joy-Cons
  are taken apart again, as the game asked before", "the left one is now player 2", and
  then "Player 2 has no controller any more". A bug of mine in `OnControllers`: the loop
  applied each player from a copy of the seats made before it started, so player 2, who
  had just been given the left Joy-Con by the split, was applied as empty and
  disconnected. Fixed: each player's seat is read just before it is applied. Built and
  copied to gabecube; the running game still has the bug, so it can't get its left
  Joy-Con back and has to be closed and started again.
- Worth knowing: Ring Fit has its own screen for a Joy-Con that went missing and waits
  for a button on it, so with the fix a re-formed pair should be picked up by the game
  itself.

- The user closed that game with Force close from the tile menu, its first use: the
  shell logged "killing eden.service", the unit ended "inactive" with result "success",
  no Eden process was left, and the service logged "Eden disconnected".

- With the fixed build the user tried the pair formed again while the game was parked:
  "Ok. That worked." So the whole round holds: tile, Home, park, Joy-Cons away and back
  with L+R, continue.

- Committed by the user (8f366e7). Their word on what is left (2026-11-23, around
  midnight): the graphical glitches are not ours to chase ("They were very minor");
  "Rumble is definitely a thing in Ring Fit, should work"; lights: "not sure if there is
  any?" (there are only the player lights: on a Switch the ring's Joy-Con shows player 1
  and the leg's player 2, here both show the pair's number; cosmetic); the IR camera
  "would be indeed interesting"; user profiles "apply to photos, youtube, eden, probably
  some of the settings. So it is an another system that we need to build from the ground
  up." And on the evening as a whole: "this was a pure vibes operation for the most part.
  Going over the code and looking for refactoring and architecture improvement
  opportunities is a must." The lists in `apps/eden/DEVELOPMENT.md` and
  `controllers/DEVELOPMENT.md` and the brief now say so.

- Housekeeping the same night, on the user's word: the test clients used all day lived in
  the session's scratch folder, which is in memory only on the development machine and
  gone at shutdown; the ones worth keeping are now `controllers/tools/` (the emulator
  socket's client and the four cemuhook clients). The recon scripts of the morning, the
  temporary Eden binding script and the screenshots were left behind: what they found is
  in this log. `apps/eden/try.sh` is removed: the shell starts Eden.

- The user on the player lights, which differ from a Switch only in what the leg's Joy-Con
  shows: "If it indeed has no effect aside from that, then it's fine", "Playing the game,
  nobody will really notice anyway", and "we don't worry about the controller LEDs in
  Ring Fit". Left as they are; off the list.

- The user asked for "a short and sweet top level README.md" and noted that parts of
  `CLAUDE.md` "sound like a README", some "a little bit outdated", some that "probably
  should live elsewhere". Done as: a top-level `README.md` (what it is, what is where,
  how it is built); a top-level `DEVELOPMENT.md` that takes the brief's decisions,
  hardware, apps table, Waydroid caveats, open questions and ideas, brought up to date
  (Eden in place of "no emulator installed", the apps as they are now, the Steam Machine
  as the only source); and `CLAUDE.md` cut down to how to work on the project, with
  pointers. The brief's "first task: reconnaissance" section is gone: it was done on the
  first day and its answers are at the start of this log. The protocol references moved
  to `controllers/DEVELOPMENT.md`. Three working rules that had only been said in
  conversation are now in `CLAUDE.md`: ask before each deploy, the user runs sudo,
  secrets stay on gabecube.

- The setup as of 2026-11-23, from the user: "Current setup is the old monitor via DP +
  bluetooth to the soundbar. The soundbar goes to sleep when idle and doesn't wake with
  the machine automatically. It's all temporary". (The brief had said HDMI and an analog
  cable.) `DEVELOPMENT.md` says so now.

- The project has a name: the user called the repository GabeTV
  (https://github.com/Dethware/GabeTV; "GabeTV for the gabecube"). It is public. The
  top-level documents carry the name; the programs keep theirs (`tvshell`,
  `tvcontrollers`). The local checkout's remote still says `Dethware/SteamTV`, the name
  before; GitHub redirects it.

- For next time, from the user (2026-11-24): "Those choice controls in the settings.
  Maybe they could instead be more like classical combo boxes, and not automatically
  capture the input/cursor when they are focused. I have gone in those power menus and
  accidentally changed the timeouts too many times." Noted in `shell/DEVELOPMENT.md`
  under Next. It is the same wish as with reordering the controllers: nothing changes
  without a deliberate step.
- And: "I think youtube should be a separate process/app so we stop rebooting it every
  time we make a change to the shell". Noted there too. With it: "it is about the shape
  of things. YouTube already has its own TV screen form factor and wouldn't integrate
  into the shell anyway"; "The shell should still carry configuration for youtube,
  because I don't want to put that in some youtube web ui hack."
- On the shell's structure: "I'd split take src and qml and split them by topic instead of
  by file type. nobody looking at this cares about the file types too much, but
  semantically, certain things belong together. like all the photos stuff should be in a
  photos module. everything dealing with Steam games should be in a steam module, etc.
  There might be some common controls and code. that can be in a common module".
- On describing apps: "There is probably some more structured way to describe the apps,
  and what configuration they have, that the shell could consume. We can just keep that
  in mind as we add more and have more information about what such an interface would
  need." So: wanted, deliberately not designed yet.

- The log keeps its details (2026-11-24). I had taken the photo server's host name and
  the controllers' Bluetooth addresses out because the repository is public; the user
  removed that commit again: "I think it's fine to have them. The log is mostly a
  reference for you with reasons why things were done, and the details can help." So
  device addresses and internal host names stay; secrets (keys, tokens, sign-in details)
  stay out, as before. `CLAUDE.md` says so.

- Shell layout agreed in conversation (2026-11-24): one QML module per topic (shell,
  common, session, power, audio, photos, steam, apps, youtube, controllers, assistant).
  The user's corrections: the volume display doesn't belong with power ("they seem like
  something that should go into some sort of overlay we don't have yet"), so `audio/`
  for now and an overlay layer later; NowPlayingButton to `audio/`; the app logos stay in
  `apps/` until apps are defined outside the shell: "a definition of what an 'installed'
  app is, which includes a manifest with its name and other details along with an icon
  file".

### The shell by topic (2026-11-24)

- On the user's "let's do it": the shell's `src/` and `qml/` are gone; each topic is a
  folder that is one QML module (`TvShell.Common`, `.Session`, `.Power`, `.Audio`,
  `.Photos`, `.Steam`, `.Apps`, `.YouTube`, `.Controllers`, `.Assistant`), a static
  library linked into the program, with `TvShell` itself keeping `Main.qml`, `TopBar.qml`
  and `SettingsPage.qml` at the top. Done the way Qt's "Writing QML Modules" describes
  for several modules in one binary: `qt_add_library(… STATIC)` + `qt_add_qml_module`,
  the plugin targets linked into the program and `Q_IMPORT_QML_PLUGIN` in `main.cpp`,
  `QT_QML_OUTPUT_DIRECTORY` for the tools. `WebKeys` went to YouTube (it only filters
  the YouTube page's keys); `make-places.py`, a build-time tool, stays at the top.
- One dependency had to turn round: the network layer (common) read the Immich
  configuration (photos) itself. Now the photos module hands it a rule at start that adds
  the API key and prefers the cache for an asset's images.
- Checked: it builds; qmllint gives the same 34 warnings as the old layout (built and
  linted from a worktree of the commit before), after the moved files importing their
  own module where they use its C++ types and the session and YouTube modules declaring
  QtQuick as a dependency (for the `QWindow` of their `window` properties); a headless
  run starts with no QML errors. Not deployed yet.

- The dependencies after the move, as the user asked: a star. The topic modules depend on
  `common` at most (`audio` on `apps` for one logo); the top module imports all ten, and
  `Main.qml` (678 lines) creates their objects and holds 64 places of cross-module glue.
  The user: "there are definitely some opportunities for decoupling and generalizing the
  communication between the core and the modules. let's keep that in mind as we add more
  things." Noted in `shell/DEVELOPMENT.md` (Next), not designed ahead.

- Deployed with the user's go-ahead (23:09). The shell came up and took the screen, and
  the screensaver loaded "4445 photos, shuffled" from Immich: the API key now goes through
  the photos module's rule, as intended.

### Wake-ups on 2026-11-24

- Before that deploy gabecube didn't resolve: it was asleep. The user woke it, "with the
  remote?", and "The front LEDs were lit up", and noted that "earlier today, it looked
  like it got randomly woken up by something".
- The day in the journal: suspended at 00:11:32, 03:58, 11:12, 11:43, 12:45, 20:49,
  21:34; woke at 00:12:11 (39 s later), 10:42, 11:13:47 (80 s later), 12:15, 19:32,
  21:03:50 and 23:08:10. Each time it went to sleep again 30 minutes after a wake with
  nobody there, which is the shell's idle chain. The remote connected 80 to 90 ms after
  every resume, so its connecting says nothing about what woke the machine: it is always
  calling (see "Waking the machine with the remote").
- The kernel doesn't say what woke it: no wake-source line in the log, the wakeup
  counters in `/sys/class/wakeup` last changed about 25 hours after boot (the Bluetooth
  adapter `1-5` at 13 events, the power button `PNP0C0C` at 2), so none of today's
  wakes counted there, and `/sys/power/pm_wakeup_irq` says 9 (the ACPI interrupt, which
  most wake-ups go through).
- The 39 s and 80 s wakes look like the remote waking it by itself, as on 2026-11-22
  (then: after it had been disconnected at suspend). Whether a remote button press woke
  it at 23:08, or its calls did and the press only came after, can't be told from this.
  Earlier the conclusion was that a button press can't wake it.

- The user wanted wake-ups logged ("Yes, log wake-ups"). `system/wake-log`: a user service
  that follows logind's `PrepareForSleep` (through `gdbus monitor`) and at each resume
  writes a report to the journal: time asleep, `/sys/power/pm_wakeup_irq`, the wake
  counters that moved, the kernel's lines from the sleep, and the first seconds after
  (inputs appearing, Bluetooth connections, controller presses). Installed 23:12. A dry
  run of the report for the 23:08 wake: 5911 s asleep, interrupt 9 (ACPI), no counter
  moved, nothing in the kernel log between "suspend entry" and "suspend exit". For the
  kernel to name the wake event, `/sys/power/pm_debug_messages` must be 1 (it is 0); the
  user runs the printed sudo commands once (a tmpfiles.d entry and a keep-list entry).
  Not seen on a real resume yet.

### Two ways of going to sleep (2026-11-25)

- `pm_debug_messages` is 1 since 00:22 (the user ran the sudo commands). The wake log
  reported three resumes this morning.
- The user: picking up the remote, without pressing anything, woke the machine. "When it
  is suspended automatically or from the remote, the LED strip in front is still lit up.
  It can be woken up from this state by the remote. When done manually by pressing the
  power button on the machine, the LED strip goes off. It cannot be woken up." Asked
  whether these are different power states.
- The reports:
  - 00:51:41 → 08:08:20 (7 h 17 min): interrupt 9 (ACPI), the Bluetooth adapter `1-5`
    counted a wake. The remote connected 64 ms after the resume.
  - 08:38:20 → 08:43:09 (289 s): interrupt 1 (i8042) first, and `1-5` counted a wake too.
  - 08:55:45 → 08:56:09 (24 s): interrupts 9 and 1, `serio0` counted two, `1-5` nothing.
    This one was put to sleep by the power button and woken by it again (i8042 is how
    the button arrives here, besides ACPI).
- **It is the same sleep every time:** s2idle, the kernel's only choice
  (`/sys/power/mem_sleep` is `[s2idle]`), with the same PEP transitions and SMU idle mask.
  What differs is what happens before it:
  - The shell suspends through logind after 30 minutes of idle with the screen off. The
    remote's Power button doesn't suspend at all: the shell holds the suspend key and
    turns the screen off (08:50:54 and 08:51:16 today were that).
  - The machine's power button goes to logind ("Power key pressed short"), then
    `steamos-powerbuttond` hands it to Steam, which plays its own suspend ("PrepareForSuspend",
    the `steam_os_suspend.webm` movie) and suspends through logind (`dbus-send`) 2 s
    later.
- **The LED strip** is `valve-leds` (17 LEDs in `/sys/class/leds/valve-leds[0..16]`, a
  driver of Valve's). Its files `brightness`, `brightness_scale`, `effect`, `enabled`,
  `multi_intensity` belong to the user `deck`, so Steam drives it itself: `brightness_scale`
  was last written at 08:56, the resume. *Inferred:* Steam turns the strip off as part of
  its own suspend and back on at resume; when the shell suspends, Steam only hears of it
  ("Suspend notification: entering") and leaves the strip alone. Not looked at what it
  writes.
- **The remote:** before every one of the shell's suspends, here and on 2026-11-24
  (20:49, 21:34, 00:51, 08:38), the shell logged "remote gone" 1 to 3 s before "idle for
  1800 s … suspending". The shell's idle and the remote's own idle both count from the
  last press on the remote; *inferred:* the remote drops its connection after just under
  30 minutes untouched, so the shell always suspends with the remote disconnected. At the
  power button press the remote had been used 4 minutes before and was still connected,
  so the suspend scan's duplicate filter keeps it from waking the machine, as found on
  2026-11-21.
- **But a disconnected remote doesn't call all the time.** The user: "If the remote was
  waking up the machine by poking it all the time, it wouldn't be a weird coincidence
  where I pick it up and the machine wakes up at the same moment." The night agrees: from
  00:51 to 08:08 the remote was disconnected and the machine slept 7 hours, until it was
  picked up. The conclusion of 2026-11-22 ("always calling") came from a remote that had
  just been cut off with `bluetoothctl block`, watched for 10 minutes. *Inferred, to be
  observed:* the remote calls for a while after losing its connection, then sleeps itself
  until it is moved or pressed, and calls again. The 39 s and 80 s wakes of 2026-11-24
  would be the first kind, 08:08 today the second.
- So the LED and the remote are two separate things that happened to go together: the
  LED shows who put the machine to sleep (Steam or the shell), the remote's wake depends on
  whether it was connected when the machine went to sleep.
- To observe this, the wake log now also writes, as the machine goes to sleep, which
  Bluetooth devices are connected, and in each report who asked for the sleep (logind's
  lines). Open: does a disconnected remote wake the machine soon after a sleep starts and
  then never by itself; does the power button with the remote untouched for 30 minutes
  give LED off and a remote that wakes it.
- The new wake log, installed 09:11, saw two sleeps, both from the shell's idle:
  - 09:26:09 → 09:35:53 (584 s). "Bluetooth connected: nothing" (the shell's "remote
    gone" at 09:26:02). Interrupt 9; counters moved on the Bluetooth adapter `1-5`, and
    also on the second USB controller `0000:07:00.4` (with nothing plugged in) and its
    bridge; the kernel named GPIO 58 active at resume, where the other resumes name GPIO 0
    or 18. Nobody was around (the machine went to sleep again by the idle chain at
    10:05:54). Not known what woke it.
  - 10:05:54 → 10:32:00 (1566 s). "Bluetooth connected: nothing" (remote gone at
    10:05:50). Interrupt 1 (i8042) first, `1-5` counted one. The user: "Picked up the
    remote, again with no button presses. The machine woke up by me just picking it up."
    The remote connected 148 ms after the resume.
- So twice now, picking up a remote that had disconnected before the sleep woke the
  machine, and a remote left alone didn't wake it for 26 minutes or 7 hours. The remote
  disconnects itself 4 to 7 s before the shell's 30-minute suspend each time.
- The user asked whether the remote has a "ready" state it wakes to when touched, without
  a press. Looked up: a Google Store spec sheet for the Chromecast with Google TV voice
  remote reportedly lists an accelerometer; Google never announced a feature for it, and
  users on Google's forum report the remote turning their TV and Chromecast back on when
  merely moved (sources: gizmochina.com/?p=354319, mobilesyrup.com 2020-11-17,
  googlenestcommunity.com "Chromecast Remote Draining Batteries Quickly"). One forum
  answer by a community specialist says it has no motion sensor; the observations here
  disagree.
- Seen here while the machine was awake (2026-11-24): the remote dropped its connection
  at 02:58:27, connected again at 03:16:46, and the first key arrived only at 03:17:20,
  34 s later. So it reconnects on being picked up, before any press.
- *Inferred:* the remote sleeps after about 30 minutes untouched and drops its
  connection; moved, it wakes and calls the host to reconnect, so the first press works
  at once. That call is what wakes gabecube. It never calls while lying still, which is
  why the machine sleeps for hours. (The 2026-11-22 trace of continuous calls was a
  remote that had just been blocked, before it went to sleep itself.)
- Useful later: a remote that connects while the machine is awake means someone picked
  it up, before they press anything (DEVELOPMENT.md decision 4).
- The user has never seen a Chromecast turn the screen on from this movement. Their
  reading: the Chromecast, "a very underpowered device", uses the pickup to wake into a
  state where it can answer the buttons quickly, so that the wake-up feels quicker; the
  screen stays as it is until a press. On gabecube the same signal wakes the whole
  machine, and the shell then turns the screen on and stays awake for its 30 minutes.
- Decided by the user: the remote needs a ready state ("if I keep using it, does need the
  ready state"): a wake by picking up the remote leaves the screen off until a press, and
  goes back to sleep without one. The other controllers keep their behaviour: "I don't
  think the other controllers have such a feature at all, so I don't think we should
  change their behavior without more thought put into it."
- The user also asked for more observation of the ways to put the machine to sleep, which
  all behave differently. What each does, read from the code and today's journal, with
  what to expect (*inferred* where not seen):

  | Way | What happens | LED strip | Remote at sleep | Remote wakes it |
  |---|---|---|---|---|
  | Power on the remote | The shell turns the screen off; the machine stays awake (then the shell's idle timer, below) | — | — | — |
  | The shell's idle timer | 30 minutes idle with the screen off: the shell suspends through logind | On (seen) | Disconnected (seen four times) | Yes, when picked up (seen) |
  | Hold Power on the remote → power dialog → Sleep | The shell suspends through logind at once (`PowerActions::suspend`) | On (*inferred*: Steam isn't involved) | Connected (*inferred*) | No (*inferred*, duplicate filter) |
  | Power button on the machine | logind → `steamos-powerbuttond` → Steam's own suspend, 2 s later | Off (seen) | Connected if used in the last 30 min (seen once) | No (user) |
  | Steam's own power menu → Sleep | Not looked at; *inferred:* Steam's own suspend, as for the power button | ? | ? | ? |

  The user: "What workarounds we did with the remote up to now was done based on incomplete
  information, so I think we need to just drop it and do it based on what's true."
  What rests on "the remote can't wake the machine": Power as screen off (the shell's
  `handle-suspend-key` lock, `togglePower`), the key map sending Power as the sleep key,
  the power dialog leaving the remote connected when it switches off controllers, and the
  plans to wake through an IR receiver or CEC. Decided: these stay as they are until the
  redesign; the redesign is built on what the observations show.

  The wake log's "going to sleep" and "asked by" lines tell these apart; the power dialog's
  Sleep shows as `tvshell` with no "idle for 1800 s" line before it.
- The user asked whether anything exists for reading the remote's accelerometer or setting
  its sensitivity, suspecting that people walking by wake the machine. Looked up:
  - Nothing public: no setting on Google's side, no documentation, no project that reads it.
    The forum thread above asks how to turn the motion wake off and has no answer.
  - Google's reference remote firmware (`refDesignRcu`, checked out at `66b7eb9`) has no
    accelerometer at all. The Chromecast remote runs other firmware (TI chip), so its
    motion code isn't public.
  - What the reference firmware does have explains how an Android TV box tells a press from
    a pickup: pressing one of a set of **wake keys** (by default Assistant, Home, Power,
    Input and the app keys) while disconnected sends a **wake packet**, an advertisement
    with its own content, before it reconnects as usual (`app_start_send_wakeup`,
    `app_custom_wakeup_key_load` in `vendor/827x_ble_remote/`). The box, asleep, wakes on
    that packet only. The wake keys and the packet's format are part of the remote's
    stored settings, which the reference firmware lets a host rewrite over Bluetooth
    (`APP_DATA_UPDATE_OVER_BLE`). Our machine's Bluetooth wakes on any advertisement from
    a device on its accept list (the suspend scan found on 2026-11-21), so a reconnect
    after a pickup is enough.
  - *Inferred, not tested:* if the Chromecast remote sends such a wake packet for key
    presses, a key press and a pickup look different in the air. Observable with `btmon`
    while the machine is awake and the remote has dropped its connection: pick it up and
    watch, then press a key and watch.
  - The remote itself, listed and read over bluez once the user woke the machine: the same
    services as on 2026-11-21, none for motion. Device Information: model `A0`, firmware
    `26.7`, hardware `PVT/MP`, software `26.7-GLOBAL`, manufacturer `Google Inc.`. The
    vendor service `6c6f6701-…` ("log" in its first three bytes) has one readable
    characteristic, `6c6f6705-…`: 120 bytes, a run of `be` fill, then 32-bit values that
    read as a Cortex-M fault record (`0xE000ED10` and `0xE000ED08` are the core's system
    control registers, `0x1000….` ROM and `0x2000….` RAM addresses). *Inferred:* the
    remote's last crash or reset, kept for Google; nothing to set.
  - The machine's side has one switch per device, bluez's `WakeAllowed` (yes for the
    remote). Turning it off stops every wake from the remote, presses included.
- Over the air, a pickup and a press compared, with `btmon` while the machine was kept
  awake by a sleep inhibitor (the filter kept only the remote's adverts and connection
  events, nothing with a key):
  - 13:26:34 the remote dropped its connection by itself; the machine went back to its
    passive scan, which starts a fresh duplicate filter. **Not one advert in the next
    10 minutes**: lying still after dropping, it didn't call at all this time.
  - 13:36:36.260, picked up without a press (reported): one `ADV_IND`, the same as on
    2026-11-22 (flags `0x05`, appearance Keyboard, HID and Battery, Google's data all
    `ff`), RSSI −48 dBm. The kernel connected 28 ms later.
  - 14:06:36 it dropped its connection again, exactly 30 minutes after that connection,
    untouched in between.
  - 14:07:19.170, Home pressed with the remote lying flat (reported): **the same
    `ADV_IND`, byte for byte**, RSSI −52 dBm; connected 37 ms later. The shell got Home
    at 14:07:19.404, 0.2 s after the connection: the remote keeps the press and sends
    it once connected. The release came at 14:07:20.224, and in between X's key repeat
    turned the one press into seven Home presses in the shell.
  - So this remote sends no wake packet: a pickup and a press look the same to the
    machine's radio, and nothing at that level can let one through and not the other.
    They differ only once connected, by whether a key follows within a moment. That is
    what the ready state has to go by.
- The user asked whether the time the remote stays connected (30 minutes) can be
  changed, since after a walk-by the machine has to stay awake until the remote lets go.
  Nothing on the remote sets it (none of its services has anything for it, listed
  above). Google's reference firmware, for comparison (`vendor/827x_ble_remote/app.c`),
  uses much shorter times: it drops an idle connection after 30 s
  (`CONN_IDLE_ENTER_DEEP_TIME`); when the host drops the connection it calls to come
  back for 2 minutes (`ADV_IDLE_ENTER_DEEP_TIME`, 120 s) and otherwise for 5 × 2 minutes
  ("adv 10 mins"), then sleeps until a key wakes it. The Chromecast remote's own times
  differ (30 minutes connected), but it may follow the same pattern after the host drops
  it: on 2026-11-22, cut off with `bluetoothctl block`, it was still calling after 10
  minutes, which is where that watch ended. Not measured: when it goes quiet. That time
  is the only lever on our side: the host can drop the remote and keep it out until it
  is quiet, then sleep; the remote can't be used in between.
- The redesign, proposed from the above and agreed (the user, on the 30 minutes awake
  after a walk-by: "Accept 30 minutes"):
  - The shell sleeps only once the remote has let go of its connection by itself (at
    once if it isn't connected). Then the remote is quiet, and moving it wakes the machine.
  - The remote's Power and the power dialog's Sleep turn the screen off at once and put
    the machine to sleep as soon as the remote lets go, at most 30 minutes later.
  - After a sleep with the remote disconnected, a wake where the remote connects within
    about 2 s is the remote's: the screen stays off. Any key or button, from anything,
    wakes it fully; without one the machine sleeps again when the remote lets go. A wake
    where the remote doesn't connect is from something else and wakes fully, as before.
  - Left as they are: the machine's power button and Steam's power menu (Steam suspends
    at once, and the remote can't wake the machine afterwards).
  - Rejected: dropping the remote from the machine's side to sleep sooner. It keeps
    calling for an unknown time (over 10 minutes on 2026-11-22), every call would wake
    the machine, and it can't be used while kept out.
- The redesign's first run on the machine (the shell deployed at 15:58:50, the remote
  connected then). The user came back after well over 30 minutes and found the machine
  awake. The shell's log and wake-log tell the story:
  - 16:08:51 the screen went off (idle); 16:27:16 the remote let go by itself; 16:28:51
    the shell suspended, "idle for 1800 s with the screen off, the remote not connected".
    The idle path, not Power: no Power press is in the log before 16:57.
  - 16:47:15.33 resumed (wake interrupt 9, the USB Bluetooth adapter's wake counter
    moved); the remote connected at 16:47:15.41, 80 ms after, and the shell took it as
    the remote's wake: screen kept off, waiting for the remote to let go. No key came,
    so the remote was moved, not pressed. The user didn't know of anyone touching it
    ("Maybe the remote got bumped?").
  - 16:57:18 Home: screen on, sleep called off. 16:57:33 Power: screen off, sleep asked
    for, waiting for the remote to let go.
  - So it worked as designed, and showed the design's cost: one bump (or a walk-by)
    after 18 minutes asleep kept the machine awake, screen off, for the remote's 30
    minutes.
- The evening after that, from the same logs. The user used the TV 17:02–17:10, then
  left the house and came back at 22:10:
  - 17:35:26 the remote let go; 17:40:32 the shell suspended (idle).
  - 18:07:57 a wake, the remote connected at once, no key: ready state. 18:37:54 the
    remote let go and the shell suspended at once ("asked for").
  - 19:07:30 the same again; 19:37:26 asleep again.
  - 22:10:55 the same, as the user came home.
  - The two wakes while the user was out came 32 and 30 minutes after the remote let go;
    the next sleep then lasted 2.5 hours. Someone was home: the user's wife was packing
    for a trip, "so I guess the table got bumped, or it just picked up the floor
    vibrating". So these were most likely the remote being moved, not calling by itself. Each one kept
    the machine awake for the remote's 30 minutes. Wake interrupts: 9 (ACPI) at 16:47,
    7 (`pinctrl_amd`) at 18:07 and 19:07, 1 (`i8042`) at 22:10; the USB Bluetooth
    adapter's wake counter (`1-5`) moved every time.
- **The screen blinks at every resume.** The user, coming home at 22:10: "the screen
  definitely blinked". The shell's log shows why: gamescope's input counter
  (`GAMESCOPE_INPUT_COUNTER`) moves once by itself 6 ms after the kernel resumes, before
  logind's `PrepareForSleep(false)` reaches the shell. The shell took it as input and
  turned the screen on; 3 to 150 ms later its resume handling turned it off again, and
  the TV lost and found the signal (gamescope logged DP-1 disconnected and back). So the
  open question from the redesign is answered: the input counter does move by itself at
  resume. Fixed: input counts for nothing between the shell's suspend and logind's word
  that it's back; that resume handling decides.

## Jellyfin stops answering the remote (2026-11-25)

- The user: Jellyfin took no more input from the remote. "Going back from a show screen
  was the last input that worked." Only Back and Home still did anything.
- The shell's log: 22:37:31 Jellyfin stopped playing; 22:37:43 and 22:37:55 Jellyfin's
  Back at its start screen handed the screen to the shell (`LeaveApp`), and each time the
  user opened Jellyfin again a second later. So Back still reached the page; the arrows
  and OK did nothing.
- The keys did reach the player: gamescope's focus (`GAMESCOPE_FOCUSED_WINDOW`) and X's
  keyboard focus (`xdotool getwindowfocus`) were both on its window, `0x340002c`.
- A screenshot (`gamescopectl screenshot`) showed Jellyfin's start page with no card
  highlighted. `xdotool key Down` to the window changed nothing; `xdotool key Tab` put the
  focus on the first card (Movies), and the remote worked from there.
- So the web page's focus was left on an element that isn't shown, most likely on the
  show page that Back had just left. In `--tv` the player turns the remote's arrows and OK
  into commands for jellyfin-web's input manager (`native/inputPlugin.js`), which moves
  the focus from the focused element; from one that isn't shown it finds nowhere to go.
  Back doesn't depend on the focus, so it kept working. Tab isn't one of the player's
  commands, so it went to the page as a plain key.
- Leaving to the shell and coming back didn't cause it: it started before the first
  `LeaveApp`, and coming back didn't fix it either.
- Worked around in the player (`apps/jellyfin-desktop/patches/focus-recovery.patch`,
  agreed with the user): when an arrow or OK arrives and the focused element isn't shown,
  that press focuses the first element on screen instead (in an open dialog first, the
  header last). Not while a video plays, where the arrows seek. Deployed; not yet tried, as
  the state can't be brought about at will.

## A Switch game's icon and name, from the game file (2026-11-26)

Research for the library of Switch games (`DEVELOPMENT.md`, Next, item 3): what it takes
for the shell to show a game's own icon and name, read from the game file as Eden and the
Switch do. Ring Fit Adventure first. Read only; no code yet. Sources: Eden v0.2.1's source
(`apps/eden/build/src`), switchbrew.org (pages XCI, Gamecard Format, NCA, RomFS, NACP,
Ticket), and the files on gabecube.

What a game file holds:

- **XCI** (a cartridge dump; `ringfit/ROM.xci`): a header (`HEAD` at 0x100) pointing to a
  root HFS0, a plain file list, not encrypted. Its partitions in Ring Fit: `update`
  (386 MB, the system update the card carries), `logo` (`NintendoLogo.png`,
  `StartupMovie.gif`: the same Nintendo logo on every card), `normal` (empty) and `secure`
  (3054 MB) with the game's five NCAs: 3.04 GB (the program), 13.7 MB, 1.3 MB, 202 kB and
  a 4 kB `.cnmt.nca` (the content list). Which one is the control NCA can't be told
  without the header key; by size most likely the 1.3 MB one.
- **NSP** (an installable package): a PFS0, the same kind of plain list, with the NCAs, the
  `.cnmt.nca` and, for titles that need one, a ticket (`.tik`) carrying the title key.
- **NCA**: its first 0xC00 bytes (the header) are AES-128-XTS with `header_key`, in sectors
  of 0x200 whose tweak is the sector number big-endian. The header gives the content type
  (Control is 2), the title id, the key generation, a rights id and the encrypted key area.
  Without a rights id (cartridge dumps) the section key is in the key area, decrypted with
  `key_area_key_application_NN` for the key generation; with one, it is the title key
  (from `title.keys` or the NSP's ticket) decrypted with `titlekek_NN`.
- **The control NCA** has one section: a RomFS, AES-CTR, under an IVFC hash tree (the data
  is at the last level). In it: `control.nacp` and `icon_<Language>.dat`, one JPEG of
  256×256 for each language the game has (`icon_AmericanEnglish.dat`,
  `icon_BritishEnglish.dat`, …).
- **`control.nacp`** (0x4000 bytes): per language the name (0x200) and the publisher
  (0x100), 16 languages; newer games may store them compressed (raw deflate, for 18 and
  more). Also the display version and the game's settings.
- So everything, even the title id and the name, needs the keys. The only art in the file
  is the square 256 px icon. The logos in the card's `logo` partition and the program's
  logo section are Nintendo's and the same for every game. Covers or banners would have to
  come from somewhere else.

How Eden does it:

- `core/loader/xci.cpp` and `nsp.cpp`: the loader takes the control NCA
  (`GetNCAByType(Control)`) and `PatchManager::ParseControlNCA` (`core/file_sys/
  patch_manager.cpp`) reads its RomFS, `control.nacp`, and the first `icon_<Language>.dat`
  in the order of preference of the language set in Eden (`language_index`). An installed
  update's control NCA takes the place of the game's own (`PatchRomFS`), so an update can
  change the name or the icon.
- The NCA itself: `core/file_sys/content_archive.cpp` and `fssystem/fssystem_nca_reader.cpp`;
  `core/file_sys/control_metadata.cpp` for the NACP (with zlib for the compressed names).
  NSP tickets go into the key manager as the package is opened (`submission_package.cpp`,
  `SetTicketKeys`).
- Keys: `user/keys/prod.keys` and `title.keys` (also `*_autogenerated`) in Eden's data
  folder (`core/crypto/key_manager.cpp`).
- Its cache (`yuzu/game/game_list_worker.cpp`): `user/cache/game_list/<TITLEID>.jpeg` and
  `<TITLEID>.appname.txt`, but only for games installed into its NAND or SD card. Game
  files found in a game folder (our case) are read anew every time and never cached. On
  gabecube `user/cache` holds only `launched.json`: our Eden starts the game directly and
  never shows its game list.
- Eden v0.2.1 takes `nso nro nca xci nsp kip`; nothing about NSZ or XCZ in its source.

On gabecube:

- Game: `~/Games/Emulators/Switch/ringfit/ROM.xci`, title id `01002FF008C24000` (from
  Eden's `user/cache/launched.json` and `user/load/`).
- Keys: `~/apps/eden/user/keys/{prod,title}.keys`, the same files as
  `~/Games/Emulators/Switch/keys/22.5.0/` (`cmp`). `prod.keys` has `header_key`, 22 each
  of `key_area_key_application_NN`, `titlekek_NN` and `master_key_NN`, and `sd_seed`
  (names counted, values not read).
- `~/Games/Emulators/Switch/sdcard/Nintendo` (43 GB) is a real Switch's SD card: 203
  entries under `Contents/registered`, saves, album. That content is encrypted for the
  console it came from (needs `sd_seed`); Eden doesn't use it (its own `user/sdmc` is
  52 kB) and it wasn't looked into.
- No hactool, hactoolnet or nstool. Python 3 without any AES module. OpenSSL 3.6.5
  (`libcrypto.so.3`) and zlib are in SteamOS, and with their headers in the toolchain image.

Ways for the shell, weighed:

- **Eden's cache**: only for installed games, and only filled when Eden's own game list
  has run. Not usable for game files in folders.
- **A small program from Eden's code** (GPL-3, a program of its own; the shell runs it and
  reads the files it writes): for example a mode of our Eden build, `eden
  --tv-game-info <file> <folder>`, that opens the file with Eden's loader and writes the
  icon and the name without opening a window. Same result as Eden, updates included. It
  needs a `Core::System` and Eden's settings (for the language) to start, so a patch into
  Eden's `main` before the window exists; a second, smaller binary linking Eden's `core`
  would duplicate most of the 41 MB. Keys: where Eden finds them.
- **hactool, hactoolnet or nstool**: none on SteamOS; each would be built in a toolchain
  image and kept under `~/apps`. They work in steps through files: the control NCA out
  of the XCI or NSP, then its RomFS out, then reading the NACP. hactool unpacks only whole
  XCI partitions (3 GB for `secure`); nstool can take a single file; hactoolnet needs
  .NET. Keys passed with `-k ~/apps/eden/user/keys/prod.keys`. Another program to build,
  keep and parse the output of, for one small job.
- **Our own reader** in the shell: XCI and NSP lists, the NCA header and key area, AES-CTR
  for the RomFS, the RomFS file table, the NACP. Written from switchbrew's descriptions
  (nothing from Eden), with OpenSSL's libcrypto for AES (XTS with the big-endian sector
  number as the tweak, ECB, CTR) and zlib for compressed names. About as much code as one
  of the shell's smaller parts; reads a few MB per game once, then the shell keeps the
  icon and name in a cache of its own. Keys read from Eden's `user/keys`, in memory only.
  Not covered: updates and personalized tickets (a ticket made for one console).

### Decided: our own reader (2026-11-26)

The user: "We may exchange Eden for something else in the future. They could make some
mistake and the project could disappear just like the ones before. So, let's build it on
our side." The shell reads the game files itself; the library doesn't depend on Eden, its
code or its folders.

The same day the user chose square tiles for Switch games, fitting the icon, over the icon
centred on a 2:3 cover tile like Steam's. And the places: games are every `.xci` and
`.nsp` under `~/Games/Emulators/Switch/`, keys come from the newest version folder in
`~/Games/Emulators/Switch/keys/`, not from Eden's `user/keys`.

### The reader reads Ring Fit (2026-11-27)

The shell's own reader (`shell/switch/format/`) is written: XCI and NSP file lists, the NCA
header (XTS with `header_key`), the control NCA's key (key area or title key), its RomFS
under the IVFC tree (AES-CTR), `control.nacp` and the icon. `switch-gameinfo`, copied to a
temporary folder on gabecube and run on `ringfit/ROM.xci`, printed on its first run:

```
title id:  01002FF008C24000
name:      Ring Fit Adventure
publisher: Nintendo
version:   1.0.0
icon:      92470 bytes
```

The icon is a 256×256 baseline JPEG (Exif, "Nintendo AuthoringTool"), the game's box art.
The whole read took 9 ms: it reads the XCI's header, the root and secure lists, the headers
of the NCAs up to the control one, and a few kB of its RomFS, nothing of the 3 GB program.

Details that switchbrew's NCA page leaves open and that this run settled: the AES-CTR
counter's upper half is the section header's u64 at 0x140 (generation and secure value,
little-endian there) written big-endian, its lower half the offset in the NCA (not in the
section) divided by 16, big-endian; and the key area's third key (index 2) is the CTR key.
Ring Fit's control NCA has no rights id, so the key area path is what ran.

Not tried, because Ring Fit is the only game file on the machine: NSPs, title keys from
`title.keys` or an NSP's ticket, and the compressed titles of 21.0.0 (the NACP page says
"Deflate" without saying whether with a zlib header; the reader tries both).

### One tile with its icon (2026-11-27)

The library went on the TV: `SwitchLibrary` found `ringfit/ROM.xci`, read it on its first
look and cached it, and the Games place showed Ring Fit's icon in a square tile above the
Steam games. The user, after seeing it: "I think we need to rethink this. We got the icon.
That's cool. But maybe it's just fine to put it as the app icon on the existing tile." And:
"it actually kinda broke the navigation in the Steam part of the UI. So, let's keep things
simple."

Decided: Ring Fit's tile on Home shows the game's icon where the other apps have their
logos (`SwitchGame`, one file in, name and icon out); the Games place is Steam's alone
again. The list model, the square Switch tiles, the two-group Games place and the
`eden@.service` template were taken out; the reader and the cache stayed. A library of
Switch games waits until there are more games than Ring Fit, and its place is open.

## Rumble from the game (2026-11-26, late)

The Joy-Cons rumble when a game in Eden asks for it; Ring Fit first. Both ends were ours
already, with a `Rumble` message in the emulator protocol that neither side acted on.

Where Eden has the values: every way a game sets vibration (`SendVibrationValue`, the
vibration devices' activate and unmount, the GameCube and N64 commands) ends in
`EmulatedController::SetVibration(DeviceIndex, VibrationValue)`, after the game's master
volume is applied: a low and a high band, each an amplitude from 0 to 1 and a frequency in
Hz, per Joy-Con (`DeviceIndex` Left or Right; a Pro Controller has one actuator in each
grip). The message carried the bands but not which of a Pro Controller's two: it got a
`motor` byte, and the protocol is version 2.

Facts used for the Joy-Con's side (sources read, nothing copied):

- dekuNukem's `rumble_data_table.md` and `bluetooth_hid_notes.md`: four bytes per
  actuator, left Joy-Con first, in output reports 0x10 (rumble alone) and 0x01 (rumble and
  a subcommand); frequency as `round(log2(f/10)·32)`, high band 81.75 to 1252.57 Hz, low
  band 40.875 to 626.28 Hz; amplitude in two pieces (above 0.23 and above 0.12), the
  lowest piece "TBD". It is fitted here as `round(4·log2(120·a))` (the table's steps are
  2^(1/4) apart): checked against the table it is exact at 0, at 1 and at the frequency
  ends, and one step off only at the table's own boundary points (0.117, 0.23). Above
  amplitude 1: "they can damage the linear actuators".
- SDL (`SDL_hidapi_switch.c`): rumble written at most every 30 ms ("If you send commands
  more frequently than this, you can turn off the controller in Bluetooth mode"), and a
  running rumble written again every 50 ms to keep the motors running. It enables
  vibration with subcommand 0x48.
- Eden's own Joy-Con driver: a single Joy-Con gets the same four bytes in both halves; both
  bands are scaled down when their sum is over 1 (they drive one actuator, so the sum is
  its swing); one value is written after each report read, never refreshed.
- None of these describes how the Switch packs several samples into one block to get
  vibration at 200 a second through slower reports.

Decided and built:

- The service writes in step with the Joy-Con's reports, at least 30 ms apart, and again
  at 40 ms while it vibrates (the third report). Between writes each band keeps the
  strongest value since the last write, so a pulse shorter than that is still felt.
- Subcommands carried "no vibration" in their rumble bytes, which would have cut a rumble
  at every ring lookup or light change: they carry the current rumble now, and count as a
  write.
- Subcommand 0x48 goes out when a Joy-Con is opened.
- Eden passes every new value straight on; its vibration settings and its filter of values
  within 15 ms of each other are bypassed, the service sets the pace.
- Stops: the game's zero; the emulator disconnecting; `Playing` off (Home, a parked game);
  the Joy-Con going quiet; its unit ended. No timeout: Eden leaves out repeated values, so
  a timeout would also cut a steady rumble a game asked for.

Tried with `tools/emulator-client.py rumble` (a test pattern sent 200 times a second) on a
pair and the Pro Controller: low band, high band, both, 10 ms pulses and a sweep all felt;
the Pro Controller's left and right grip alone "quite subtle" (one shell carries both).
During it the reports stayed at 67 to 74 a second, no write failed and nothing went quiet.
`hold` (left on, then disconnecting): stopped 9 ms after the connection ended.

Ring Fit: "Rumble seems to be working. Not sure if the same, but definitely working." 70
rumbles in a few minutes, all on the right Joy-Con (in the ring), none on the leg: 86 ms
typically, 71 ms to 1.9 s; none left on. How often the game sends values wasn't measured
(the service logs a rumble's start and end only), so whether its texture is lost at a
write every 30 ms is open.

Seen along the way:

- After the service was restarted for the deploy, the Pro Controller (a unit as soon as it
  connects) became player 1 before the Joy-Cons' L+R, and Ring Fit asked for the ring on
  the Pro Controller. Units that survive a restart (`controllers/DEVELOPMENT.md`, Next)
  would avoid it.
- Trading players 1 and 2 in the shell while Ring Fit was parked made it worse: carrying
  on, the game didn't ask for the ring again on the Joy-Con, and after a restart of the
  game its controller check wouldn't pass until the Pro Controller was switched off.
  Everything off and only the Joy-Cons woken got it going. Open.
- "write failed: No such device" for a Joy-Con as it was switched off: a write landing as
  its node went. Harmless.

### Rumble wears down the Joy-Cons' link (2026-11-27, night)

Played longer, the Joy-Cons lost their connection and Ring Fit's character stopped now
and then. Measured with throwaway scripts on the emulator socket (lost reports from
`Describe`, per phase of 15 to 30 s, Joy-Cons held still) and, in the service, a debug
line per rumble and a pacing read from a file at run time (both temporary).

- What Ring Fit sends: a value every 5 ms during a rumble (200 a second), the amplitude
  changing from one to the next (low band up to 1.0 at 129 to 241 Hz, high band up to 1.0
  at 258 to 410 Hz), rumbles of 70 ms to 2 s. With a write every 30 ms the Joy-Con got
  about every sixth value, which may be why it felt "not sure if the same".
- Rumble writes cost reports. Soundbar on: no rumble 0 to 8 lost per Joy-Con in 20 to 30 s;
  rumble at the service's pace (a write every 30 to 40 ms) 76 and 86; writes of "no
  vibration" at the same rate 110 and 127, so the writes and not the vibration. Both
  Joy-Cons dropped within seconds after each such phase ("disconnect reason: Unknown").
- While written to, a Joy-Con sends more reports than its 67 a second (77 to 93), and
  goes on doing so for a while after the writes stop; its reports then arrive in
  bunches.
- The soundbar (Philips HTL3310) is on the same Bluetooth adapter as the Joy-Cons, playing
  over A2DP whenever it is on (the shell's stream is always running). It was on from
  23:18 to 23:56 and from 00:19 on. With it on, the links were worse: in one run hundreds
  of reports were lost with no rumble written at all, starting when it connected. The
  Chromecast remote is on the adapter too (LE). Wi-Fi is on 5 GHz.
- With the soundbar off it still happened: writes every 60 ms (unsynced) lost 34 and 27
  reports and the right Joy-Con dropped; writes right after a report, at most every 60 ms
  (about 13 a second), lost 6 to 32, and one Joy-Con ended the connection itself
  ("Connection terminated by remote", what a switch-off looks like) or, in the next run,
  kept sending 90 reports a second in bunches after the writes stopped and dropped 20 s
  later.
- The kernel's `hid_nintendo` (read for facts): every output on Bluetooth at least 60 ms
  after the last, only after an input report, 4 ms later, and only when the last three
  reports came 8 to 17 ms apart ("Sending subcommands and/or rumble data at too high a rate
  can cause bluetooth controller disconnections"); "off" sent 5 times; a rumble is sent
  only while the report's vibrator byte is set. Copied as a pacing here: the cadence
  check never passed, because the reports reach the service two at a time every 30 ms
  (gaps 30, 0, 30) once a link has been written to, and stay that way across restarts of
  the service (the connection lives on). Without the check, see above. The vibrator byte
  (report byte 12) was set in every report, with or without rumble (`b0`, `0b`, `a0`,
  `09`), so it gates nothing here.
- Open: why this adapter (Valve 28de:1401) and these Joy-Cons can't take writes that SDL,
  the kernel and Eden send at similar or higher rates. Next: a Bluetooth trace (`btmon`)
  of a run.

Found with `btmon` (run as root by the user; `btmon -r` reads the file without root): the
Joy-Cons keep their links in sniff mode with a 15 ms interval, and for every packet the
computer sends them, the kernel's Bluetooth stack first sends "Exit Sniff Mode"; the link
goes Active, and some 20 ms later the Joy-Con asks for sniff again:

    00:43:17.751  Mode Change → Sniff 15 ms
    00:43:17.752  < Exit Sniff Mode (a write waiting)
    00:43:17.772  Mode Change → Active
    00:43:17.789  Mode Change → Sniff 15 ms
    00:43:17.798  < Exit Sniff Mode

In a minute with rumble every 60 ms: 212 sniff exits and 458 mode changes on the two
links; the left Joy-Con dropped 13 s after the writes stopped. That flapping is the lost
reports, the bunches, the pairs 30 ms apart and the drops. (The soundbar's A2DP on the
same adapter makes everything worse, but isn't the cause.)

With the Joy-Cons' link policy set to refuse sniff (`hcitool lp <address> RSWITCH`, as
root, for the connection only): the first write took each link Active, and it stayed
Active. 2 mode changes in the whole run, and rumble at the original pace (a write every
30 ms, refreshed at 40 ms) lost 0 to 4 reports per 20 s per Joy-Con, in no rumble, rumble
and after, with no drop.

Active, a Joy-Con sends 89 reports a second, not 67. Its timer (report byte 1) still
ticks every 5 ms: it moves 2 per report 69 times a second and 3 per report 23 times
(207 ticks). The three readings in a report are never byte for byte the previous report's
last, so they are three fresh readings spread over the 10 or 15 ms the report covers, not
5 ms apart. The service takes every report as three readings 5 ms apart, so its motion
clock ran 1.33 times too fast (6.66 s of readings in 4.98 s): Eden would integrate a third
too much rotation.

Decided with the user: the service itself refuses sniff, over a raw HCI socket (the unit
grants `CAP_NET_RAW`), "while we want to use rumble": a game being played is one such
time, haptics in the shell may be another later. Not `hcitool` from a udev rule
(deprecated, may go in an update). The user: the motors surely use more power than the
radio's mode; and wanting rumble could be a lease, kept for some seconds after each
rumble asked for (next, `controllers/DEVELOPMENT.md`). Motion now takes the readings'
times from the timer (a report's three readings spread over its ticks).

Tried, soundbar off:

- Sniff mode (nobody wants rumble): 67 reports a second, motion clock ×1.00 (1.005,
  0.993 over 5 s).
- A test client saying it plays: "is kept active (sniff mode refused)" for both, 89
  reports a second, motion clock ×1.00 (1.001, 0.999); half a second after it left, "may
  sleep again" and back to 67 a second, with no reconnect.
- Refusing sniff only takes effect at the next packet the computer sends: with nothing
  written, a Joy-Con stays asleep (one run showed 67 a second for the left one).
- Turned once round by hand, face up on a table, about z: 321 and 359 degrees asleep, 360
  awake. By hand it varies by tens of degrees, but the old timing would have shown about
  470 awake.
- Once the adapter reset itself: "hci0: command tx timeout", "Resetting usb device", all
  links dropped at once (Joy-Cons and remote), the first such reset in the journal. 5 s
  after sniff was allowed again, while the service asked the right Joy-Con's rail once a
  second (a probe: it lay on its side), each ask a packet that woke its sleeping link.
  Whether the link policy had a part isn't known; to watch.

Ring Fit with the soundbar on again (it was switched on just before): the links were kept
awake ("Eden is playing", "kept active"), and still the left Joy-Con lost 556 reports and
dropped after about a minute and a half, the right one lost 2664 since it connected; 34
rumbles meanwhile. The clean test above was with the soundbar off, so this doesn't say
whether the soundbar's audio stream or the play (the Joy-Cons in the ring and on a leg,
held, moving) is to blame. Switching the soundbar off froze Eden (it seems not to cope
with its audio device going away). The user is changing the audio setup; Ring Fit is to
be tried again without Bluetooth audio.

The next round (01:38) was spoiled by a bug: Joy-Cons that connected after Eden already
said it was playing weren't kept awake (the emulator server hears of a Joy-Con before
`main.cpp` connects its request to `Bluetooth`, so the first request was lost); their
links flapped again and the right one dropped. Fixed: `main.cpp` also applies the state a
Joy-Con already has.

On the table, links awake, the right Joy-Con in the Ring-Con, 20 s phases: ring read, no
rumble / rumble / no rumble lost 3/0, 3/0, 2/0 (left/right); ring not read 2/0, 7/0, 5/0.
Reading the ring with rumble is fine once the link stays awake.

Ring Fit, soundbar off (sound over HDMI), the fix deployed: a whole level played, from
01:47 to about 02:00, 254 rumbles, no drop, no Joy-Con quiet, no adapter reset. Lost
reports in that time: left (on the leg) 795, right (in the ring) 1317, about 1.5 and 2.5 %
at 89 reports a second; the sessions of 2026-11-23, before rumble and asleep, lost 760 to
1470 on the left. So with the soundbar's Bluetooth audio off, rumble works in Ring Fit.
With it on, the Joy-Cons still dropped (above); not tried again since the fix.

Seen in passing: on the screen after a level, the right Joy-Con's stick doesn't move
through the menu, the left one's does; the user says the menu is odd on a Switch too.

The user on the rumble in Ring Fit: "Rumble feels fine." The soundbar moves to the new TV
over HDMI ARC (2026-11-27), so Bluetooth audio leaves the adapter; the Joy-Cons with
Bluetooth audio on are to be tested again later.

## HDMI sound through the TV (2026-11-27)

The LG OLED G6 is connected on HDMI (`HDMI-A-1`, the DisplayPort monitor is gone), the
soundbar on the TV's ARC port. The machine had no HDMI sound: PipeWire played to "Dummy
Output", the HDMI card's profile was "off" and every HDMI port "not available".

- The machine side was fine: the connector connected, the HDA ATI HDMI card present and
  bound to amdgpu.
- No ELD on any of the card's four pins (`/proc/asound/card0/eld#0.*`: `monitor_present
  0`, `eld_valid 0`), so the sound driver never heard of a monitor.
- The cause was the TV's EDID (`/sys/class/drm/card0-HDMI-A-1/edid`, name "LG TV SSCR2"):
  its CTA block sets "basic audio support" but its Audio Data Block is empty (`20`, tag 1
  with length 0), no formats at all. amdgpu gives no ELD for a sink without audio formats.
- The TV's sound settings (audio input and output) were bitstream and pass-through. The
  user set both to PCM; the EDID then lists Linear PCM, 2 channels, 32–192 kHz, 16–24
  bits, the ELD is valid with one format, and PipeWire switched to "HDMI / DisplayPort
  Digital Stereo (HDMI)" by itself. Nothing changed on the machine.
- *Inferred:* in pass-through, the TV fills the audio block with what the ARC device
  takes, and it hadn't learnt the soundbar's formats (HDMI-CEC/SIMPLINK). Not tested.
- Seen in passing: the EDID's video block goes up to 4K30 and 1080p120, no 4K60 or
  4K120. LG offers more only with "HDMI Ultra HD Deep Colour" on for that input.

The volume, the same day. The remote's volume buttons change the soundbar's volume, but
the shell's indicator stays at 100 %. The remote sends no IR at all (the user); its
buttons reach only the machine, over Bluetooth.

- SteamOS has the path: `cecd` (D-Bus `com.steampowered.CecDaemon1`, owns `/dev/cec0`)
  and `cec-audio-control` (user service, started 11:52 when the TV came up). The latter
  serves PipeWire's `org.pipewire.ExternalVolume` over varlink for route `hdmi-output-0`:
  PipeWire hands the HDMI output's volume and mute to it, and it sends them over CEC to
  the audio system (logical address 5, the soundbar behind the TV's ARC).
- So the HDMI route has no volume of its own in PipeWire (`volumeStep` FLT_MAX,
  `volumeControlFlags` set), the sink stays at 1.0 (`wpctl set-volume 0.9` leaves it at
  1.00), and WirePlumber logs "spa.alsa: Failed to write volume: Invalid argument" at
  each press.
- The level can't be read: `cec-audio-control` logs "Can read: false, Volume: 0, Mute:
  true" at start, and cecd's `GetAudioStatus` times out for address 5 and for the TV (0).
  The soundbar (or the TV in between) doesn't answer Give Audio Status. Mute is a toggle
  the machine can't check either.
- cecd also wakes the TV over CEC (`WakeTv` and `SuspendTv` true): "TV did not respond
  to wake immediately" several times at 11:52–11:53.

Deeper, the same day:

- The bus (`cec-ctl --show-topology`; the adapter is `cros-ec-cec`, the machine is
  3.0.0.0, Playback Device 1): the TV `OLED55G64L` (LG, CEC 1.4) and the soundbar
  `HTL3310` at 2.0.0.0, Audio System (CEC 1.4, vendor 0x525352). The soundbar answers
  Give System Audio Mode Status in 50 ms ("on"), its name and power.
- Give Audio Status (0x71) to the soundbar, and to the TV: acknowledged, never answered,
  not even late (cecd's `ReceivedMessage` signal watched for 12 s; only the TV's own
  broadcasts arrive). Volume Up and Down sent to the soundbar change its volume, but it
  sends no Report Audio Status after them either. *Inferred:* it reports its level only
  to the TV, if at all. The adapter has no Monitor All, so messages to the TV can't be
  seen.
- `cec-audio-control`'s capabilities (`GetCapabilities` over varlink): `writeVolumeRelative`
  and `writeMuteToggle` only; no reading, no absolute volume, no `writeMuteValue`.
- Mute: the shell sets the sink's mute through libpulse. PipeWire turns that into a mute
  value, which the service doesn't take, so nothing is sent and the mute flips back (the
  "blink" in the shell). PipeWire's own device command (`pw-cli send-command 42
  VolumeControl '{ "muteToggle": true }'`) didn't reach the service either. Calling the
  service's `WriteMuteToggle` directly makes cecd send Mute to the soundbar, and the
  soundbar mutes (the user); a second call unmutes it.
- The external volume code isn't in PipeWire's upstream source (the GitHub mirror,
  2026-11-27): it is SteamOS's own (`pipewire 1:1.6.8-1.7`, `cec-audio-control 0.1.0-7`,
  `cecd 0.3.0-2`).
- The TV shows no number either: at a volume press it shows + or − and an icon, which
  turns into a mute icon when the volume gets very low (the user). *Inferred:* the TV
  hears something of the soundbar's level, but nobody shows a number. That is with the
  LG remote: with the machine's remote (volume over CEC from the machine), the TV shows
  nothing at all.
- With the TV's audio input on bitstream and its output on auto (12:37): the EDID's audio
  block is empty again and HDMI sound gone, and Give Audio Status to the soundbar and the
  TV still goes unanswered, as do the volume presses. So the audio setting doesn't decide
  whether the soundbar reports its level.
- Input back on PCM, output left on auto: the audio block lists Linear PCM again and HDMI
  sound is back. So the TV's audio *input* setting decides what it offers the machine.
- The TV's "Universal Control Settings" (HDMI 2, Sound Bar, pick a manufacturer) teaches
  the LG remote IR codes for the soundbar. Not used.

Decision: volume by infrared to the TV, not over CEC from the machine. The user's idea:
with CEC from the machine the TV shows nothing, and the shell would need to take the
volume keys from everywhere to show anything itself.

- `system/remote-ir.py` writes the codes over the remote's IR programming service (see
  "IR programming service", 2026-11-22): LG's NEC codes, address 0x04, `0x20DF40BF`
  volume up, `0x20DFC03F` down, `0x20DF906F` mute; 38 kHz, 33 % duty. Volume as type 3
  (the frame, then NEC's repeat frame while held, each filling 108 ms), Mute as type 1
  (once per press). Loaded 13:10.
- The TV changes the soundbar's volume and shows its + or − and mute indicator (the
  user). No level there either: the soundbar seems not to report one at all.
- The buttons no longer reach the machine over Bluetooth: no volume call to cecd in the
  10 minutes after 13:10 while the user pressed volume and Mute. So the keys with a code
  go out as IR only (Suppress left empty).
- A shell change that sent its Mute through `cec-audio-control`'s `WriteMuteToggle`
  itself was built but dropped unused: Mute no longer reaches the machine.
- Not checked: whether the codes survive a battery change or a new pairing (they are in
  the remote's flash, by the reference firmware).
- Mute pressed three times in a row opens a TV menu over the picture ("Select a menu
  item": model, software version webOS26 43.21.98, Reset, Auto Tuning, Set-Top Box Power,
  Auto Power Sync, Show LG logo, No Signal Image). It is the TV's own reaction to three
  mutes, which LG's remote would trigger the same way. Left as it is.
- Tried instead: Mute as the soundbar's own IR mute, so the TV wouldn't see it. No code for
  the HTL3310 is published. Philips soundbars in the Flipper IR database
  (Lucaslhm/Flipper-IRDB) all use RC6 mode 0, Mute command 0x0D: address 0x10 (HTL2163,
  TAB5105, the HTS8010S's volume and mute), 0x20 (HTL2060). Sent as RC6 at 36 kHz, one
  frame per press and as two frames alternating the toggle bit; then three frames per
  press at addresses 0x20, 0x15, 0x11 on Mute and 0x04, 0x05, 0x14 on Input. The remote
  blinks in a phone camera, the soundbar reacts to none of them (13:42–13:45). Not known
  whether the encoding is right: no device here is known to take RC6. The machine has no
  IR receiver (`/sys/class/rc` only has CEC) to read the soundbar's own remote. LG's Mute
  loaded back at 13:45.

A click at the start of sounds, the same day: Steam's UI sounds have a short impulse at
their start that isn't part of them (first heard as "eats the first fraction of a second").

- PipeWire only allows 48 kHz (`clock.allowed-rates = [ 48000 ]`), so no rate switch.
- SteamOS's own settings already keep the HDMI output running between sounds
  (`node.pause-on-idle = false`, `session.suspend-timeout-seconds = 3600`); setting them
  again in a WirePlumber rule changed nothing. Adding `dither.noise = 2` (never exact
  digital silence) didn't help either. Both removed again.
- Short beeps played from the machine (`pw-play`, 0.25 s) came through.
- With the TV's own speakers in place of the soundbar, no click (the user, though the TV
  speakers are faint). So it happens between the TV and the soundbar, or in the soundbar.
- Still there after rebooting everything.
- The `dither.noise = 2` above proved nothing: PipeWire counts it in steps of the output
  format (here s32), and HDMI carries 24 bits at most, so it was cut off. Tried again with
  16 (the maximum, about −96 dB), the output running with Steam's stream: still clicks.
  So the soundbar doesn't click for coming out of digital silence. Removed again.
- With the TV's own speakers the TV also shows its volume level (the user). So the missing
  level, like the click, belongs to the soundbar (HTL3310).
- No firmware for it: Philips's support page lists it as discontinued, with no software
  section. Decision (the user): replace it with an LG soundbar; an LG NS90 is ordered.
- Two other suspects: the TV's audio *output* went from PCM to Auto at 12:37 (for the
  bitstream test) and stayed there, and the clicks were reported after that; set back to
  PCM, the clicks stay. And static, not tested: the user got shocks from everything that
  day (a sofa cover off), and the soundbar took one at some point.
- Steam's UI sounds played with `pw-play` (navigation, OK, tab, toggles, bumper, modal;
  normal and at full scale; at 100 ms and at 256 samples of latency): no click.
- A recording of the HDMI output's monitor while moving through Steam's menus: Steam's
  stream ("Chromium", 512/48000, the same quantum as the rest) plays its navigation sound,
  `steamui/sounds/deck_ui_misc_10.wav` (correlation 1.0), exactly as stored, nothing
  added. The file has a 2 ms tick at its start (−31 dB, 1–3 ms), then near silence until
  20 ms, but the click isn't that: played on its own, with or without the tick, no click.
- The user: moving through Steam's menus without a break, no clicks; after a pause, the
  first sound clicks.
- With a 20 Hz tone at −50 dBFS playing all the time (inaudible), no clicks after pauses
  either. So the soundbar (or the TV's ARC output) goes quiet on silence and clicks when
  sound starts again; −96 dB of dither is below whatever it listens for.
- Decision: `system/soundbar-keepalive`, a user service playing that tone all the time
  (pw-cat, stream "soundbar-keepalive", role Notification), until the LG NS90 replaces the
  soundbar; then to be tried without it first. Installed 16:48. Not checked yet: whether
  a stream playing all the time keeps the machine from sleeping.

### The keep-alive doesn't keep the machine awake (2026-11-27)

Left idle from 16:53 with the keep-alive playing:

- 16:58:41 screensaver; 17:03:41 screen off, the shell sends the TV to standby. With the
  TV in standby the HDMI sink is gone and PipeWire falls back to "Dummy Output"
  (`auto_null`), so the keep-alive plays into nothing.
- 17:23:41 sleep (the remote had let go long before); the shell doesn't count the
  keep-alive as music playing.
- 18:16:29 the remote woke the machine, screen kept off; Home a second later turned the
  screen and the TV on. The HDMI sink came back and the keep-alive is linked to it again
  by itself.
- Steam's "Turn on TV when resuming from sleep" is now off too (`wake_tv = false`).

## The TV off with the screen (2026-11-27)

The power menu's Turn off screen (the shell's ScreenOff, DPMS off) leaves the TV on,
showing "No Signal" with LG's animation. The user: no signal should mean off.

- 15:04:20, cecd's D-Bus `CecDevice1.Standby(0)` (Standby to the TV): the TV went off,
  and 0.6 s later cecd asked logind to suspend the machine ("suspend requested from client
  PID 2787 ('cecd')"). The machine slept until 15:06:00 (woken by the power button: wake
  counter `serio0`). At the resume cecd tried to wake the TV ("TV did not respond to wake
  immediately").
- *Inferred:* the TV answered by sending Standby to all (LG's "Auto Power Sync" is on in
  its menu), and cecd, with `allow_standby = true`, put the machine to sleep for it.
- cecd's settings come from SteamOS Manager (`~/.config/cecd/config.d/99-steamos-manager.toml`:
  `wake_tv`, `suspend_tv`, `uinput`, `allow_standby`, all true), which follows Steam's
  HDMI-CEC setting (`SteamOSManager1.HdmiCec1.HdmiCecState` = 3).
- The user switched off in Steam (Settings, Power, HDMI CEC): "Enable remote control
  input", "Resume from sleep when turning on the TV", "Sleep when turning off the TV".
  The file then has `uinput = false`, `allow_standby = false`. The TV is to be just a
  screen; the LG remote only for when it does something by itself.
- 15:13:03, `Standby(0)` again: TV and soundbar report `standby` (Give Device Power
  Status), the machine stays awake. `CecDevice1.Wake()`: the TV reports `on` within 2 s
  and shows the machine (cecd still logs "TV did not respond to wake immediately").
- Decision: the TV follows the shell's screen. TvPower in the shell sends Standby when
  the screen sleeps and Wake when it wakes, and wakes the TV again after a resume with the
  screen on. "Turn on TV when resuming from sleep" has to be off too, or cecd lights the TV
  at a resume where the shell keeps the screen off (the remote's ready state).
- Deployed 15:48: the power menu's Turn off screen sends TV and soundbar to standby, a key
  brings them back with the shell (the user: "it works"). "Turn on TV when resuming from
  sleep" was still on at that point (`wake_tv = true`).

## A SteamOS update (2026-11-27)

Before it, at 3.9.5 (build 20261023.100), kernel 7.2.7-valve2-2-neptune-72: Qt 6.11.1
(base 6.11.1-1, declarative 6.11.1-3, webengine 6.11.1-4, multimedia 6.11.1-1), pipewire
1:1.6.8-1.7, wireplumber 0.5.18-1.2, cecd 0.3.0-2, cec-audio-control 0.1.0-7, gamescope
3.16.30.1-2, bluez 5.87-2.4. Keep-list in `/etc/atomic-update.conf.d/`: tv-controllers,
tv-remote, tv-wake-log-keep.

It was Steam's own client update, not SteamOS: Steam restarted itself at 16:51, SteamOS
stayed 3.9.5 and the machine didn't reboot. Afterwards the shell, the controller service,
the keep-alive, the wake log and the watchdog all running, and cecd's settings as the user
set them (`uinput = false`, `allow_standby = false`).

## YouTube moves out of the shell (2026-11-28)

The shell restarts with every deploy and YouTube restarted with it. Decided:

- A generic web app program (`apps/webapp`), with YouTube as its first site: a folder with a
  description (address, user agent, scripts) and page scripts. Chosen over a module per site
  built into the program, since a site is then added without building anything, a step
  towards apps described outside the shell. YouTube's own parts are already JavaScript run
  in the page; SponsorBlock's request moves into the page script too.
- The program and a site's scripts talk through functions the program calls in the page
  (`webapp.player()`, `webapp.back()`), as the shell does today with `runJavaScript`. Not
  QWebChannel: the player has to be read in the page's own world, where the page would see a
  channel and could call the program through it.
- When left with nothing playing it stays loaded and frozen, as today, with a per-app "When
  left" setting in the shell to close it after a while.
- What plays goes to the shell over MPRIS, with our own metadata keys for music and cover
  only; the shell's `ManagedApp` reads it for every app.
- The settings stay in the shell's Settings; the shell writes them to a file per app that the
  program watches (works without the shell running).
- Code both programs use (the controller as keys) goes in a shared folder, `shared/`.

Scaffold in place; not checked yet: whether Qt WebEngine registers an MPRIS player for a page
by itself.

### Qt WebEngine's own MPRIS player (2026-11-28)

It does: the shell's process owns `org.mpris.MediaPlayer2.chromium.instance3904` (3904 its
PID), `Identity` "tvshell" (the application name), no introspection data.

- Nothing playing: `Stopped`, empty metadata, `CanPlay` and `CanPause` false.
- A music track playing in YouTube: `Playing`, `xesam:title` and `xesam:artist` as YouTube
  shows them, `mpris:artUrl` (a file in `/tmp`), `mpris:length`, `Position`, `CanSeek`,
  `CanGoNext`, `CanGoPrevious` true.
- `Pause` paused the video (status `Paused`, and the shell's own reading of the page saw it
  stop), `Play` played it again.
- No music mark, which MPRIS has no key for.

So the web app's own MPRIS player is dropped: only whether it is music (and cover only) goes
to the shell on a small interface of the web app's own (`apps/webapp/DEVELOPMENT.md`,
Playing). The shell finds a web app's player by the prefix and by its owner's process being
in the app's unit.

### Building it (2026-11-28)

- YouTube's TV page sends `connect-src` in its Content-Security-Policy, a long list of
  Google's and ad servers without sponsor.ajay.app. A script in the program's own world is
  held to the page's rules too, so SponsorBlock can't be asked from the page. Its server
  sends `Access-Control-Allow-Origin: *`, but that doesn't help against the page's own
  rules. So the program fetches for the page, only for prefixes in `site.json`, over a Qt
  WebChannel placed in the program's world (`webChannelWorld`), where the page can't see
  it. QWebChannel was rejected earlier for the hooks that read YouTube's player; those stay
  with `runJavaScript` in the page's world.
- Qt WebChannel is on SteamOS (`qt6-webchannel` 6.11.1-1) and in the toolchain image.
- In QML a C++ property marked `SCRIPTABLE false` isn't there at all: the plan of two
  property names on one object (lowercase for QML, capitalised for D-Bus with
  `ExportScriptableProperties`) failed with "Property 'music' does not exist". The D-Bus
  names went to a `QDBusAbstractAdaptor` instead.
- In the toolchain image a program started without a display printed nothing: Qt logs to
  the journal when stderr isn't a terminal. `QT_FORCE_STDERR_LOGGING=1` shows it.
- The first deploy came during screen off. The new shell started thinking the screen was
  on, so it sent nothing, and gamescope kept the screen asleep and the TV stayed in
  standby; Home changed nothing either. Woken by hand (`gamescopectl
  drm_sleep_external_screen 0`, cecd `Wake`). The shell now says screen on and wakes the TV
  once when it starts.
- Leaving YouTube didn't pause "Microsoft has admitted defeat", and the top bar showed it:
  the web app counted it as music. Its mark is `MUSIC_VIDEO_TYPE_PODCAST_EPISODE`: YouTube
  files podcasts under the same field as songs, and any mark counted as music (also in the
  old page inside the shell). The user: podcasts may well keep playing, but it should be a
  choice. Now the web app tells `song` (`_ATV`, `_PRIVATELY_OWNED_TRACK`), `music-video`
  (`_OMV`, `_UGC`, `_OFFICIAL_SOURCE_MUSIC`), `podcast` and `video` apart, and Settings,
  YouTube, chooses which keep playing when YouTube is left: songs, music videos and
  podcasts at first, ordinary videos pause. The list of marks is from a third-party library
  (ytmusicapi_dart's `VideoType`), not from YouTube.
- Opening YouTube played a video by itself, after it had been paused from the top bar:
  opening an app always sent MPRIS `Play`. Now only a pause from leaving is continued on
  opening (a parked game in Eden always is).
- Opening Jellyfin paused YouTube's music at once, and opening YouTube again then
  continued it. The user: only pause when the other app plays something itself. Now an app
  that starts playing pauses the others, and that pause isn't continued on opening. A
  photo's video no longer pauses anything (the user: it shouldn't interrupt background
  music; maybe the music gets quieter, later).
- A paused ordinary video had nothing in the top bar. The user: show it there; OK opens
  YouTube to continue it. It then showed the title for about a second and turned to
  "YouTube": once the web app hides and freezes the page, Chromium's player says
  `Stopped` with empty metadata. So the shell keeps the title of a video that leaving
  paused, and on opening sends Play only once the player is back to `Paused` (at most 5 s
  later), since a Play that reaches a frozen page may be lost.
- That didn't continue the video either: after the page woke, Chromium's player went from
  `Stopped` straight to `Playing` (the user's own press), never `Paused`, so no Play went
  out; the user saw YouTube's interface loading after the wake. In the shell before, the
  page was frozen too, but it seemed loaded already. Now the shell asks the web app
  (`Resume` on `local.webapp.youtube`), which plays the main video in the page once the
  page runs, and a page that was just left stays unfrozen for a minute. Not known yet
  whether the loading after a wake is the freezing itself.
- Coming back within the minute worked. After more than a minute YouTube showed "Who's
  watching?" and the video's place was gone: YouTube treats a page woken from a freeze
  like a TV app brought back, it seems. Before, inside the shell, the page was frozen as
  well, so the difference isn't understood. Now the web app never freezes: a page left
  with nothing playing is only hidden, which Chromium throttles anyway. Not known yet
  whether YouTube also shows the picker after being hidden for long.
- Hidden for a few minutes, then shown: YouTube loaded the video again and continued from
  the last point it had seeked to, not from where it was paused. The user: keep it open
  for a while; the screensaver, screen-off and sleep times are there anyway, and an app
  suspend time fits beside them. Now Settings, Apps, has a web app suspend time, one for
  every web app since more are to come (half an hour at first): only then is the page
  hidden.
- "Boneklezmer - Extended" (an extended track uploaded as an ordinary video) paused when
  YouTube was left: it has no music mark. Its player response has no category either: on
  the TV page it holds only `responseContext`, `playabilityStatus`, `streamingData`,
  `heartbeatParams`, `playbackTracking`, `captions`, `videoDetails`, `playerConfig`,
  `storyboards`, `trackingParams`, `adBreakHeartbeatParams` (no `microformat`), and
  `videoDetails` only ID, length, channel, thumbnail and flags. But YouTube shows a music
  note on its tile and offers "Switch to audio" in the player, for this video and not for
  others. Looked at with DevTools (a temporary drop-in with `QTWEBENGINE_REMOTE_DEBUGGING`,
  removed again): the player's actions (`ytlr-player-actions-container`,
  `__instance.props.data.settingActions`) include
  `TRANSPORT_CONTROLS_BUTTON_TYPE_MUSIC_DISPLAY` (icon `AUDIO`, a popup "Choose what you
  want to appear on the screen while you're playing music", signals `AUDIO_MODE` and
  `VIDEO_MODE`). Recorded every 2 s: there for this video with the controls shown and
  hidden, never for an ordinary video (a Wind Waker video). Now a video without a mark but
  with that action counts as a music video.
- Tried: it paused the first time, kept playing every time after (also after a restart of
  YouTube). The log: a video counts as `video (no mark)` for about two seconds after it
  opens, until the player's actions are there and it turns `music-video (no mark, music
  display)`. The first time it was left two seconds in, still inside that window. The user:
  note it, don't pursue it now.
- Stopping the web app (`systemctl --user restart`) once ended in "double free or
  corruption" and a core dump on the way out. Not looked into yet.
- How long an app stays loaded when left became a setting (Settings, Apps): YouTube stays,
  as it did inside the shell; Jellyfin closes after ten minutes, as before.

## Waydroid prerequisites

- Kernel has the Rust binder driver built in (`CONFIG_ANDROID_BINDER_IPC_RUST=y`,
  `CONFIG_ANDROID_BINDER_DEVICES=""`); `binder` is a registered filesystem. No `/dev/binder*`
  exists until binderfs is mounted (needs root). Untested whether Waydroid works with the Rust driver.
- `waydroid` and `lxc` are not installed; installing via pacman means unlocking the rootfs
  and redoing it after each update. `podman` and `distrobox` are present.

## Other notes

- HDMI-CEC: `cecd` runs as a user service, `/dev/cec0` exists, state `extended`,
  remote passthrough enabled. `steamosctl` has wake/suspend TV and `hdmi-cec-make-active`.
- No Flatpaks and no browser installed.
- Current monitor: the connector lists a 3840x2160 mode, but gamescope's usable list tops
  out at 2560x1440@60, and our full-screen windows were 2560x1440. *Inferred:* 4K is only
  available below 60 Hz through the old switch/monitor. Irrelevant once the G6 is in.
- Audio: one sink, HDMI to the monitor (`A409U`); the soundbar hangs off the monitor's analog out.
- Present tools: `xprop`, `xdotool`, `xwininfo`, `gamescopectl`, `cec-ctl`, `ir-keytable`,
  `evtest`, `python3` (with tkinter and GTK3), `podman`, `distrobox`.
- Steam Controller held without pressing anything (2026-11-21): the shell's screensaver ended
  with no button event on Steam's virtual pad. Ignoring the left stick outside the awake state
  fixed it. *Inferred:* a resting thumb on a trackpad (or stick drift) moves the virtual pad's
  stick; don't treat stick input as a deliberate press.

## Open / untested

- Does Steam rewrite `GAMESCOPECTRL_BASELAYER_APPID` on notifications or downloads finishing?
- Remote: wake via a USB IR receiver or CEC; does the remap survive an OS update;
  does the dongle reclaim the remote if both are powered.
- How to give a Wayland window a real app ID with `--expose-wayland`.
- How gamescope's "derived from cgroup/process hierarchy" app-ID lookup is triggered. It
  worked for the Steam-launched shortcut, but not for our own X windows on `:0` or `:1` with
  `SteamLaunch AppId=<id>` in their own or a parent's command line, nor in an
  `app-steam-app<id>-*` scope, nor for a GTK X11 window with `_NET_WM_PID` set.
  *Inferred:* Steam assigns it for programs it launches. For programs we start ourselves,
  `STEAM_GAME` is the only method that works.
- With our shell as a non-Steam shortcut: Steam menu/QAM behavior, downloads, idle/suspend.
