import QtQuick
import WinLauncher.Core

/*
 * 顶部分类标签条（对齐 macOS Applications 面板那一排 Social / Creativity / …）。
 *
 * 数据来自 Ctrl.appCategories（每项 { key, label, count }），点一下就把
 * AppConfig.appCategory 改掉，网格随即只显示该分类的条目。
 *
 * 用 Flickable 而不是 ScrollView：分类多的时候要能横向滑，但不需要滚动条和那一层
 * 控件开销。滚轮也接上了 —— 鼠标竖着滚就横向走。
 */
Item {
    id: root

    property bool dark: true
    property color textColor: dark ? "#F2F2F7" : "#1C1C1E"

    implicitHeight: 28

    readonly property string current: AppConfig.appCategory

    Flickable {
        id: scroller
        anchors.fill: parent
        contentWidth: row.width
        contentHeight: height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick

        WheelHandler {
            onWheel: function (event) {
                const max = Math.max(0, scroller.contentWidth - scroller.width)
                scroller.contentX = Math.max(0, Math.min(max, scroller.contentX - event.angleDelta.y))
                event.accepted = true
            }
        }

        Row {
            id: row
            height: parent.height
            spacing: 6

            Repeater {
                model: Ctrl.appCategories

                delegate: Rectangle {
                    id: chip

                    readonly property bool active: modelData.key === root.current
                    readonly property bool hovered: hit.containsMouse

                    width: labelText.implicitWidth + 26
                    height: 26
                    anchors.verticalCenter: parent.verticalCenter
                    radius: height / 2

                    // 选中 = 实心胶囊（macOS 那边也是白底深字），未选中 = 很淡的底 + 细边
                    color: active
                           ? (root.dark ? Qt.rgba(1, 1, 1, 0.92) : Qt.rgba(1, 1, 1, 0.96))
                           : (hovered
                              ? (root.dark ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(0, 0, 0, 0.07))
                              : (root.dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.04)))
                    border.width: active ? 0 : 1
                    border.color: root.dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.06)

                    Behavior on color {
                        ColorAnimation { duration: 120 }
                    }

                    Text {
                        id: labelText
                        anchors.centerIn: parent
                        text: modelData.label
                        font.pixelSize: 12
                        font.weight: chip.active ? Font.DemiBold : Font.Normal
                        color: chip.active ? "#1C1C1E" : root.textColor
                    }

                    MouseArea {
                        id: hit
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: AppConfig.appCategory = modelData.key
                    }
                }
            }
        }
    }
}
