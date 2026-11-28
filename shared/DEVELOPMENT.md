# shared: development

Code used by more than one program of this project: the shell (`../shell`) and the
programs it shows that are our own (`../apps/webapp`). Programs built from someone else's
source (Jellyfin, Eden) don't use it. Each folder is one QML module, built as a static
library and linked into each program that uses it, laid out like the shell's topics
(`../shell/DEVELOPMENT.md`, Layout). Nothing here knows about any one program.

| Folder | Module | What |
|---|---|---|
| `input/` | `TvShared.Input` | `GamepadInput`: Steam's virtual pad (SDL3) as key events for a window, so the remote, a keyboard and the controller arrive the same way (`../shell/DEVELOPMENT.md`, "Controller and keys"). `FreshKeys`: from the moment a program is shown, nothing until a key is freshly pressed |

A program on screen that isn't the shell reads the controller itself: the shell reads it
only while it is on screen, because the same virtual pad feeds games. So the web app has
its own `GamepadInput`, enabled while gamescope shows its window. `FreshKeys` is for a
program that is handed the screen while a key is still held (the OK or YouTube key that
opened it): that key's repeats and release would otherwise act in it.

## Building

A program adds the folder with `add_subdirectory(<path to shared> shared)`, links the
modules it uses (`tvshared_input` and its plugin, `tvshared_inputplugin`) and imports the
plugin in its `main.cpp` (`Q_IMPORT_QML_PLUGIN(TvShared_InputPlugin)`). Its `build.sh`
mounts the whole repository read-only and points CMake at its own folder in it, so
`../shared` is there in the container. Logging category: `tv.gamepad`.
