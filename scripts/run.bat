@echo off
rem Launch WinLauncher with the Qt runtime on PATH.
rem Usage: run.bat [--trace] [--autostart]
setlocal EnableExtensions

set "QT_ROOT=C:/Container/System/Library/QT/6.8.3/msvc2022_64"
set "EXE=%~dp0..\build\WinLauncher.exe"

if not exist "%EXE%" goto :no_exe

set "PATH=%QT_ROOT%\bin;%PATH%"
start "" "%EXE%" %*
exit /b 0

:no_exe
echo [run] not built yet: %EXE%
echo [run] run scripts\build.bat first
endlocal
exit /b 1
