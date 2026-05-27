@echo off
setlocal

pushd "%~dp0\.." || exit /b 1

if exist build-win32 (
    rmdir /s /q build-win32
)

popd
exit /b 0
