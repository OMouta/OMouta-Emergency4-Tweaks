@echo off
setlocal

pushd "%~dp0\.." || exit /b 1

call :detect_config
if errorlevel 1 (
    popd
    exit /b 1
)

echo Using CMake configure preset "%CONFIGURE_PRESET%" and build preset "%BUILD_PRESET%".

cmake --preset "%CONFIGURE_PRESET%"
if errorlevel 1 (
    popd
    exit /b 1
)

cmake --build --preset "%BUILD_PRESET%"
set "EXIT_CODE=%ERRORLEVEL%"

popd
exit /b %EXIT_CODE%

:detect_config
call :find_vswhere
call :detect_visual_studio

if not "%OMOUTA_CMAKE_CONFIGURE_PRESET%"=="" (
    set "CONFIGURE_PRESET=%OMOUTA_CMAKE_CONFIGURE_PRESET%"
    if "%OMOUTA_CMAKE_BUILD_PRESET%"=="" (
        echo OMOUTA_CMAKE_BUILD_PRESET must be set when OMOUTA_CMAKE_CONFIGURE_PRESET is set.
        exit /b 1
    )
    set "BUILD_PRESET=%OMOUTA_CMAKE_BUILD_PRESET%"
    exit /b 0
)

if exist "build-win32-vs18\CMakeCache.txt" if "%HAS_VS18%"=="1" (
    set "CONFIGURE_PRESET=vs2026-win32-local"
    set "BUILD_PRESET=release-vs2026-local"
    exit /b 0
)

if exist "build-win32\CMakeCache.txt" if "%HAS_VS17%"=="1" (
    set "CONFIGURE_PRESET=vs2022-win32"
    set "BUILD_PRESET=release"
    exit /b 0
)

if exist "build-win32-vs16\CMakeCache.txt" if "%HAS_VS16%"=="1" (
    set "CONFIGURE_PRESET=vs2019-win32"
    set "BUILD_PRESET=release-vs2019"
    exit /b 0
)

if "%HAS_VS18%"=="1" (
    set "CONFIGURE_PRESET=vs2026-win32-local"
    set "BUILD_PRESET=release-vs2026-local"
    exit /b 0
)

if "%HAS_VS17%"=="1" (
    set "CONFIGURE_PRESET=vs2022-win32"
    set "BUILD_PRESET=release"
    exit /b 0
)

if "%HAS_VS16%"=="1" (
    set "CONFIGURE_PRESET=vs2019-win32"
    set "BUILD_PRESET=release-vs2019"
    exit /b 0
)

echo Could not find Visual Studio 2026, 2022, or 2019 Build Tools with MSBuild.
echo Install Visual Studio with Win32 C++ build tools, or set OMOUTA_CMAKE_CONFIGURE_PRESET and OMOUTA_CMAKE_BUILD_PRESET.
exit /b 1

:detect_visual_studio
set "HAS_VS18=0"
set "HAS_VS17=0"
set "HAS_VS16=0"

if "%VSWHERE%"=="" exit /b 0

for /f "delims=" %%I in ('"%VSWHERE%" -version [18.0^,19.0^) -products * -requires Microsoft.Component.MSBuild -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul') do (
    set "HAS_VS18=1"
)

for /f "delims=" %%I in ('"%VSWHERE%" -version [17.0^,18.0^) -products * -requires Microsoft.Component.MSBuild -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul') do (
    set "HAS_VS17=1"
)

for /f "delims=" %%I in ('"%VSWHERE%" -version [16.0^,17.0^) -products * -requires Microsoft.Component.MSBuild -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul') do (
    set "HAS_VS16=1"
)

exit /b 0

:find_vswhere
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" exit /b 0

set "VSWHERE="
for /f "delims=" %%I in ('where vswhere.exe 2^>nul') do (
    set "VSWHERE=%%I"
    exit /b 0
)
exit /b 0
