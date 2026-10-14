import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import WinLauncher.Core

/*
 * 编辑窗口（独立、不透明）：只负责图标的增删。
 * 设置项全部搬到 SettingsWindow —— 两者混在一起太长。
 * 支持把 .lnk / .exe / .url 直接拖进窗口添加。
 */
Window {
    id: root

    property bool dark: AppConfig.darkMode
    readonly property color textColor: dark ? "#F2F2F7" : "#1C1C1E"
    readonly property color subTextColor: dark ? "#9A9AA0" : "#6E6E73"
    readonly property color bgColor: dark ? "#232327" : "#F2F2F4"
    readonly property color lineColor: dark ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(0, 0, 0, 0.08)

    width: 560
    height: 620
    visible: false
    title: qsTr("WinLauncher 编辑")
    color: bgColor
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.NoDropShadowWindowHint

    function openWindow() {
        visible = true
        raise()
        requestActivate()
    }

    Rectangle {
        anchors.fill: parent
        color: root.bgColor
        border.width: 1
        border.color: root.lineColor
    }

    // ---- 拖放添加 ----
    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            if (drop.hasUrls)
                Ctrl.addPaths(drop.urls)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        // ---- 标题栏（可拖动）----
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: "transparent"

            MouseArea {
                anchors.fill: parent
                onPressed: root.startSystemMove()
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("编辑图标")
                color: root.textColor
                font.pixelSize: 14
                font.bold: true
            }

            Text {
                anchors.right: closeBtn.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("把 .lnk / .exe / .url 拖进窗口即可添加")
                color: root.subTextColor
                font.pixelSize: 11
            }

            Rectangle {
                id: closeBtn
                width: 26
                height: 26
                radius: 7
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                color: closeArea.containsMouse
                       ? (root.dark ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(0, 0, 0, 0.09))
                       : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    font.pixelSize: 11
                    color: root.textColor
                }

                MouseArea {
                    id: closeArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.visible = false
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: root.lineColor
        }

        SectionTitle {
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 14
            Layout.fillWidth: true
            text: qsTr("应用（%1）").arg(Ctrl.totalCount)
            subText: qsTr("手动拖入的条目可移除；系统扫描来的条目请到源程序卸载")
            textColor: root.textColor
            subTextColor: root.subTextColor
        }

        // ---- 应用列表 ----
        ListView {
            id: appList
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 10
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: Ctrl.apps
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                width: appList.width
                height: 44
                radius: 8
                color: rowArea.containsMouse
                       ? (root.dark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0, 0, 0, 0.045))
                       : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 10

                    Image {
                        Layout.preferredWidth: 24
                        Layout.preferredHeight: 24
                        source: modelData ? modelData.iconUrl : ""
                        sourceSize: Qt.size(48, 48)
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: modelData ? modelData.name : ""
                            color: root.textColor
                            font.pixelSize: 12
                        }

                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                            text: modelData && modelData.target.length > 0
                                  ? modelData.target
                                  : qsTr("（未解析到可执行目标）")
                            color: root.subTextColor
                            font.pixelSize: 10
                        }
                    }

                    Rectangle {
                        visible: modelData ? modelData.removable : false
                        Layout.preferredWidth: 50
                        Layout.preferredHeight: 24
                        radius: 6
                        color: removeArea.containsMouse
                               ? Qt.rgba(1, 0.35, 0.35, 0.22)
                               : (root.dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.05))

                        Text {
                            anchors.centerIn: parent
                            text: qsTr("移除")
                            font.pixelSize: 11
                            color: root.textColor
                        }

                        MouseArea {
                            id: removeArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Ctrl.removeApp(modelData.id)
                        }
                    }
                }

                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: root.lineColor
        }

        RowLayout {
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 10
            Layout.bottomMargin: 12
            Layout.fillWidth: true
            spacing: 8

            PillButton {
                text: qsTr("重新扫描")
                dark: root.dark
                textColor: root.textColor
                onClicked: Ctrl.rescan()
            }

            PillButton {
                text: qsTr("重建图标缓存")
                dark: root.dark
                textColor: root.textColor
                onClicked: Ctrl.clearCacheAndRescan()
            }

            Item { Layout.fillWidth: true }

            Text {
                text: qsTr("设置请到「设置」窗口")
                color: root.subTextColor
                font.pixelSize: 11
            }
        }
    }
}
