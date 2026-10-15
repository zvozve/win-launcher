import QtQuick
import QtQuick.Controls
import WinLauncher.Core

/*
 * Win11 右键菜单项：左边一个 Fluent 图标，右边文字。
 *
 * 图标不是图片资源，就是 **Segoe Fluent Icons** 里的字形（系统自带）。
 * 码点从 UiFont.Glyph 取，字体名问 UiFont.menuIconFamily —— 万一这台机器
 * 没有这个字体，menuIconsAvailable 为 false，图标整块收起来，退回纯文字菜单，
 * 而不是让你看一堆豆腐块。
 *
 * 尺寸按 Win11：行高 32、圆角 4、左右各留 4px 内边距（高亮块不能顶到容器边上，
 * 否则第一项/最后一项 hover 时高亮块的直角会盖住菜单容器的圆角）。
 *
 * 颜色走 Theme。
 */
MenuItem {
    id: root

    property string iconGlyph: ""     // 码点（UiFont.Glyph.* 或者直接给 "E8B7" 这类字符）

    implicitHeight: 32
    implicitWidth: 188

    readonly property bool showIcon: UiFont.menuIconsAvailable && root.iconGlyph.length > 0
    // 图标列宽固定，文字左边缘才会对齐（没图标时不留空档）
    readonly property int iconColumn: root.showIcon ? 22 : 0

    contentItem: Row {
        spacing: 0
        // 与外层 4px 内边距叠加后，图标离菜单边框 12px
        leftPadding: 8
        rightPadding: 8

        Text {
            width: root.iconColumn
            height: root.implicitHeight
            visible: root.showIcon
            text: root.showIcon ? root.iconGlyph : ""
            color: Theme.textHi
            font.family: UiFont.menuIconFamily
            // Fluent 字形的视觉重心偏上，15px 配 32px 行高最接近系统观感
            font.pixelSize: Theme.fsHead
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            width: root.implicitWidth - root.iconColumn - 16
            height: root.implicitHeight
            text: root.text
            color: Theme.textHi
            font.pixelSize: Theme.fsLabel
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    background: Rectangle {
        anchors.fill: parent
        anchors.margins: 4
        radius: Theme.rXs
        color: root.highlighted ? Theme.stateHover : "transparent"

        Behavior on color {
            ColorAnimation { duration: 80 }
        }
    }

    // 禁用项（比如「卸载此程序」没匹配到卸载项时）整体压暗
    opacity: root.enabled ? 1.0 : 0.45
}
