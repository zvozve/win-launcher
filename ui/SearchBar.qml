import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import WinLauncher.Core

/*
 * 搜索栏（左 → 右）：
 *   搜索类型选择器 | 放大镜 | 输入框 | 清空 | 搜索源（本地 / 全盘）
 *
 * 类型选择器改的是 AppConfig.searchCategory，Controller 会同时用它做
 * 本地扩展名过滤和 Everything 的 ext: 过滤。
 */
Item {
    id: root

    property bool dark: true
    property alias text: field.text
    property color textColor: dark ? "#F2F2F7" : "#1C1C1E"
    property color hintColor: dark ? "#8E8E93" : "#8A8A8E"

    readonly property bool everythingReady: Ctrl.everything.available
    readonly property string source: AppConfig.searchSource

    implicitHeight: 38

    signal accepted()

    function focusInput() {
        field.forceActiveFocus()
    }

    Rectangle {
        id: box
        anchors.fill: parent
        radius: 9
        color: root.dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.05)
        border.width: 1
        border.color: field.activeFocus
                      ? Qt.rgba(0.04, 0.52, 1.0, 0.8)
                      : Qt.rgba(0, 0, 0, 0)

        Behavior on border.color {
            ColorAnimation { duration: 110 }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 8
            spacing: 6

            // ---- 搜索类型 ----
            CategorySelector {
                id: category
                Layout.preferredWidth: 78
                Layout.preferredHeight: 26
                dark: root.dark
                textColor: root.textColor
                hintColor: root.hintColor
            }

            // 分隔线
            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 18
                color: root.dark ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.08)
            }

            // ---- 放大镜（画出来，不依赖字体符号）----
            Item {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16

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
                Layout.fillWidth: true
                Layout.fillHeight: true

                placeholderText: qsTr("搜索")
                placeholderTextColor: root.hintColor
                color: root.textColor
                font.pixelSize: 14
                background: null
                selectByMouse: true
                leftPadding: 0
                rightPadding: 0

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

            // ---- 清空 ----
            Rectangle {
                id: clearButton
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                radius: 9
                visible: field.text.length > 0
                color: clearArea.containsMouse
                       ? (root.dark ? Qt.rgba(1, 1, 1, 0.24) : Qt.rgba(0, 0, 0, 0.16))
                       : (root.dark ? Qt.rgba(1, 1, 1, 0.13) : Qt.rgba(0, 0, 0, 0.09))

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

            // ---- 搜索源：本地 / 全盘 ----
            Row {
                Layout.alignment: Qt.AlignVCenter
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
                               ? Qt.rgba(0.04, 0.52, 1.0, 0.88)
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
}
