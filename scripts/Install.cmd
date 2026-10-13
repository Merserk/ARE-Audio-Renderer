@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install.ps1" %*
set "ARE_INSTALL_RESULT=%ERRORLEVEL%"
if not "%ARE_INSTALL_RESULT%"=="0" pause
exit /b %ARE_INSTALL_RESULT%
