# OMouta's EM4 Tweaks

A lightweight launcher for **EMERGENCY 4** that can load optional fixes and
tweaks before the game starts.

The current release includes one tweak: a borderless window fix for EM4's
OpenGL renderer.

## What It Does

Start the game through `OMoutasEM4Tweaks.exe` instead of launching `em4.exe`
directly. The launcher shows a short countdown, loads the enabled tweaks, then
starts EM4.

Current tweaks:

| Tweak | Description |
| --- | --- |
| Borderless Window Fix | Runs EM4 in a 1920x1080 borderless window and prevents several fullscreen-style behaviors. |

## Installation

Extract the release next to `em4.exe`.

Launch the game with:

```text
OMoutasEM4Tweaks.exe
```

## Launcher

When the launcher opens, it waits 5 seconds before starting the game.

```text
L = launch now
E = edit settings
C = cancel
```

If you do nothing, it launches automatically.

## Settings

Settings are stored here:

```text
OMoutasEM4Tweaks\config.ini
```

Example:

```ini
[Game]
em4_path=em4.exe

[Tweaks]
borderless_window=1

[BorderlessWindow]
x=0
y=0
width=1920
height=1080
keep_visible_on_focus_loss=1
```

Most users can use the launcher menu instead of editing this file manually.

## Logs

Logs are stored here:

```text
OMoutasEM4Tweaks\Logs\
```

Current log files:

```text
Launcher.log
BorderlessWindowFix.log
```

If something breaks, these files are the first place to check.

## Building From Source

Requirements:

```text
Windows
Visual Studio 2022
CMake
Win32/x86 build tools
```

Build:

```bat
scripts\rebuild-release.bat
```

Create a release folder:

```bat
scripts\package-release.bat
```

The ready-to-zip output appears here:

```text
dist\OMoutasEM4Tweaks\
```

For this development machine only, there are VS2026 helper scripts:

```bat
scripts\rebuild-release-vs2026-local.bat
scripts\package-release-vs2026-local.bat
```

## Adding Tweaks

Tweaks are separate DLL modules under `src\hooks`.

For example, the current borderless tweak lives here:

```text
src\hooks\borderless\
```

A new tweak can follow the same pattern:

```text
src\hooks\my-tweak\
  HookEntry.cpp
  MyTweak.h
  MyTweak.cpp
```

Then add it to `CMakeLists.txt`, add a config toggle, and have the launcher load
the DLL when that toggle is enabled.

## Contributing

Contributions are welcome! If you have an idea for a tweak or improvement, feel free to open an issue or submit a pull request.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.
