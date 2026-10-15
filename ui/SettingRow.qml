import QtQuick
import QtQuick.Layouts

/*
 * 设置行：左侧「标签 + 说明」，右侧放任意控件（默认属性即 data，直接写子控件即可）。
 *
 * 尺寸按 Win11「设置」应用来：整行 60 高（说明会换行时自动放宽到 72）、
 * 标签 14px、说明 12px，控件垂直居中。
 *
 * 左右内边距 20：Win11 官方是 16，但那个数字配的是 13px 字号；
 * 这里标签 14px、说明 12px，16 会显得挤，20 观感才对得上。
 */
RowLayout {
    id: root

    property string label: ""
    property string hint: ""
    property color textColor: "#FFFFFF"
    property color subTextColor: "#C5C5C5"

    spacing: 16
    implicitHeight: hint.length > 30 ? 72 : 60
    Layout.leftMargin: 20
    Layout.rightMargin: 20
    Layout.topMargin: 2
    Layout.bottomMargin: 2

    ColumnLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
        spacing: 3

        Text {
            Layout.fillWidth: true
            text: root.label
            color: root.textColor
            font.pixelSize: 14
            elide: Text.ElideRight
        }

        Text {
            Layout.fillWidth: true
            visible: root.hint.length > 0
            text: root.hint
            color: root.subTextColor
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            lineHeight: 1.15
            lineHeightMode: Text.ProportionalHeight
        }
    }
}

