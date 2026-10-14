import QtQuick

/*
 * 网格里的单个应用图标：图标 + 名称，悬停高亮、按下缩放。
 */
Item {
    id: root

    property var app: null
    property bool dark: true
    property color textColor: dark ? "#F2F2F7" : "#1C1C1E"

    signal activated()

    readonly property bool hasIcon: app !== null && image.status === Image.Ready

    Rectangle {
        id: background
        anchors.fill: parent
        anchors.margins: 2
        radius: 14
        color: area.containsMouse
               ? (root.dark ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.07))
               : "transparent"

        Behavior on color {
            ColorAnimation { duration: 90 }
        }
    }

    // 没有图标时的占位块
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 10
        width: 52
        height: 52
        radius: 12
        visible: !root.hasIcon
        color: root.dark ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(0, 0, 0, 0.10)

        Text {
            anchors.centerIn: parent
            text: root.app ? root.app.name.charAt(0).toUpperCase() : "?"
            font.pixelSize: 22
            font.bold: true
            color: root.textColor
            opacity: 0.75
        }
    }

    Image {
        id: image
        anchors.horizontalCenter: parent.horizontalCenter
        y: 10
        width: 52
        height: 52
        source: root.app ? root.app.iconUrl : ""
        sourceSize: Qt.size(104, 104)
        fillMode: Image.PreserveAspectFit
        smooth: true
        asynchronous: true
        visible: status === Image.Ready
    }

    Text {
        id: label
        anchors.top: image.bottom
        anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width - 8
        text: root.app ? root.app.name : ""
        color: root.textColor
        font.pixelSize: 11
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
        lineHeight: 0.95
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.activated()
    }

    scale: area.pressed ? 0.93 : 1.0
    Behavior on scale {
        NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
    }
}
