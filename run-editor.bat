@echo off
cd /d "%~dp0"

if not exist build-editor\CMakeCache.txt (
    echo build-editor not found, configuring...
    cmake -S . -B build-editor -G "MinGW Makefiles"
    if errorlevel 1 goto fail
)

cmake --build build-editor -j8
if errorlevel 1 goto fail

cmake --build build-editor --target run
pause
exit /b 0

:fail
echo.
echo Editor build FAILED.
pause
exit /b 1
