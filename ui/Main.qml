import QtQuick
import WinLauncher.Core

/*
 * 根窗口：一块弹出式面板（仿 macOS 26/27 的 Applications 浮层），不是全屏启动台。
 *
 * 视觉层级（从下到上）：
 *   1. backdropImage —— C++ 烘好的底图：模糊桌面 + 面板圆角 + 外阴影，铺满整个窗口
 *   2. LauncherPanel —— 内缩 shadowMargin 的面板内容（涂层 / 搜索栏 / 网格 / 状态行）
 *
 * 窗口比可见面板四周各大 shadowMargin，多出来的空间就是给外阴影的。
 * 这里不用任何系统材质（Mica / Acrylic），原因见 inc/BackdropBlur.h。
 */
LauncherWindow {
    id: root

    visible: false
    hideOnFocusOut: AppConfig.hideOnFocusOut
    darkBackdrop: AppConfig.darkMode
    cornerRadius: 26

    property alias panel: panel
    property alias editWindow: editWindow
    property alias settingsWindow: settingsWindow

    // [临时探针] QML 更新是否进 exe：正常应看到左上角一块红条
    Rectangle {
        width: 420
        height: 46
        color: "#FF0000"
        z: 9999

        Text {
            anchors.centerIn: parent
            text: "QML-PROBE-OK"
            color: "#FFFFFF"
            font.pixelSize: 22
            font.bold: true
        }
    }

    // 玻璃底图。C++ 在 showAtCursor() 里、显示窗口**之前**抓屏生成，
    // 所以第一帧就有图。URL 尾巴上的 token 用来绕过 QML 的图片缓存。
    Image {
        id: backdropImage
        anchors.fill: parent
        source: Ctrl.backdropToken > 0 ? "image://wlbg/panel?" + Ctrl.backdropToken : ""
        fillMode: Image.Stretch
        smooth: true
        cache: false
        asynchronous: false
    }

    LauncherPanel {
        id: panel
        anchors.fill: parent
        anchors.margins: root.shadowMargin // 四周留出阴影空间
        cornerRadius: root.cornerRadius
        glassReady: backdropImage.status === Image.Ready
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
        if (visible) {
            // 先复位过滤条件再聚焦，顺序反了会把焦点丢掉（清空会重设 TextField）
            panel.resetFilters()
            panel.focusSearch()
        }
    }
}
