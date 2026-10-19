import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import WinLauncher.Core

/*
 * 编辑窗口（独立、不透明）：只负责图标的增删。
 * 设置项全部搬到 SettingsWindow —— 两者混在一起太长。
 * 支持把 .lnk / .exe / .url 直接拖进窗口添加。
 *
 * 配色统一走 Theme，和面板 / 设置窗口同一套。
 */
Window {
    id: root

    width: 560
    height: 620
    visible: false
    title: qsTr("WinLauncher 编辑")
    color: Theme.bgWindow
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.NoDropShadowWindowHint

    function openWindow() {
        visible = true
        raise()
        requestActivate()
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bgWindow
        border.width: 1
        border.color: Theme.border
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
                color: Theme.textHi
                font.pixelSize: Theme.fsTitle
                font.weight: Font.DemiBold
            }

            Text {
                anchors.right: closeBtn.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("把 .lnk / .exe / .url 拖进窗口即可添加")
                color: Theme.textMid
                font.pixelSize: Theme.fsSmall
            }

            Rectangle {
                id: closeBtn
                width: 26
                height: 26
                radius: Theme.rSm
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                color: closeArea.containsMouse ? Theme.stateHover : "transparent"

                Behavior on color {
                    ColorAnimation { duration: Theme.durFast }
                }

                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    font.pixelSize: Theme.fsSmall
                    color: Theme.textHi
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
            color: Theme.divider
        }

        SectionTitle {
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 14
            Layout.fillWidth: true
            text: qsTr("应用（%1）").arg(Ctrl.totalCount)
            subText: qsTr("手动拖入的条目可移除；系统扫描来的条目请到源程序卸载")
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
                radius: Theme.rMd
                color: rowArea.containsMouse ? Theme.stateIdle : "transparent"

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
                            color: Theme.textHi
                            font.pixelSize: Theme.fsBody
                        }

                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                            text: modelData && modelData.target.length > 0
                                  ? modelData.target
                                  : qsTr("（未解析到可执行目标）")
                            color: Theme.textMid
                            font.pixelSize: Theme.fsTiny
                        }
                    }

                    Rectangle {
                        visible: modelData ? modelData.removable : false
                        Layout.preferredWidth: 50
                        Layout.preferredHeight: 24
                        radius: Theme.rSm
                        color: removeArea.containsMouse ? Theme.errSoft : Theme.stateIdle

                        Behavior on color {
                            ColorAnimation { duration: Theme.durFast }
                        }

                        Text {
                            anchors.centerIn: parent
                            text: qsTr("移除")
                            font.pixelSize: Theme.fsSmall
                            color: removeArea.containsMouse ? Theme.err : Theme.textHi
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
            color: Theme.divider
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
                onClicked: Ctrl.rescan()
            }

            PillButton {
                text: qsTr("重建图标缓存")
                onClicked: Ctrl.clearCacheAndRescan()
            }

            Item { Layout.fillWidth: true }

            Text {
                text: qsTr("设置请到「设置」窗口")
                color: Theme.textMid
                font.pixelSize: Theme.fsSmall
            }
        }
    }
}
