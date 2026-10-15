import QtQuick
import QtQuick.Controls

/*
 * 主题化数字输入框（SpinBox）。
 *
 * 同 WinComboBox：默认 SpinBox 在 Windows 上是 Fusion 观感（白底直角、
 * 上下箭头按钮），和设置页的卡片不搭，所以 background / contentItem /
 * up / down 全部按 Theme 重画。
 *
 * 加减按钮做成卡片内的两枚小圆角块（内缩 3px），而不是贴着边缘的直角块 ——
 * 后者会顶破外层圆角。
 */
SpinBox {
    id: root

    implicitWidth: 130
    implicitHeight: 30
    font.pixelSize: Theme.fsBody

    background: Rectangle {
        radius: Theme.rSm
        color: Theme.stateIdle
        border.width: 1
        border.color: root.activeFocus ? Theme.accent : Theme.border

        Behavior on border.color {
            ColorAnimation { duration: Theme.durFast }
        }
    }

    contentItem: TextInput {
        text: root.textFromValue(root.value, root.locale)
        font: root.font
        color: root.enabled ? Theme.textHi : Theme.textDis
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: TextInput.AlignVCenter
        readOnly: !root.editable
        validator: root.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        selectByMouse: true
    }

    up.indicator: Rectangle {
        x: root.width - width - 3
        y: 3
        width: 24
        height: root.height - 6
        radius: Theme.rSm
        color: root.up.pressed ? Theme.statePressed
             : (root.up.hovered ? Theme.stateHover : "transparent")

        Behavior on color {
            ColorAnimation { duration: Theme.durFast }
        }

        Text {
            anchors.centerIn: parent
            text: "+"
            font.pixelSize: Theme.fsTitle
            color: root.value < root.to ? Theme.textHi : Theme.textDis
        }
    }

    down.indicator: Rectangle {
        x: 3
        y: 3
        width: 24
        height: root.height - 6
        radius: Theme.rSm
        color: root.down.pressed ? Theme.statePressed
             : (root.down.hovered ? Theme.stateHover : "transparent")

        Behavior on color {
            ColorAnimation { duration: Theme.durFast }
        }

        Text {
            anchors.centerIn: parent
            text: "−"
            font.pixelSize: Theme.fsTitle
            color: root.value > root.from ? Theme.textHi : Theme.textDis
        }
    }
}
