cmake_minimum_required(VERSION 3.21)

project(WinLauncher VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(WINLAUNCHER_CONSOLE "Build with a console window for debugging" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Widgets Svg)

qt_standard_project_setup(REQUIRES 6.5)

set(WINLAUNCHER_SOURCES
    src/main.cpp

    src/core/appitem.h
    src/core/appitem.cpp
    src/core/debugtrace.h
    src/core/debugtrace.cpp
    src/core/shortcutresolver.h
    src/core/shortcutresolver.cpp
    src/core/iconprovider.h
    src/core/iconprovider.cpp
    src/core/appscanner.h
    src/core/appscanner.cpp
    src/core/launcher.h
    src/core/launcher.cpp

    src/core/windoweffects.h
    src/core/windoweffects.cpp
    src/core/launcherwindow.h
    src/core/launcherwindow.cpp
    src/core/hotkeymanager.h
    src/core/hotkeymanager.cpp
    src/core/config.h
    src/core/config.cpp
    src/core/everythingsearch.h
    src/core/everythingsearch.cpp

    src/app/controller.h
    src/app/controller.cpp
)

qt_add_executable(WinLauncher ${WINLAUNCHER_SOURCES})

qt_add_qml_module(WinLauncher
    URI WinLauncher
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/LauncherPanel.qml
        qml/EditWindow.qml
        qml/GlassPanel.qml
        qml/SearchBar.qml
        qml/AppTile.qml
        qml/PillButton.qml
        qml/SectionTitle.qml
        qml/SettingRow.qml
        qml/HotkeyRecorder.qml
)

if(NOT WINLAUNCHER_CONSOLE)
    set_target_properties(WinLauncher PROPERTIES WIN32_EXECUTABLE ON)
endif()

target_include_directories(WinLauncher PRIVATE src)

target_link_libraries(WinLauncher PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    Qt6::Svg
    dwmapi
    user32
    shell32
    ole32
    oleaut32
    uuid
)
