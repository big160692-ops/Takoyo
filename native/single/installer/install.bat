@echo off
setlocal
set "TMPDIR=%TEMP%\TakoyoInstall_1795577389"
mkdir "%TMPDIR%" 2>nul
powershell -command "Expand-Archive -Force -LiteralPath '%~dp0Takoyo.exe' -DestinationPath '%TMPDIR%'"
powershell -ExecutionPolicy Bypass -File "%TMPDIR%\install.ps1"
rmdir /s /q "%TMPDIR%" 2>nul
