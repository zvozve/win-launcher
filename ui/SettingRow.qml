import QtQuick
import QtQuick.Layouts

/*
 * 设置行：左侧「标签 + 说明」，右侧放任意控件（默认属性即 data，直接写子控件即可）。
 */
RowLayout {
    id: root

    property string label: ""
    property string hint: ""
    property color textColor: "#F2F2F7"
    property color subTextColor: "#9A9AA0"

    spacing: 12

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 1

        Text {
            Layout.fillWidth: true
            text: root.label
            color: root.textColor
            font.pixelSize: 12
        }

        Text {
            Layout.fillWidth: true
            visible: root.hint.length > 0
            text: root.hint
            color: root.subTextColor
            font.pixelSize: 10
            wrapMode: Text.WordWrap
        }
    }
}
