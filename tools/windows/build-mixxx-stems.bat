@echo off
REM ---------------------------------------------------------------------------
REM Thin wrapper so the build can be started by double-clicking or from cmd.
REM Everything is implemented in build-mixxx-stems.ps1 next to this file.
REM
REM Usage:
REM   build-mixxx-stems.bat x64
REM   build-mixxx-stems.bat arm64 -BuildEnvBase C:\mixxx-buildenv -Zip
REM   build-mixxx-stems.bat            (defaults to the host architecture)
REM
REM The .ps1 is written for Windows PowerShell 5.1, which is present on every
REM Windows 10/11 install, so no extra PowerShell installation is required.
REM ---------------------------------------------------------------------------
setlocal

set "PS_EXE=powershell"
where pwsh.exe >nul 2>nul
if %ERRORLEVEL% EQU 0 set "PS_EXE=pwsh"

"%PS_EXE%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-mixxx-stems.ps1" %*
set "RC=%ERRORLEVEL%"

if not "%RC%"=="0" (
    echo.
    echo build-mixxx-stems.ps1 exited with code %RC%.
)

endlocal & exit /b %RC%
