@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Uninstall.ps1"
set "ARE_UNINSTALL_RESULT=%ERRORLEVEL%"
if not "%ARE_UNINSTALL_RESULT%"=="0" pause
exit /b %ARE_UNINSTALL_RESULT%
