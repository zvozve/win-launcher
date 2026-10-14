import QtQuick
import QtQuick.Layouts

/*
 * 编辑窗口里的分组标题（主标题 + 灰色说明）。
 */
ColumnLayout {
    id: root

    property alias text: title.text
    property alias subText: subtitle.text
    property color textColor: "#F2F2F7"
    property color subTextColor: "#9A9AA0"

    spacing: 2

    Text {
        id: title
        Layout.fillWidth: true
        color: root.textColor
        font.pixelSize: 13
        font.bold: true
    }

    Text {
        id: subtitle
        Layout.fillWidth: true
        visible: text.length > 0
        color: root.subTextColor
        font.pixelSize: 10
        wrapMode: Text.WordWrap
    }
}
