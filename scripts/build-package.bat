@echo off
setlocal

pushd "%~dp0\.." || exit /b 1

call scripts\build-dist.bat
if errorlevel 1 (
    popd
    exit /b 1
)

set "PACKAGE_PATH=dist\OMoutaEM4Tweaks.zip"
if exist "%PACKAGE_PATH%" del /q "%PACKAGE_PATH%"

powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -LiteralPath 'dist\OMoutaEM4Tweaks' -DestinationPath '%PACKAGE_PATH%' -Force"
if errorlevel 1 (
    popd
    exit /b 1
)

echo Created "%PACKAGE_PATH%"

popd
exit /b 0
