import QtQuick
import QtQuick.Controls
import WinLauncher.Core

/*
 * 搜索类型选择器（搜索栏最左侧）。
 * 只负责下拉交互；真正生效的类型存在 AppConfig.searchCategory，
 * 由 Controller 同时用于「本地过滤」和「Everything 的 ext: 过滤」。
 */
Item {
    id: root

    property bool dark: true
    property color textColor: dark ? "#F2F2F7" : "#1C1C1E"
    property color hintColor: dark ? "#8E8E93" : "#8A8A8E"

    readonly property var keys: Ctrl.searchCategoryKeys
    readonly property var labels: Ctrl.searchCategoryLabels
    readonly property int currentIndex: Math.max(0, keys.indexOf(AppConfig.searchCategory))

    implicitWidth: 78
    implicitHeight: 26

    function choose(index) {
        if (index >= 0 && index < keys.length)
            AppConfig.searchCategory = keys[index]
    }

    Rectangle {
        id: pill
        anchors.fill: parent
        radius: 7
        color: area.containsMouse || menu.visible
               ? (root.dark ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(0, 0, 0, 0.09))
               : (root.dark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0, 0, 0, 0.04))
        border.width: 1
        border.color: root.dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.05)

        Behavior on color {
            ColorAnimation { duration: 90 }
        }

        Row {
            anchors.centerIn: parent
            spacing: 4

            Text {
                text: root.labels[root.currentIndex]
                color: root.textColor
                font.pixelSize: 12
            }

            Text {
                text: "▾"
                color: root.hintColor
                font.pixelSize: 10
            }
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: menu.visible ? menu.close() : menu.open()
    }

    Popup {
        id: menu
        parent: root
        x: 0
        y: root.height + 4
        width: 116
        padding: 4

        background: Rectangle {
            radius: 8
            color: root.dark ? "#2B2B30" : "#FFFFFF"
            border.width: 1
            border.color: root.dark ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.08)
        }

        contentItem: ListView {
            implicitHeight: contentHeight
            model: root.labels
            boundsBehavior: Flickable.StopAtBounds
            interactive: false

            delegate: Rectangle {
                required property int index
                required property string modelData

                width: ListView.view.width
                height: 28
                radius: 6
                color: index === root.currentIndex
                       ? Qt.rgba(0.04, 0.52, 1.0, 0.88)
                       : (itemArea.containsMouse
                          ? (root.dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.06))
                          : "transparent")

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    text: modelData
                    font.pixelSize: 12
                    color: index === root.currentIndex ? "#FFFFFF" : root.textColor
                }

                MouseArea {
                    id: itemArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.choose(index)
                        menu.close()
                    }
                }
            }
        }
    }
}
