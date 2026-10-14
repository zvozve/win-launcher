@echo off
rem Deploy the Qt runtime next to WinLauncher.exe so it can be launched by
rem double-clicking, without putting Qt's bin on PATH.
rem
rem Usage: deploy.bat [Debug|Release]   (default Debug)
rem
rem -release / -debug: windeployqt copies the matching Qt DLLs. A mix of
rem release and debug DLLs will not load, so match the build configuration.
rem --qmldir: the QML sources, so windeployqt picks up the QtQuick imports.
setlocal EnableExtensions

set "QT_ROOT=C:\Container\System\Library\QT\6.8.3\msvc2022_64"
set "PROJ=%~dp0.."

set "CONFIG=Debug"
if not "%~1"=="" set "CONFIG=%~1"
set "BUILD=%PROJ%\build\%CONFIG%"
set "EXE=%BUILD%\WinLauncher.exe"
set "WDFLAG=--debug"
if /I "%CONFIG%"=="Release" set "WDFLAG=--release"

if not exist "%EXE%" goto :no_exe
if not exist "%QT_ROOT%\bin\windeployqt.exe" goto :no_wdq

set "PATH=%QT_ROOT%\bin;%PATH%"

echo [deploy] windeployqt %WDFLAG%  ->  %BUILD%
"%QT_ROOT%\bin\windeployqt.exe" %WDFLAG% --qmldir "%PROJ%\qml" "%EXE%"
if errorlevel 1 goto :fail

rem windeployqt does not know about the Everything SDK DLL.
set "ESDK=%PROJ%\third_party\everything\Everything64.dll"
if not exist "%ESDK%" goto :built
copy /y "%ESDK%" "%BUILD%\" >nul
echo [deploy] copied Everything64.dll
goto :built

:built
echo [deploy] done: %EXE%
echo [deploy] now it can be started directly (Qt DLLs are next to the exe)
endlocal
exit /b 0

:no_exe
echo [deploy] not built yet: %EXE%
echo [deploy] run: scripts\build.bat %CONFIG%
endlocal
exit /b 1

:no_wdq
echo [deploy] windeployqt not found: %QT_ROOT%\bin\windeployqt.exe
endlocal
exit /b 1

:fail
echo [deploy] FAILED
endlocal
exit /b 1
