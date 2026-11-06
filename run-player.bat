@echo off
cd /d "%~dp0"

if not exist build-editor\CMakeCache.txt (
    echo build-editor not found, configuring...
    where ninja >nul 2>nul
    if errorlevel 1 (
        cmake -S . -B build-editor -G "MinGW Makefiles"
    ) else (
        cmake -S . -B build-editor -G Ninja
    )
    if errorlevel 1 goto fail
)

cmake --build build-editor --target Mirage-Player -j8
if errorlevel 1 goto fail

rem Runs from the project root so scripts\ and font\ resolve.
build-player\Mirage-Player.exe %*
pause
exit /b 0

:fail
echo.
echo Player build FAILED.
pause
exit /b 1
