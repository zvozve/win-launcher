import QtQuick
import WinLauncher.Core

/*
 * 根窗口：一块弹出式面板（仿 macOS 26/27 的 Applications 浮层），不是全屏启动台。
 * 尺寸/位置/毛玻璃由 C++ 侧 LauncherWindow 负责，这里只管内容与窗口协作。
 */
LauncherWindow {
    id: root

    visible: false
    hideOnFocusOut: AppConfig.hideOnFocusOut
    darkBackdrop: AppConfig.darkMode

    property alias panel: panel
    property alias editWindow: editWindow

    // 面板内容
    LauncherPanel {
        id: panel
        anchors.fill: parent
        onLaunchRequested: (item) => Ctrl.launchItem(item)
        onEditRequested: editWindow.openWindow()
        onCloseRequested: root.hidePanel()
    }

    // 独立编辑窗口
    EditWindow {
        id: editWindow
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
    }

    onVisibleChanged: {
        if (visible)
            panel.focusSearch()
    }
}
