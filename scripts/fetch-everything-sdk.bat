@echo off
rem Download the Everything SDK and extract dll\Everything64.dll into
rem third_party\everything\ so that scripts\build.bat can copy it next to
rem the executable and WinLauncher's "全盘" search source can work.
rem
rem Uses only tools shipped with Windows 10+: curl.exe and tar.exe (bsdtar).
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
echo [sdk] next: scripts\build.bat Debug   (它会自动拷到 build\Debug\)
endlocal
exit /b 0

:no_curl
echo [sdk] curl.exe not found (需要 Windows 10 1803+ 自带的 curl)
endlocal
exit /b 1

:no_tar
echo [sdk] tar.exe not found (需要 Windows 10 1803+ 自带的 bsdtar)
endlocal
exit /b 1

:dl_fail
echo [sdk] download failed: %URL%
echo [sdk] 可手动下载 Everything-SDK.zip 并解出 dll\Everything64.dll 放到 %DEST%
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
