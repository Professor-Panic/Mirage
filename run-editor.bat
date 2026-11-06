@echo off
cd /d "%~dp0"

if not exist build-editor\CMakeCache.txt (
    echo build-editor not found, configuring...
    rem Ninja is much faster than MinGW Makefiles when installed.
    where ninja >nul 2>nul
    if errorlevel 1 (
        cmake -S . -B build-editor -G "MinGW Makefiles"
    ) else (
        cmake -S . -B build-editor -G Ninja
    )
    if errorlevel 1 goto fail
)

rem Builds the editor and the player (they share one compiled core).
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
