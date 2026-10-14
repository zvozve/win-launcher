import QtQuick

/*
 * 胶囊按钮，面板右上角的「编辑 / 关闭」用。
 */
Rectangle {
    id: root

    property bool dark: true
    property color textColor: dark ? "#F2F2F7" : "#1C1C1E"
    property alias text: label.text

    signal clicked()

    implicitWidth: label.implicitWidth + 24
    implicitHeight: 30
    radius: 8

    color: area.containsMouse
           ? (root.dark ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(0, 0, 0, 0.09))
           : (root.dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.045))

    Behavior on color {
        ColorAnimation { duration: 90 }
    }

    Text {
        id: label
        anchors.centerIn: parent
        color: root.textColor
        font.pixelSize: 12
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    scale: area.pressed ? 0.95 : 1.0
    Behavior on scale {
        NumberAnimation { duration: 80 }
    }
}
