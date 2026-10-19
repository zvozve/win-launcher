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
 * 尺寸按 Win11 新版（大间距）：行高 36、高亮块四周内缩 4、菜单本体再留 6px 外边距
 * （高亮块不能顶到容器边上，否则第一项/最后一项 hover 时高亮块的直角会盖住
 * 菜单容器的圆角）。
 *
 * 颜色走 Theme。
 */
MenuItem {
    id: root

    property string iconGlyph: ""     // 码点（UiFont.Glyph.* 或者直接给 "E8B7" 这类字符）

    /*
     * ★ 这里必须写 height，不能只写 implicitHeight。
     *
     * 实测（qml 复刻菜单逐项量）：QQuickMenu 的 popup 高度按各菜单项的
     * **height()** 累加，写 height 是最直接的保证；implicitHeight 虽然
     * 实测也能活着（没被重算盖掉），但那是隐式路径，不写 height 时条目
     * 高度会跟着 contentItem 的隐式高度浮动，不可控。
     *
     * 2026-10-16 修正：此前这里写的是「QQuickControl 在 componentComplete
     * 会按 contentItem 重算 implicitHeight，把 QML 赋的值盖掉」—— 该结论
     * **已被实测证伪**（旧版 implicitHeight: 9 / :32 都原样保留，新旧两版
     * 菜单几何完全一致）。旧版真正难看的是分隔线 1px 线贴在 9px 槽的
     * **顶部**（contentItem 没 anchors.fill），不是高度被压。
     *
     * implicitWidth 同理不可靠，所以下面文字宽度都从 parent.width 反推。
     */
    implicitWidth: 188
    height: 36

    /*
     * ★ padding 必须清零。
     *
     * Basic 样式的 MenuItem 自带 `padding: 6`，而我们把 height 写死成 36 ——
     * 于是在"高度已固定"的前提下，contentItem 会被放到 y=6、高度压成
     * 36-6-6=24。里面文字又写的是 height: root.height(36)，等于从 y=6
     * 往下画 36，直接溢出到 42。
     *
     * 实测后果（qml 逐层量，行高 32 那版）：item h=32, topPad=6/bottomPad=6,
     * contentItem y=6 h=20, background(高亮块) y=4 h=24 中心 16，文字中心 22
     * → 比高亮低 6px。观感就是「文字上面一大片空、下面压住高亮的下沿」。
     *
     * 清零后 contentItem 撑满整个行高，文字中心与高亮块中心重合
     * （36 → 两者都是 18）。
     * 水平缩进改由 contentItem 里那个 Row 的 leftPadding/rightPadding 负责。
     */
    padding: 0

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
            height: root.height
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
            width: Math.max(0, parent.width - root.iconColumn - 16)
            height: root.height
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
