<div align="center">
  <img src="./assets/OMoutaTweaksIcon.png" alt="OMouta's EM4 Tweaks Logo" width="300" />
</div>

A lightweight launcher for **EMERGENCY 4** that can load optional fixes and
tweaks before the game starts.

The current release includes one tweak: a borderless window fix for EM4's
OpenGL renderer.

## What It Does

Start the game through `OMoutaEM4Tweaks.exe` instead of launching `em4.exe`
directly. The launcher opens a small settings window, loads the enabled tweaks,
then starts EM4.

Current tweaks:

| Tweak | Description |
| --- | --- |
| Borderless Window Fix | Runs EM4 in a 1920x1080 borderless window and prevents several fullscreen-style behaviors. |

## Installation

Extract the release next to `em4.exe`.

Launch the game with:

```text
OMoutaEM4Tweaks.exe
```

## Launcher

When the launcher opens, it shows a compact countdown screen. If you do
nothing, the game launches after five seconds. Click **Settings** during the
countdown to configure the launcher.

The settings window has three tabs:

| Tab | Purpose |
| --- | --- |
| General | Launcher-level settings such as the `em4.exe` path. |
| Tweaks | Enable installed tweaks and edit each tweak's manifest-defined settings. |
| About | Basic package and launcher information. |

Settings are applied only when you click **Save & Launch** or **Save & Exit**.
Closing the window or clicking **Exit** discards unsaved changes.

## Settings

Settings are stored here:

```text
OMoutaEM4Tweaks\config.ini
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

Most users can use the launcher window instead of editing this file manually.

## Logs

Logs are stored here:

```text
OMoutaEM4Tweaks\Logs\
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
Visual Studio 2019 or newer
CMake
Win32/x86 build tools
```

Build:

```bat
scripts\build.bat
```

Create a release folder:

```bat
scripts\build-dist.bat
```

Create a release archive:

```bat
scripts\build-package.bat
```

The release output appears here:

```text
dist\OMoutaEM4Tweaks\
dist\OMoutaEM4Tweaks.zip
```

The build script auto-detects an existing build folder or installed Visual
Studio version and uses the matching CMake preset. To override detection, set:

```bat
set OMOUTA_CMAKE_CONFIGURE_PRESET=vs2022-win32
set OMOUTA_CMAKE_BUILD_PRESET=release
```

## Adding Tweaks

Tweaks are loaded as small packages. A package is a folder under:

```text
OMoutaEM4Tweaks\Hooks\
```

The borderless fix ships like this:

```text
OMoutaEM4Tweaks\Hooks\BorderlessWindowFix\
  BorderlessWindowFix.dll
  tweak.ini
```

`tweak.ini` is what lets the launcher show the tweak by name and decide which
DLL to inject:

```ini
[Tweak]
id=borderless_window
name=Borderless Window Fix
description=Runs EM4 in a borderless window and reduces fullscreen-style focus behavior.
version=1.0.0
dll=BorderlessWindowFix.dll
config_key=borderless_window
default_enabled=1
log=BorderlessWindowFix.log

[Settings]
x=BorderlessWindow|x|Window X|int|0
y=BorderlessWindow|y|Window Y|int|0
width=BorderlessWindow|width|Window Width|int|1920
height=BorderlessWindow|height|Window Height|int|1080
keep_visible_on_focus_loss=BorderlessWindow|keep_visible_on_focus_loss|Keep visible when focus changes|bool|1
```

The launcher discovers these folders automatically, so adding another packaged
tweak does not require hardcoding its DLL name in the launcher.

Each `[Settings]` entry uses:

```text
id=config_section|config_key|Label|type|default
```

Supported UI types are `text`, `int`, and `bool`. The launcher writes values to
the requested config section and key, so tweak-specific settings do not need to
be hardcoded into the launcher UI.

Source code for the current tweak lives here:

```text
src\hooks\borderless\
```

New built-in tweaks should follow the same package shape in the release folder.

## Contributing

Contributions are welcome! If you have an idea for a tweak or improvement, feel free to open an issue or submit a pull request.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.
