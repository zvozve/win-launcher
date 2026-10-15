import QtQuick
import QtQuick.Layouts

/*
 * 编辑窗口里的分组标题（主标题 + 灰色说明）。
 * 颜色走 Theme。
 */
ColumnLayout {
    id: root

    property alias text: title.text
    property alias subText: subtitle.text

    spacing: 2

    Text {
        id: title
        Layout.fillWidth: true
        color: Theme.textHi
        font.pixelSize: Theme.fsLabel
        font.bold: true
    }

    Text {
        id: subtitle
        Layout.fillWidth: true
        visible: text.length > 0
        color: Theme.textMid
        font.pixelSize: Theme.fsTiny
        wrapMode: Text.WordWrap
    }
}
