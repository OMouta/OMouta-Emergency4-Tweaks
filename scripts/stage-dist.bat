@echo off
setlocal

pushd "%~dp0\.." || exit /b 1

set "BUILD_DIR=%~1"
if "%BUILD_DIR%"=="" set "BUILD_DIR=build-win32\Release"

set "DIST_DIR=dist\OMoutasEM4Tweaks"
set "APP_DIR=%DIST_DIR%\OMoutasEM4Tweaks"
set "HOOKS_DIR=%APP_DIR%\Hooks"
set "BORDERLESS_DIR=%HOOKS_DIR%\BorderlessWindowFix"
set "LOGS_DIR=%APP_DIR%\Logs"

if not exist "%BUILD_DIR%\OMoutasEM4Tweaks.exe" (
    echo Missing launcher: "%BUILD_DIR%\OMoutasEM4Tweaks.exe"
    popd
    exit /b 1
)

if not exist "%BUILD_DIR%\OMoutasEM4Tweaks\Hooks\BorderlessWindowFix\BorderlessWindowFix.dll" (
    echo Missing hook DLL: "%BUILD_DIR%\OMoutasEM4Tweaks\Hooks\BorderlessWindowFix\BorderlessWindowFix.dll"
    popd
    exit /b 1
)

if not exist "%BUILD_DIR%\OMoutasEM4Tweaks\Hooks\BorderlessWindowFix\tweak.ini" (
    echo Missing tweak metadata: "%BUILD_DIR%\OMoutasEM4Tweaks\Hooks\BorderlessWindowFix\tweak.ini"
    popd
    exit /b 1
)

if exist dist rmdir /s /q dist

mkdir "%BORDERLESS_DIR%" || exit /b 1
mkdir "%LOGS_DIR%" || exit /b 1

copy /y "%BUILD_DIR%\OMoutasEM4Tweaks.exe" "%DIST_DIR%\" >nul || exit /b 1
copy /y "%BUILD_DIR%\OMoutasEM4Tweaks\Hooks\BorderlessWindowFix\BorderlessWindowFix.dll" "%BORDERLESS_DIR%\" >nul || exit /b 1
copy /y "%BUILD_DIR%\OMoutasEM4Tweaks\Hooks\BorderlessWindowFix\tweak.ini" "%BORDERLESS_DIR%\" >nul || exit /b 1
copy /y README.md "%DIST_DIR%\" >nul

(
    echo [Game]
    echo em4_path=em4.exe
    echo.
    echo [Tweaks]
    echo borderless_window=1
    echo.
    echo [BorderlessWindow]
    echo x=0
    echo y=0
    echo width=1920
    echo height=1080
    echo keep_visible_on_focus_loss=1
) > "%APP_DIR%\config.ini"

echo Created "%DIST_DIR%"

popd
exit /b 0
