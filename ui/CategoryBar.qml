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
 *
 * 胶囊配色走 Theme：选中 = 实心白胶囊 + 深色字（macOS 就是这样），
 * 未选中只有一层很淡的底、**不描边** —— 满排十几个描边胶囊看着很碎。
 */
Item {
    id: root

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

                    color: active
                           ? Theme.chipBgActive
                           : (hovered ? Theme.chipBgHover : Theme.chipBg)
                    // 浅色模式下白胶囊需要一层描边才看得出边界（深色下该值为 transparent）
                    border.width: active ? 1 : 0
                    border.color: Theme.chipBorderActive

                    Behavior on color {
                        ColorAnimation { duration: Theme.durBase }
                    }

                    Text {
                        id: labelText
                        anchors.centerIn: parent
                        text: modelData.label
                        font.pixelSize: Theme.fsBody
                        font.weight: chip.active ? Font.DemiBold : Font.Normal
                        color: chip.active ? Theme.chipTextActive : Theme.textHi
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
