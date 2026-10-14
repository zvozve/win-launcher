import QtQuick
import QtQuick.Controls
import WinLauncher.Core

/*
 * 搜索栏。
 * 左边放大镜、中间输入框、右边清空键，末尾是搜索源切换（本地 / Everything）。
 * Everything 未就绪时切换按钮会提示并自动回退本地过滤。
 */
Item {
    id: root

    property bool dark: true
    property alias text: field.text
    property alias placeholder: field.placeholderText
    property color textColor: dark ? "#F2F2F7" : "#1C1C1E"
    property color hintColor: dark ? "#8E8E93" : "#8A8A8E"

    readonly property bool everythingReady: Ctrl.everything.available
    readonly property string source: AppConfig.searchSource

    implicitHeight: 40

    signal accepted()

    function focusInput() {
        field.forceActiveFocus()
    }

    Rectangle {
        id: box
        anchors.fill: parent
        radius: 10
        color: root.dark ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(0, 0, 0, 0.05)
        border.width: 1
        border.color: field.activeFocus
                      ? Qt.rgba(0.04, 0.52, 1.0, 0.75)
                      : Qt.rgba(0, 0, 0, 0)

        Behavior on border.color {
            ColorAnimation { duration: 110 }
        }

        // 放大镜（画出来，避免依赖字体符号）
        Item {
            id: magnifier
            width: 16
            height: 16
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter

            Rectangle {
                width: 10
                height: 10
                radius: 5
                color: "transparent"
                border.width: 1.6
                border.color: root.hintColor
            }

            Rectangle {
                width: 1.6
                height: 6
                radius: 0.8
                color: root.hintColor
                x: 9
                y: 9
                rotation: -45
                transformOrigin: Item.TopLeft
            }
        }

        TextField {
            id: field
            anchors.left: magnifier.right
            anchors.right: clearButton.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.leftMargin: 8
            anchors.rightMargin: 6

            placeholderText: qsTr("搜索应用")
            placeholderTextColor: root.hintColor
            color: root.textColor
            font.pixelSize: 15
            background: null
            selectByMouse: true

            onAccepted: root.accepted()

            Keys.onEscapePressed: (event) => {
                if (field.text.length > 0) {
                    field.text = ""
                    event.accepted = true
                } else {
                    event.accepted = false
                }
            }
        }

        // 清空
        Rectangle {
            id: clearButton
            width: 18
            height: 18
            radius: 9
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: sourceSwitch.left
            anchors.rightMargin: 6
            visible: field.text.length > 0
            color: clearArea.containsMouse
                   ? (root.dark ? Qt.rgba(1, 1, 1, 0.22) : Qt.rgba(0, 0, 0, 0.14))
                   : (root.dark ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.08))

            Text {
                anchors.centerIn: parent
                text: "✕"
                font.pixelSize: 10
                color: root.textColor
            }

            MouseArea {
                id: clearArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    field.text = ""
                    field.forceActiveFocus()
                }
            }
        }

        // 搜索源切换：本地 / Everything
        Row {
            id: sourceSwitch
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Repeater {
                model: [
                    { key: "local", label: qsTr("本地") },
                    { key: "everything", label: qsTr("全盘") }
                ]

                delegate: Rectangle {
                    required property var modelData
                    readonly property bool current: root.source === modelData.key
                    width: 44
                    height: 22
                    radius: 6
                    color: current
                           ? Qt.rgba(0.04, 0.52, 1.0, 0.85)
                           : (srcArea.containsMouse
                              ? (root.dark ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(0, 0, 0, 0.09))
                              : "transparent")

                    Text {
                        anchors.centerIn: parent
                        text: modelData.label
                        font.pixelSize: 11
                        color: current ? "#FFFFFF" : root.hintColor
                    }

                    MouseArea {
                        id: srcArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (modelData.key === "everything" && !root.everythingReady) {
                                Ctrl.notifyError(qsTr("未检测到 Everything64.dll，仍使用本地过滤"))
                                return
                            }
                            AppConfig.searchSource = modelData.key
                        }
                    }
                }
            }
        }
    }
}
