import QtQuick

/*
 * Win11 风格按钮：圆角 4、1px 淡边、hover/pressed 分层。
 *
 * 启动台面板那边用的是 PillButton（胶囊），保持 macOS 观感；
 * 设置窗口这种「Windows 自己的界面」用这个 —— 保留 Win11 的**几何形态**
 * （4px 圆角、32 高），但强调色跟全局统一到系统蓝，不再单独用 #0067C0。
 *
 * enabled 直接用 Item 自带的（禁用时 MouseArea 自然收不到事件）。
 */
Rectangle {
    id: root

    property string text: ""
    property bool accent: false // 主要按钮：强调色实心

    signal clicked()

    implicitWidth: label.implicitWidth + 28
    implicitHeight: 32
    radius: Theme.rXs

    color: {
        if (!root.enabled)
            return Theme.stateIdle
        if (root.accent) {
            if (area.pressed)
                return Theme.accentPressed
            if (area.containsMouse)
                return Theme.accentHover
            return Theme.accent
        }
        if (area.pressed)
            return Theme.statePressed
        if (area.containsMouse)
            return Theme.stateHover
        return Theme.stateIdle
    }

    border.width: root.accent ? 0 : 1
    border.color: Theme.border

    Behavior on color {
        ColorAnimation { duration: Theme.durFast }
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        font.pixelSize: Theme.fsLabel
        color: {
            if (!root.enabled)
                return Theme.textDis
            if (root.accent)
                return Theme.accentText
            return Theme.textHi
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
