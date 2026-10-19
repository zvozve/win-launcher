import QtQuick
import QtQuick.Controls
import WinLauncher.Core

/*
 * 搜索类型选择器（搜索栏最左侧）。
 * 只负责下拉交互；真正生效的类型存在 AppConfig.searchCategory，
 * 由 Controller 同时用于「本地过滤」和「Everything 的 ext: 过滤」。
 *
 * 颜色走 Theme。
 */
Item {
    id: root

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
        radius: Theme.rSm
        color: area.containsMouse || menu.visible ? Theme.stateHover : Theme.stateIdle
        border.width: 1
        border.color: Theme.border

        Behavior on color {
            ColorAnimation { duration: Theme.durFast }
        }

        Row {
            anchors.centerIn: parent
            spacing: 4

            Text {
                text: root.labels[root.currentIndex]
                color: Theme.textHi
                font.pixelSize: Theme.fsBody
            }

            Text {
                text: "▾"
                color: Theme.textLo
                font.pixelSize: Theme.fsTiny
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
            radius: Theme.rMd
            color: Theme.bgMenu
            border.width: 1
            border.color: Theme.border
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
                radius: Theme.rSm
                color: index === root.currentIndex
                       ? Theme.accent
                       : (itemArea.containsMouse ? Theme.stateHover : "transparent")

                Behavior on color {
                    ColorAnimation { duration: Theme.durFast }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    text: modelData
                    font.pixelSize: Theme.fsBody
                    color: index === root.currentIndex ? Theme.accentText : Theme.textHi
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
