@echo off
REM =====================================================================
REM  fetch-everything-sdk.bat - get Everything64.dll for the full-disk search
REM
REM  The Everything installer does NOT ship the SDK DLL; it only comes with
REM  the separate Everything SDK download. This script pulls
REM  https://www.voidtools.com/Everything-SDK.zip and extracts
REM  dll\Everything64.dll into third_party\everything\, from where
REM  packaging\deploy.bat copies it next to the exe.
REM
REM  The DLL itself is git-ignored (binary, reproducible from this script).
REM
REM  Only uses tools shipped with Windows 10 1803+: curl.exe and tar.exe.
REM =====================================================================
setlocal EnableExtensions

set "DEST=%~dp0..\third_party\everything"
set "URL=https://www.voidtools.com/Everything-SDK.zip"
set "ZIP=%TEMP%\Everything-SDK.zip"
set "TMPD=%TEMP%\everything-sdk-extract"

if not exist "%DEST%" mkdir "%DEST%"

where curl >nul 2>nul
if errorlevel 1 goto :no_curl
where tar >nul 2>nul
if errorlevel 1 goto :no_tar

echo [sdk] downloading %URL%
curl -L --fail --silent --show-error -o "%ZIP%" "%URL%"
if errorlevel 1 goto :dl_fail

if exist "%TMPD%" rmdir /s /q "%TMPD%"
mkdir "%TMPD%"
tar -xf "%ZIP%" -C "%TMPD%" "dll/Everything64.dll"
if errorlevel 1 goto :unzip_fail

copy /y "%TMPD%\dll\Everything64.dll" "%DEST%\Everything64.dll" >nul
if errorlevel 1 goto :copy_fail

rmdir /s /q "%TMPD%" >nul 2>nul
del "%ZIP%" >nul 2>nul

echo [sdk] ok: %DEST%\Everything64.dll
echo [sdk] next: build.bat   (it runs windeployqt and copies this DLL)
endlocal
exit /b 0

:no_curl
echo [sdk] curl.exe not found (needs Windows 10 1803+)
endlocal
exit /b 1

:no_tar
echo [sdk] tar.exe not found (needs Windows 10 1803+)
endlocal
exit /b 1

:dl_fail
echo [sdk] download failed: %URL%
echo [sdk] download Everything-SDK.zip manually and put dll\Everything64.dll in %DEST%
endlocal
exit /b 1

:unzip_fail
echo [sdk] extract failed
endlocal
exit /b 1

:copy_fail
echo [sdk] copy failed -> %DEST%
endlocal
exit /b 1
