import QtQuick

/*
 * 胶囊按钮，面板右上角的「编辑 / 设置」用。
 * 颜色走 Theme。
 */
Rectangle {
    id: root

    property alias text: label.text

    signal clicked()

    implicitWidth: label.implicitWidth + 24
    implicitHeight: 30
    radius: Theme.rMd

    color: area.containsMouse ? Theme.stateHover : Theme.stateIdle

    Behavior on color {
        ColorAnimation { duration: Theme.durFast }
    }

    Text {
        id: label
        anchors.centerIn: parent
        color: Theme.textHi
        font.pixelSize: Theme.fsBody
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
