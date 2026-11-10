@echo off
rem KomaPaint - register .koma file association (per user)
rem
rem Double-clicking a .koma file will open it in this KomaPaint build.
rem No administrator rights are needed; the registration is written under
rem HKEY_CURRENT_USER\Software\Classes only.
rem
rem Usage:
rem   Register_koma_file_association.cmd             register
rem   Register_koma_file_association.cmd --remove    unregister

setlocal
set "EXE=%~dp0komapaint.exe"
if not exist "%EXE%" (
    echo ERROR: komapaint.exe not found next to this script. 1>&2
    exit /b 1
)

if /I "%~1"=="--remove" (
    "%EXE%" --unassociate
    exit /b %ERRORLEVEL%
)
if not "%~1"=="" (
    echo Usage: %~nx0 [--remove] 1>&2
    exit /b 2
)

"%EXE%" --associate
exit /b %ERRORLEVEL%
