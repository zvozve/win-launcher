@echo off
rem WinLauncher build script: MSVC2022 + Qt6.8.3 + Ninja (single-config)
rem
rem Layout: build\<Config>\WinLauncher.exe  (Config = Debug | Release)
rem Ninja is a single-config generator, so each configuration gets its own
rem build directory; CMAKE_BUILD_TYPE only lives inside that directory.
rem For a single tree with build\Debug + build\Release side by side, switch to
rem "-G \"Ninja Multi-Config\"" and build with --config <Config>.
rem
rem Toolchain env is assembled by hand: vcvars64 calls reg.exe, which the
rem sandbox blocks, and that leaves the Windows SDK paths unset.
rem NOTE: do not wrap paths in "if ( ... )" blocks -- %MSVC_ROOT% expands to
rem "C:\Program Files (x86)\..." and the closing paren terminates the block.
setlocal EnableExtensions

set "QT_ROOT=C:\Container\System\Library\QT\6.8.3\msvc2022_64"
set "CMAKE_EXE=C:\Container\System\Library\QT\Tools\CMake_64\bin\cmake.exe"
set "MSVC_ROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207"
set "SDK_ROOT=C:\Program Files (x86)\Windows Kits\10"
set "SDK_VER=10.0.26100.0"
set "PROJ=%~dp0.."

set "CONFIG=Debug"
if not "%~1"=="" set "CONFIG=%~1"
set "BUILD=%PROJ%\build\%CONFIG%"

if not exist "%CMAKE_EXE%" goto :no_cmake
if not exist "%QT_ROOT%\lib\cmake\Qt6\Qt6Config.cmake" goto :no_qt
if not exist "%MSVC_ROOT%\bin\Hostx64\x64\cl.exe" goto :no_msvc

set "VCToolsInstallDir=%MSVC_ROOT%\"
set "WindowsSdkDir=%SDK_ROOT%\"
set "WindowsSDKVersion=%SDK_VER%\"

set "PATH=%MSVC_ROOT%\bin\Hostx64\x64;%SDK_ROOT%\bin\%SDK_VER%\x64;%QT_ROOT%\bin;%PATH%"
set "INCLUDE=%MSVC_ROOT%\include;%SDK_ROOT%\Include\%SDK_VER%\ucrt;%SDK_ROOT%\Include\%SDK_VER%\shared;%SDK_ROOT%\Include\%SDK_VER%\um;%SDK_ROOT%\Include\%SDK_VER%\winrt"
set "LIB=%MSVC_ROOT%\lib\x64;%SDK_ROOT%\Lib\%SDK_VER%\ucrt\x64;%SDK_ROOT%\Lib\%SDK_VER%\um\x64"

echo [build] config=%CONFIG%  out=%BUILD%

"%CMAKE_EXE%" -S "%PROJ%" -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_PREFIX_PATH="%QT_ROOT%"
if errorlevel 1 goto :fail

"%CMAKE_EXE%" --build "%BUILD%"
if errorlevel 1 goto :fail

rem Everything SDK DLL goes next to the exe so the full-disk search source works.
set "ESDK=%PROJ%\third_party\everything\Everything64.dll"
if not exist "%ESDK%" goto :no_esdk
copy /y "%ESDK%" "%BUILD%\" >nul
echo [build] copied Everything64.dll
goto :built

:no_esdk
echo [build] note: third_party\everything\Everything64.dll missing
echo [build]       run scripts\fetch-everything-sdk.bat to enable the full-disk search source

:built
echo [build] done: %BUILD%\WinLauncher.exe
endlocal
exit /b 0

:no_cmake
echo [build] cmake not found: %CMAKE_EXE%
endlocal
exit /b 1

:no_qt
echo [build] Qt6 not found: %QT_ROOT%
endlocal
exit /b 1

:no_msvc
echo [build] cl.exe not found: %MSVC_ROOT%
endlocal
exit /b 1

:fail
echo [build] FAILED
echo [build] hint: 增量改了头文件后 qmltyperegistrar 偶尔会先失败一次，直接重跑一次 build.bat 即可
endlocal
exit /b 1
