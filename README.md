<div align="center">
  <img src="./assets/OMoutaTweaksIcon.png" alt="OMouta's EM4 Tweaks Logo" width="300" />
</div>

A lightweight launcher for **EMERGENCY 4** that can load optional fixes and
tweaks before the game starts.

Tweaks are packaged as independent hook DLLs that the launcher discovers at
runtime.

## What It Does

Start the game through `OMoutaEM4Tweaks.exe` instead of launching `em4.exe`
directly. The launcher opens a small settings window, loads the enabled tweaks,
then starts EM4.

Current tweak packages:

| Tweak | Description |
| --- | --- |
| Engine Probe | Collects observe-only runtime diagnostics for future tweak development. |

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
; tweak enablement is added here when hook packages are installed
```

Most users can use the launcher window instead of editing this file manually.

## Logs

Logs are stored here:

```text
OMoutaEM4Tweaks\Logs\
```

Each tweak can create its own log file. The launcher itself writes
`Launcher.log`.

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

The engine probe ships like this:

```text
OMoutaEM4Tweaks\Hooks\EngineProbe\
  EngineProbe.dll
  tweak.ini
```

`tweak.ini` is what lets the launcher show the tweak by name and decide which
DLL to inject:

```ini
[Tweak]
id=engine_probe
name=Engine Probe
description=Collects observe-only runtime diagnostics for EM4 engine and tweak development.
version=0.1.0
dll=EngineProbe.dll
config_key=engine_probe
default_enabled=1
log=EngineProbe.log

[Settings]
trace_file_io=EngineProbe|trace_file_io|Trace file opens and reads|bool|1
trace_windows=EngineProbe|trace_windows|Trace game windows|bool|1
trace_opengl=EngineProbe|trace_opengl|Trace OpenGL bootstrap|bool|1
```

The launcher discovers these folders automatically, so adding another packaged
tweak does not require hardcoding its DLL name in the launcher.

Each `[Settings]` entry uses:

```text
id=config_section|config_key|Label|type|default
```

Supported UI types are `text`, `int`, and `bool`. `config_key` is optional; if
it is omitted, the launcher uses the tweak `id` as the key in `[Tweaks]`. The
launcher writes values to the requested config section and key, so
tweak-specific settings do not need to be hardcoded into the launcher UI.

Each built-in hook should live in its own source folder:

```text
src\hooks\<hook-name>\
```

Each hook folder owns its own `CMakeLists.txt`. Add the DLL target there, then
call `omouta_configure_hook_package(...)` so the build places the DLL and
`tweak.ini` under:

```text
OMoutaEM4Tweaks\Hooks\<PackageName>\
```

The top-level hooks CMake file discovers hook folders automatically, so adding
another built-in tweak should not require editing the launcher target.

## Contributing

Contributions are welcome! If you have an idea for a tweak or improvement, feel free to open an issue or submit a pull request.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.
