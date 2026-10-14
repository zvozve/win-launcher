@echo off
REM =====================================================================
REM  build.bat - WinLauncher one-click build  (keep this file ASCII only!)
REM
REM  NOTE: cmd.exe on zh-CN reads .bat as GBK, so a UTF-8 Chinese comment
REM        gets chopped into garbage tokens and the build breaks.
REM        Same class of trap: never put an expanded "(x86)" path inside a
REM        parenthesized if-block -- cmd miscounts the parens and aborts
REM        with "\Microsoft was unexpected at this time". Use goto labels.
REM
REM  Toolchain: MSVC 2022 Build Tools 64bit + Qt 6.8.3 msvc2022_64 + Ninja
REM
REM  Usage:  build.bat            -> Release  (recommended)
REM          build.bat Debug      -> Debug
REM          build.bat both       -> Debug + Release
REM
REM  Steps per config: MSVC x64 env -> cmake configure -> build
REM                    -> packaging\deploy.bat (windeployqt + Everything DLL)
REM
REM  Output: build\<Config>\WinLauncher.exe   (double-clickable)
REM =====================================================================
setlocal EnableExtensions

set "QT_ROOT=C:\Container\System\Library\QT\6.8.3\msvc2022_64"
set "QT_ROOT_FWD=C:/Container/System/Library/QT/6.8.3/msvc2022_64"
set "CMAKE_EXE=C:\Container\System\Library\QT\Tools\CMake_64\bin\cmake.exe"
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set "MSVC_ROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207"
set "SDK_ROOT=C:\Program Files (x86)\Windows Kits\10"
set "SDK_VER=10.0.26100.0"
set "PROJ=%~dp0"

set "WHICH=%~1"
if "%WHICH%"=="" set "WHICH=Release"

echo ============================================================
echo  WinLauncher build  (Qt 6.8.3 msvc2022 64bit)  target=%WHICH%
echo ============================================================

if not exist "%CMAKE_EXE%" goto :no_cmake
if not exist "%QT_ROOT%\lib\cmake\Qt6\Qt6Config.cmake" goto :no_qt

REM --- 1. MSVC x64 environment -----------------------------------------
REM vcvars64 locates the Windows SDK through reg.exe. Where reg.exe is
REM blocked, vcvars64 still returns 0 but rc.exe / mt.exe stay missing --
REM so detect that and assemble the environment by hand instead.
call "%VCVARS%" >nul 2>nul
where rc.exe >nul 2>nul
if errorlevel 1 goto :manual_env
echo [1/4] MSVC x64 environment ready (vcvars64)
goto :env_ready

:manual_env
echo [1/4] vcvars64 gave no Windows SDK - assembling env by hand
set "VCToolsInstallDir=%MSVC_ROOT%\"
set "WindowsSdkDir=%SDK_ROOT%\"
set "WindowsSDKVersion=%SDK_VER%\"
set "PATH=%MSVC_ROOT%\bin\Hostx64\x64;%SDK_ROOT%\bin\%SDK_VER%\x64;%PATH%"
set "INCLUDE=%MSVC_ROOT%\include;%SDK_ROOT%\Include\%SDK_VER%\ucrt;%SDK_ROOT%\Include\%SDK_VER%\shared;%SDK_ROOT%\Include\%SDK_VER%\um;%SDK_ROOT%\Include\%SDK_VER%\winrt"
set "LIB=%MSVC_ROOT%\lib\x64;%SDK_ROOT%\Lib\%SDK_VER%\ucrt\x64;%SDK_ROOT%\Lib\%SDK_VER%\um\x64"

:env_ready
set "PATH=%QT_ROOT%\bin;%PATH%"

if /I "%WHICH%"=="Debug" goto :cfg_debug
if /I "%WHICH%"=="both" goto :cfg_both
goto :cfg_release

:cfg_debug
call :build_one Debug
if errorlevel 1 goto :fail
goto :done

:cfg_release
call :build_one Release
if errorlevel 1 goto :fail
goto :done

:cfg_both
call :build_one Debug
if errorlevel 1 goto :fail
call :build_one Release
if errorlevel 1 goto :fail
goto :done

REM ---------------------------------------------------------------
:build_one
set "CFG=%~1"
set "BUILD=%PROJ%build\%CFG%"

echo.
echo [2/4] Configuring %CFG% ...
"%CMAKE_EXE%" -S "%PROJ%." -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=%CFG% -DCMAKE_PREFIX_PATH="%QT_ROOT_FWD%"
if errorlevel 1 (
    echo [ERROR] cmake configure failed for %CFG%
    echo [ERROR] hint: after editing headers qmltyperegistrar/AUTOMOC can fail
    echo [ERROR]       once on an incremental tree - just run build.bat again.
    exit /b 1
)

echo [3/4] Building %CFG% ...
"%CMAKE_EXE%" --build "%BUILD%"
if errorlevel 1 (
    echo [ERROR] build failed for %CFG%
    exit /b 1
)

echo [4/4] Deploying %CFG% ...
call "%PROJ%packaging\deploy.bat" %CFG%
if errorlevel 1 exit /b 1
goto :eof

:done
echo.
echo ============================================================
echo  BUILD OK
echo    build\Debug\WinLauncher.exe
echo    build\Release\WinLauncher.exe
echo  Run: build.bat ^(Release^) then double-click build\Release\WinLauncher.exe
echo ============================================================
endlocal
exit /b 0

:no_cmake
echo [ERROR] cmake not found: %CMAKE_EXE%
endlocal
exit /b 1

:no_qt
echo [ERROR] Qt6 not found: %QT_ROOT%
endlocal
exit /b 1

:fail
echo [ERROR] build aborted
endlocal
exit /b 1
