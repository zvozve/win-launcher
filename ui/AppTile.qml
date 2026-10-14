import QtQuick
import QtQuick.Controls

/*
 * 网格里的单个应用图标。
 * 尺寸由 GridView 的 cellWidth/cellHeight 决定，内容用居中 Column 排：
 *   悬停背景框（整格）+ 图标 + 名称
 *
 * 右键菜单：打开 / 以管理员身份打开 / 打开文件所在位置 / 从启动台移除。
 * 这里只发信号，真正的动作在 Controller（LauncherPanel 负责连起来）。
 */
Item {
    id: root

    property var app: null
    property bool dark: true
    property color textColor: dark ? "#F2F2F7" : "#1C1C1E"

    signal activated()
    signal adminRequested()
    signal revealRequested()
    signal removeRequested()

    readonly property bool hasIcon: app !== null && image.status === Image.Ready
    // 7×4 的格子大约 165×140，图标取到 ~62 才接近 macOS 启动台的观感
    readonly property int iconSize: Math.max(
                                        28, Math.min(64, Math.round(Math.min(width * 0.38,
                                                                            height * 0.48))))

    // ---- 右键菜单 ----
    Menu {
        id: contextMenu

        delegate: MenuItem {
            id: menuEntry

            implicitHeight: 30
            implicitWidth: 164

            contentItem: Text {
                text: menuEntry.text
                font.pixelSize: 12
                color: root.dark ? "#F2F2F7" : "#1C1C1E"
                verticalAlignment: Text.AlignVCenter
                leftPadding: 12
            }

            background: Rectangle {
                radius: 6
                color: menuEntry.highlighted
                       ? (root.dark ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(0, 0, 0, 0.07))
                       : "transparent"
            }
        }

        background: Rectangle {
            implicitWidth: 176
            implicitHeight: 8 + contentItem.implicitHeight
            radius: 10
            color: root.dark ? Qt.rgba(0.15, 0.15, 0.17, 0.99) : Qt.rgba(1, 1, 1, 0.99)
            border.width: 1
            border.color: root.dark ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.08)
        }

        MenuItem {
            text: qsTr("打开")
            onTriggered: root.activated()
        }

        MenuSeparator {
            contentItem: Rectangle {
                implicitWidth: 164
                implicitHeight: 1
                color: root.dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.08)
            }
        }

        MenuItem {
            text: qsTr("以管理员身份打开")
            onTriggered: root.adminRequested()
        }

        MenuItem {
            text: qsTr("打开文件所在位置")
            onTriggered: root.revealRequested()
        }

        MenuSeparator {
            contentItem: Rectangle {
                implicitWidth: 164
                implicitHeight: 1
                color: root.dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.08)
            }
        }

        MenuItem {
            text: qsTr("从启动台移除")
            onTriggered: root.removeRequested()
        }
    }

    // ---- 鼠标悬停背景框 ----
    Rectangle {
        id: background
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
        Behavior on border.color {
            ColorAnimation { duration: 90 }
        }
    }

    Column {
        anchors.centerIn: parent
        width: parent.width - 14
        spacing: 7

        Item {
            width: root.iconSize
            height: root.iconSize
            anchors.horizontalCenter: parent.horizontalCenter

            // 没有图标时的占位块
            Rectangle {
                anchors.fill: parent
                radius: Math.round(root.iconSize * 0.24)
                visible: !root.hasIcon
                color: root.dark ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(0, 0, 0, 0.12)

                Text {
                    anchors.centerIn: parent
                    text: root.app ? root.app.name.charAt(0).toUpperCase() : "?"
                    font.pixelSize: Math.round(root.iconSize * 0.44)
                    font.bold: true
                    color: root.textColor
                    opacity: 0.75
                }
            }

            Image {
                id: image
                anchors.fill: parent
                source: root.app ? root.app.iconUrl : ""
                sourceSize: Qt.size(root.iconSize * 2, root.iconSize * 2)
                fillMode: Image.PreserveAspectFit
                smooth: true
                asynchronous: true
            }
        }

        Text {
            id: label
            width: parent.width
            text: root.app ? root.app.name : ""
            color: root.textColor
            font.pixelSize: 11
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            // 行距 0.95 会把两行名字压在一起，1.05 才是可读的行距
            lineHeight: 1.05
            lineHeightMode: Text.ProportionalHeight
        }
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
