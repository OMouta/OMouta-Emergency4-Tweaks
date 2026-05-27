@echo off
setlocal

pushd "%~dp0\.." || exit /b 1

cmake --preset vs2026-win32-local
if errorlevel 1 (
    popd
    exit /b 1
)

cmake --build --preset release-vs2026-local
set EXIT_CODE=%ERRORLEVEL%

popd
exit /b %EXIT_CODE%
