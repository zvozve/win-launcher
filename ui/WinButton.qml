import QtQuick

/*
 * Win11 风格按钮：圆角 4、1px 淡边、hover/pressed 分层。
 *
 * 启动台面板那边用的是 PillButton（胶囊），保持 macOS 观感；
 * 设置窗口这种「Windows 自己的界面」用这个。
 * enabled 直接用 Item 自带的（禁用时 MouseArea 自然收不到事件）。
 */
Rectangle {
    id: root

    property string text: ""
    property bool dark: false
    property bool accent: false // 主要按钮：Win11 的强调色实心

    signal clicked()

    implicitWidth: label.implicitWidth + 28
    implicitHeight: 32
    radius: 4

    color: {
        if (!root.enabled)
            return root.dark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(0, 0, 0, 0.03)
        if (root.accent) {
            if (area.pressed)
                return root.dark ? "#3AA0D8" : "#005A9E"
            return root.dark ? "#60CDFF" : "#0067C0"
        }
        if (area.pressed)
            return root.dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.06)
        if (area.containsMouse)
            return root.dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.05)
        return root.dark ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(1, 1, 1, 0.70)
    }

    border.width: root.accent ? 0 : 1
    border.color: root.dark ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(0, 0, 0, 0.09)

    Behavior on color {
        ColorAnimation { duration: 90 }
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        font.pixelSize: 13
        color: {
            if (!root.enabled)
                return root.dark ? "#7A7A7A" : "#9A9A9A"
            if (root.accent)
                return root.dark ? "#000000" : "#FFFFFF"
            return root.dark ? "#FFFFFF" : "#1A1A1A"
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
