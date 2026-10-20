cmake_minimum_required(VERSION 3.16)
project(WinLauncher VERSION 0.2.0 LANGUAGES CXX)

# exe 文件图标 + 版本信息走 Win32 资源(.rc), 需要显式启用 RC 语言;
# 不启用时 .rc 会被 CMake 当成"未知扩展名"静默忽略, exe 就没有图标。
#
# .rc 由 resources/winlauncher.rc.in 生成, 而不是直接用源文件 —— 这样
# VERSIONINFO 里的版本号能跟着 project(VERSION) 走(单一真相源), 不必在
# 两个地方手工同步。
if(WIN32)
    enable_language(RC)
    # rc.exe 的 ICON 语句要真实存在的路径; 生成物在 build 目录里,
    # 所以图标得给绝对路径。
    set(WINLAUNCHER_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/winlauncher.ico")
    # VERSIONINFO 要逐段数字, 从 project(VERSION) 拆出来
    set(VER_MAJOR ${PROJECT_VERSION_MAJOR})
    set(VER_MINOR ${PROJECT_VERSION_MINOR})
    set(VER_PATCH ${PROJECT_VERSION_PATCH})
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/winlauncher.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/winlauncher.rc"
        @ONLY)
endif()

# ======================================================================
#  WinLauncher - 仿 macOS 26/27 的弹出式启动台
#
#  构建 (MSVC 2022 64bit + Qt 6.8.3 msvc2022_64):
#    build.bat               Release + windeployqt (可双击运行的目录)
#    build.bat Debug         Debug 同上
#    packaging\deploy.bat    只重跑 windeployqt
#
#  手动构建示例:
#    cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release ^
#          -DCMAKE_PREFIX_PATH=C:/Container/System/Library/QT/6.8.3/msvc2022_64
#    cmake --build build/Release
#
#  ★ 生成器选择: 用 Ninja (单配置) + 每配置一个构建目录, 产物落在
#    build\Debug\ 与 build\Release\, 与 Visual Studio 生成器的目录布局一致。
#    想换成 VS 生成器就用 -G "Visual Studio 17 2022" -A x64 (注意换生成器
#    必须删掉 build\ 重配, CMake 不会在同一条树上改生成器)。
# ======================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# ★ AUTORCC 必须开。qrc 里的 ui/*.qml 只有被 AUTORCC 解析才会成为 rcc 的
#   依赖 —— 否则改完 QML 编译"成功"跑出来还是旧界面, 极难排查。
set(CMAKE_AUTORCC ON)

# Qt 安装目录(默认构建机路径; 可用 -DWINLAUNCHER_QT_DIR=... 覆盖)
if(NOT DEFINED WINLAUNCHER_QT_DIR)
    set(WINLAUNCHER_QT_DIR "C:/Container/System/Library/QT/6.8.3/msvc2022_64")
endif()
list(APPEND CMAKE_PREFIX_PATH "${WINLAUNCHER_QT_DIR}")

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core Gui Qml Quick QuickControls2
    # Widgets: 托盘图标/菜单走 QSystemTrayIcon + QMenu, 在 Qt 6.8 里它们
    # 仍然住在 QtWidgets, 去掉就会链接期 undefined symbol。
    Widgets
)

# ======================================================================
#  可执行目标
# ======================================================================
add_executable(WinLauncher WIN32
    src/main.cpp
    src/AppCategory.cpp
    src/AppItem.cpp
    src/AppScanner.cpp
    src/AppUninstaller.cpp
    src/BackdropBlur.cpp
    src/BackdropProvider.cpp
    src/Config.cpp
    src/Controller.cpp
    src/DebugTrace.cpp
    src/EverythingSearch.cpp
    src/GroupTile.cpp
    src/HotkeyManager.cpp
    src/IconProvider.cpp
    src/Launcher.cpp
    src/LauncherWindow.cpp
    src/SearchQuery.cpp
    src/ShortcutResolver.cpp
    src/UiFont.cpp
    src/WindowEffects.cpp
    # 头文件必须显式列出: AUTOMOC 只扫描 target sources 里的文件, 放在 inc/
    # 下不列出来的话 moc 不会被触发, 表现为"无法解析的外部符号 staticMetaObject"。
    inc/AppCategory.h
    inc/AppItem.h
    inc/AppScanner.h
    inc/AppUninstaller.h
    inc/BackdropBlur.h
    inc/BackdropProvider.h
    inc/Config.h
    inc/Controller.h
    inc/DebugTrace.h
    inc/EverythingSearch.h
    inc/GroupTile.h
    inc/HotkeyManager.h
    inc/IconProvider.h
    inc/Launcher.h
    inc/LauncherWindow.h
    inc/SearchQuery.h
    inc/ShortcutResolver.h
    inc/UiFont.h
    inc/WindowEffects.h
    # 资源: 由 AUTORCC 编译, 并按 qrc 里引用的文件建立依赖
    resources/winlauncher.qrc
    # exe 文件图标 + 属性页版本信息: Win32 资源(RT_ICON + VERSIONINFO), 和上面
    # 的 qrc 无关 —— QML 里的图标影响不了 exe 自身在资源管理器/任务栏上的样子。
    # 必须列进 sources, 否则 rc.exe 根本不跑, exe 显示空白通用图标、属性页全空。
    # 用 build 目录里的生成版(版本号由 project(VERSION) 注入)。
    ${CMAKE_CURRENT_BINARY_DIR}/winlauncher.rc
)

target_include_directories(WinLauncher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

target_link_libraries(WinLauncher PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
)

if(WIN32)
    # dwmapi  DWM 材质 / 圆角 / 深色标题栏
    # user32  窗口样式 / 区域裁剪 / RegisterHotKey
    # shell32 SHGetFileInfo / SHGetImageList / SHAppBarMessage
    # ole32   CoInitializeEx / IShellLink / IPersistFile
    target_link_libraries(WinLauncher PRIVATE dwmapi user32 shell32 ole32 oleaut32 uuid)
endif()

target_compile_definitions(WinLauncher PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    # 版本号单一真相源: 只在首行 project(VERSION) 维护一处, 编译期注入
    APP_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(WinLauncher PRIVATE
        /W3        # 警告等级
        /utf-8     # 源码按 UTF-8 处理, 否则中文乱码
        /wd4996    # 屏蔽 std::iterator 的 deprecate 警告
    )
    target_link_options(WinLauncher PRIVATE /DEBUG)
endif()
