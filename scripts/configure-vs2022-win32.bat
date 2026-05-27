@echo off
setlocal

pushd "%~dp0\.." || exit /b 1

cmake --preset vs2022-win32
set EXIT_CODE=%ERRORLEVEL%

popd
exit /b %EXIT_CODE%
