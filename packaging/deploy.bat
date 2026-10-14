@echo off
REM =====================================================================
REM  deploy.bat - put the Qt runtime next to WinLauncher.exe (windeployqt)
REM
REM  Usage:  packaging\deploy.bat [Debug|Release]     (default Debug)
REM
REM  Why: Qt ships as DLLs, the exe does not embed the runtime. Without
REM  this step a double-click fails with "Qt6Cored.dll was not found".
REM
REM  Notes:
REM   * --qmldir is REQUIRED, otherwise windeployqt does not scan the QML
REM     imports and the app dies with a silent QML load failure.
REM   * Debug and Release Qt DLLs must not be mixed (Qt6Cored.dll vs
REM     Qt6Core.dll), so pass the matching --debug / --release flag.
REM   * windeployqt does not know about the Everything SDK DLL; we copy it
REM     ourselves.
REM =====================================================================
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
"%QT_ROOT%\bin\windeployqt.exe" %WDFLAG% --no-translations --qmldir "%PROJ%\ui" "%EXE%"
if errorlevel 1 goto :fail

set "ESDK=%PROJ%\third_party\everything\Everything64.dll"
if not exist "%ESDK%" goto :no_esdk
copy /y "%ESDK%" "%BUILD%\" >nul
echo [deploy] copied Everything64.dll
goto :done

:no_esdk
echo [deploy] note: third_party\everything\Everything64.dll missing
echo [deploy]       run packaging\fetch-everything-sdk.bat for the full-disk search source

:done
echo [deploy] done: %EXE%
echo [deploy] now it can be started directly (Qt DLLs are next to the exe)
endlocal
exit /b 0

:no_exe
echo [deploy] not built yet: %EXE%
echo [deploy] run: build.bat %CONFIG%
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
