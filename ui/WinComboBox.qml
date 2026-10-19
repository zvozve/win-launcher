import QtQuick
import QtQuick.Controls

/*
 * 主题化下拉框。
 *
 * 为什么必须自己画：QtQuick.Controls 的默认 ComboBox 在 Windows 上走 Fusion
 * 观感 —— 灰底、近直角、深色箭头，摆进这套「白卡片 + 8px 圆角」的设置页里
 * 一眼就"不是一起的"。所以 background / contentItem / indicator / popup /
 * delegate 全部按 Theme 重画。
 *
 * 用法和默认 ComboBox 一致，model 传字符串数组即可。
 */
ComboBox {
    id: root

    implicitWidth: 130
    implicitHeight: 30
    // 和 WinSpinBox / WinButton / HotkeyRecorder 同档（fsLabel）——
    // 设置窗口里一排控件文字大小不一，是「不统一」最直接的来源
    font.pixelSize: Theme.fsLabel

    background: Rectangle {
        radius: Theme.rSm
        color: root.hovered || root.popup.visible ? Theme.stateHover : Theme.stateIdle
        border.width: 1
        border.color: root.visualFocus ? Theme.accent : Theme.border

        Behavior on color {
            ColorAnimation { duration: Theme.durFast }
        }
    }

    contentItem: Text {
        leftPadding: 10
        rightPadding: 26
        text: root.displayText
        color: root.enabled ? Theme.textHi : Theme.textDis
        font: root.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    // 箭头用字形画（SearchBar 里的搜索类型选择器也是这个做法，
    // 不引 SVG 资源、也不依赖 Fluent 字体）
    indicator: Text {
        x: root.width - 22
        y: 0
        width: 14
        height: root.height
        text: "▾"
        color: root.enabled ? Theme.textLo : Theme.textDis
        font.pixelSize: Theme.fsTiny
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    delegate: ItemDelegate {
        id: item

        required property var modelData
        required property int index

        width: root.width - 8
        height: 28
        highlighted: root.highlightedIndex === index

        contentItem: Text {
            leftPadding: 8
            text: item.modelData
            color: Theme.textHi
            font.pixelSize: Theme.fsBody
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: Theme.rSm
            color: item.highlighted ? Theme.stateHover
                 : (item.index === root.currentIndex ? Theme.accentSoft : "transparent")

            Behavior on color {
                ColorAnimation { duration: Theme.durFast }
            }
        }
    }

    popup: Popup {
        y: root.height + 4
        width: root.width
        implicitHeight: contentItem.implicitHeight + 8
        padding: 4

        background: Rectangle {
            radius: Theme.rMd
            color: Theme.bgMenu
            border.width: 1
            border.color: Theme.border
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
        }
    }
}
