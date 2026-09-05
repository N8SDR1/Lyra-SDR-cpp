@echo off
REM ============================================================
REM  Lyra - Full Settings Reset (unlock a Lyra that won't launch)
REM ------------------------------------------------------------
REM  Run this if Lyra WILL NOT LAUNCH / crashes on startup and a
REM  reinstall did NOT help.  A reinstall can't fix this because
REM  Windows uninstall does NOT remove Lyra's saved settings -
REM  they live in the registry under the current user and survive
REM  every uninstall/reinstall.  A bad saved setting (e.g. an
REM  audio device that is no longer present, a graphics backend,
REM  or a window layout) can wedge the launch, and only clearing
REM  those settings fixes it.
REM
REM  This tool:
REM    1. Closes any running/frozen Lyra.
REM    2. BACKS UP your current Lyra settings to your Desktop
REM       (Lyra-settings-backup.reg) so nothing is lost for good.
REM    3. Clears ALL Lyra settings (returns Lyra to factory
REM       defaults - you'll re-enter callsign, audio, layout, etc.).
REM    4. Relaunches Lyra.
REM
REM  To restore the old settings later: double-click the backup
REM  .reg file on your Desktop.
REM ============================================================

echo.
echo  Lyra - Full Settings Reset
echo  --------------------------

echo  Closing any running Lyra instance...
taskkill /IM lyra.exe /F >nul 2>&1

echo  Backing up your current Lyra settings to the Desktop...
reg export "HKCU\Software\N8SDR\Lyra-cpp" "%USERPROFILE%\Desktop\Lyra-settings-backup.reg" /y >nul 2>&1
if exist "%USERPROFILE%\Desktop\Lyra-settings-backup.reg" (
    echo    Saved: "%USERPROFILE%\Desktop\Lyra-settings-backup.reg"
) else (
    echo    ^(No existing settings found to back up - continuing.^)
)

echo  Clearing all Lyra settings (factory reset)...
reg delete "HKCU\Software\N8SDR\Lyra-cpp" /f >nul 2>&1
echo  Done - Lyra has been reset to defaults.
echo.

set "LYRA_EXE=%ProgramFiles%\Lyra\lyra.exe"
if exist "%LYRA_EXE%" (
    echo  Relaunching Lyra...
    start "" "%LYRA_EXE%"
    echo  Lyra should now open at its default settings.
) else (
    echo  Could not find Lyra at:
    echo    "%LYRA_EXE%"
    echo  Please launch Lyra manually.
)

echo.
echo  You can close this window.
pause >nul
