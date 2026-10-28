@echo off
cd /d "%~dp0"

if not exist build-player\CMakeCache.txt (
    echo build-player not found, configuring...
    cmake -S player -B build-player -G "MinGW Makefiles"
    if errorlevel 1 goto fail
)

cmake --build build-player -j8
if errorlevel 1 goto fail

rem Runs from the project root so scripts\ and font\ resolve.
rem Add:  --scene scenes\test.json   once Phase 3 reads it.
build-player\Mirage-Player.exe %*
pause
exit /b 0

:fail
echo.
echo Player build FAILED.
pause
exit /b 1
