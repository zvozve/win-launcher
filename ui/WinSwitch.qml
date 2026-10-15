import QtQuick

/*
 * Win11 风格开关：胶囊轨道 + 滑块，打开 = 强调色。
 *
 * 抽成独立组件（和 icloud-mon 的 AppSwitch 一个路子）：
 * 配色只有 Theme 一个来源，设置页里直接写 WinSwitch {} 就行，
 * 不用每处再传 dark / 颜色。
 *
 * 只发 toggled() 信号、**不改自己的 checked** —— 这样外部的
 * `checked: AppConfig.xxx` 绑定不会被内部赋值打断。
 */
Rectangle {
    id: root

    property bool checked: false

    signal toggled()

    implicitWidth: 40
    implicitHeight: 20
    radius: height / 2
    color: checked ? Theme.trackOn : Theme.trackOff

    Behavior on color {
        ColorAnimation { duration: Theme.durBase }
    }

    Rectangle {
        width: 12
        height: 12
        radius: 6
        anchors.verticalCenter: parent.verticalCenter
        x: root.checked ? root.width - width - 4 : 4
        color: root.checked ? Theme.knobOn : Theme.knobOff

        Behavior on x {
            NumberAnimation { duration: Theme.durBase; easing.type: Easing.OutCubic }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled()
    }
}
