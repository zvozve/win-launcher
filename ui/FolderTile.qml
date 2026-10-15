import QtQuick
import QtQuick.Controls
import WinLauncher.Core

/*
 * 网格里的「文件夹」瓦片：同源多入口（J-Link 一家 44 个图标那种）折叠成一格。
 *
 * 样子照着 macOS 启动台的文件夹：圆角底板 + 2×2 的小图标 + 下方名字。
 * 点一下进去（Ctrl.openGroup），网格换成这一组里的应用；面板顶部会出现面包屑。
 *
 * 右键菜单是应用菜单的删减版 —— 文件夹没有单一 target，
 * 所以「以管理员打开 / 打开文件所在位置 / 卸载」这几个跟目标绑定的动作不适用。
 */
Item {
    id: root

    property var folder: null
    property bool dark: true
    property color textColor: dark ? "#F2F2F7" : "#1C1C1E"

    signal activated()

    readonly property int iconSize: Math.max(
                                        32, Math.min(72, Math.round(Math.min(width * 0.44,
                                                                            height * 0.42))))
    // 底板比单图标略大一圈，能明显看出「这是一叠」
    readonly property int board: Math.round(iconSize * 1.06)
    readonly property int cell: Math.round((board - 14) / 2)

    Menu {
        id: contextMenu

        delegate: WinMenuItem {
            dark: root.dark
        }

        background: Rectangle {
            implicitWidth: 196
            implicitHeight: Math.max(32, (contentItem ? contentItem.implicitHeight : 0) + 2)
            radius: 8
            color: root.dark ? "#2C2C2C" : "#F9F9F9"
            border.width: 1
            border.color: root.dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.10)
        }

        WinMenuItem {
            text: qsTr("打开文件夹")
            iconGlyph: String.fromCharCode(UiFont.Reveal)
            dark: root.dark
            onTriggered: root.activated()
        }
    }

    // ---- 悬停背景框（和应用瓦片保持一致）----
    Rectangle {
        anchors.fill: parent
        anchors.margins: 4
        radius: 10
        color: area.containsMouse
               ? (root.dark ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(0, 0, 0, 0.10))
               : "transparent"
        border.width: 1
        border.color: area.containsMouse
                      ? (root.dark ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(0, 0, 0, 0.08))
                      : "transparent"

        Behavior on color {
            ColorAnimation { duration: 90 }
        }
    }

    /*
     * 底板 + 名字，显式锚点（和 AppTile 一样，**不用 Column**：
     * positioner 子项挂 anchors 会让隐式尺寸算成 0，名字那一行直接不显示）。
     */
    Item {
        id: boardBox
        width: root.board
        height: root.board
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: -12

        Rectangle {
            anchors.fill: parent
            radius: Math.round(root.board * 0.22)
            color: root.dark ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.55)
            border.width: 1
            border.color: root.dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.06)

            // 2×2 预览：members 少于 4 个时右下留空，不补图标
            Column {
                anchors.centerIn: parent
                spacing: 2

                Row {
                    spacing: 2

                    Repeater {
                        model: [0, 1]

                        Image {
                            width: root.cell
                            height: root.cell
                            source: {
                                const items = root.folder ? root.folder.previewItems : null
                                return (items && items.length > index) ? items[index].iconUrl : ""
                            }
                            sourceSize: Qt.size(root.cell * 2, root.cell * 2)
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            asynchronous: true
                        }
                    }
                }

                Row {
                    spacing: 2

                    Repeater {
                        model: [2, 3]

                        Image {
                            width: root.cell
                            height: root.cell
                            source: {
                                const items = root.folder ? root.folder.previewItems : null
                                return (items && items.length > index) ? items[index].iconUrl : ""
                            }
                            sourceSize: Qt.size(root.cell * 2, root.cell * 2)
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            asynchronous: true
                        }
                    }
                }
            }
        }
    }

    Text {
        anchors.top: boardBox.bottom
        anchors.topMargin: 6
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width - 16
        text: root.folder ? root.folder.label : ""
        color: root.textColor
        font.pixelSize: 12
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
        lineHeight: 1.05
        lineHeightMode: Text.ProportionalHeight
    }

    MouseArea {
        id: area
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: function (mouse) {
            if (mouse.button === Qt.RightButton)
                contextMenu.popup()
            else
                root.activated()
        }
    }

    scale: area.pressed ? 0.94 : 1.0
    Behavior on scale {
        NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
    }
}
