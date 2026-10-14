@echo off
rem Launch WinLauncher with the Qt runtime on PATH.
rem Usage: run.bat [Debug|Release] [extra args...]
setlocal EnableExtensions

set "QT_ROOT=C:\Container\System\Library\QT\6.8.3\msvc2022_64"

set "CONFIG=Debug"
if not "%~1"=="" set "CONFIG=%~1"
if /I "%CONFIG%"=="Debug"   goto :config_ok
if /I "%CONFIG%"=="Release" goto :config_ok
rem 第一参数不是配置名：按默认配置跑，并把它当作程序参数
set "CONFIG=Debug"
set "EXTRA=%*"
goto :launch

:config_ok
shift
set "EXTRA=%*"

:launch
set "EXE=%~dp0..\build\%CONFIG%\WinLauncher.exe"
if not exist "%EXE%" goto :no_exe

set "PATH=%QT_ROOT%\bin;%PATH%"
echo [run] %EXE% %EXTRA%
start "" "%EXE%" %EXTRA%
exit /b 0

:no_exe
echo [run] not built yet: %EXE%
echo [run] run: scripts\build.bat %CONFIG%
endlocal
exit /b 1
