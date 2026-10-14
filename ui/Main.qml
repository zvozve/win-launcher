import QtQuick
import WinLauncher.Core

/*
 * 根窗口：一块弹出式面板（仿 macOS 26/27 的 Applications 浮层），不是全屏启动台。
 * 尺寸/位置/毛玻璃/圆角都由 C++ 侧 LauncherWindow 负责，这里只管内容与窗口协作。
 */
LauncherWindow {
    id: root

    visible: false
    hideOnFocusOut: AppConfig.hideOnFocusOut
    darkBackdrop: AppConfig.darkMode
    cornerRadius: 18

    property alias panel: panel
    property alias editWindow: editWindow
    property alias settingsWindow: settingsWindow

    LauncherPanel {
        id: panel
        anchors.fill: parent
        onLaunchRequested: (item) => Ctrl.launchItem(item)
        onEditRequested: editWindow.openWindow()
        onSettingsRequested: settingsWindow.openWindow()
        onCloseRequested: root.hidePanel()
    }

    EditWindow {
        id: editWindow
    }

    SettingsWindow {
        id: settingsWindow
    }

    // Ctrl 的「外部触发」信号（托盘 / 热键 / 单实例）
    Connections {
        target: Ctrl

        function onShowPanelRequested() {
            root.showAtCursor()
        }

        function onEditRequested() {
            editWindow.openWindow()
        }

        function onSettingsRequested() {
            settingsWindow.openWindow()
        }
    }

    onVisibleChanged: {
        if (visible)
            panel.focusSearch()
    }
}
