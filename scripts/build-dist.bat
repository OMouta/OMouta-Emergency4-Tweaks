@echo off
setlocal

pushd "%~dp0\.." || exit /b 1

call scripts\build.bat
if errorlevel 1 (
    popd
    exit /b 1
)

call :detect_build_dir
if errorlevel 1 (
    popd
    exit /b 1
)

set "DIST_DIR=dist\OMoutaEM4Tweaks"
set "APP_DIR=%DIST_DIR%\OMoutaEM4Tweaks"
set "LOGS_DIR=%APP_DIR%\Logs"

if not exist "%BUILD_DIR%\OMoutaEM4Tweaks.exe" (
    echo Missing launcher: "%BUILD_DIR%\OMoutaEM4Tweaks.exe"
    popd
    exit /b 1
)

if exist dist rmdir /s /q dist

mkdir "%APP_DIR%\Hooks" || exit /b 1
mkdir "%LOGS_DIR%" || exit /b 1

copy /y "%BUILD_DIR%\OMoutaEM4Tweaks.exe" "%DIST_DIR%\" >nul || exit /b 1
if exist "%BUILD_DIR%\OMoutaEM4Tweaks\Hooks" (
    xcopy /e /i /y "%BUILD_DIR%\OMoutaEM4Tweaks\Hooks" "%APP_DIR%\Hooks" >nul || exit /b 1
)
copy /y README.md "%DIST_DIR%\" >nul

(
    echo [Game]
    echo em4_path=em4.exe
    echo.
    echo [Tweaks]
) > "%APP_DIR%\config.ini"

echo Created "%DIST_DIR%"

popd
exit /b 0

:detect_build_dir
if not "%OMOUTA_BUILD_DIR%"=="" (
    set "BUILD_DIR=%OMOUTA_BUILD_DIR%"
    exit /b 0
)

for /f "delims=" %%I in ('powershell -NoProfile -ExecutionPolicy Bypass -Command "$paths = @('build-win32-vs18\Release\OMoutaEM4Tweaks.exe', 'build-win32\Release\OMoutaEM4Tweaks.exe', 'build-win32-vs16\Release\OMoutaEM4Tweaks.exe'); $latest = Get-Item -LiteralPath $paths -ErrorAction SilentlyContinue | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1; if ($latest) { Split-Path -Parent $latest.FullName }"') do (
    set "BUILD_DIR=%%I"
    exit /b 0
)

echo Could not find a Release build output. Set OMOUTA_BUILD_DIR to override.
exit /b 1
