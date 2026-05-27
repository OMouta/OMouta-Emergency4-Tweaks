@echo off
setlocal

pushd "%~dp0\.." || exit /b 1

call scripts\rebuild-release-vs2026-local.bat
if errorlevel 1 (
    popd
    exit /b 1
)

call scripts\stage-dist.bat build-win32-vs18\Release
set EXIT_CODE=%ERRORLEVEL%

popd
exit /b %EXIT_CODE%
