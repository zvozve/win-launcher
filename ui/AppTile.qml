import QtQuick
import QtQuick.Controls
import WinLauncher.Core

/*
 * 网格里的单个应用图标。
 * 尺寸由 GridView 的 cellWidth/cellHeight 决定，内容用显式锚点排：
 *   悬停背景框（整格）+ 图标 + 名称
 *
 * 右键菜单：打开 / 以管理员身份打开 / 打开文件所在位置 / 从启动台移除。
 * 这里只发信号，真正的动作在 Controller（LauncherPanel 负责连起来）。
 *
 * 颜色全部走 Theme，不再需要 dark / textColor 参数。
 */
Item {
    id: root

    // 诊断用（--dump-icons 会按这两个 objectName 找节点）
    objectName: "appTile"
    readonly property string appName: app ? app.name : ""

    property var app: null

    signal activated()
    signal adminRequested()
    signal revealRequested()
    signal uninstallRequested()
    signal removeRequested()

    readonly property bool hasIcon: app !== null && image.status === Image.Ready
    // 7 列的格子大约 148×176，图标取到 ~62 才接近 macOS 启动台的观感
    readonly property int iconSize: Math.max(
                                        32, Math.min(72, Math.round(Math.min(width * 0.44,
                                                                            height * 0.42))))

    // ---- 右键菜单 ----
    Menu {
        id: contextMenu

        /*
         * 菜单项统一走 WinMenuItem（图标 + 文字，Win11 观感）。
         * 图标是 Segoe Fluent Icons 的字形 —— 本机没这个字体时
         * UiFont.menuIconsAvailable 为 false，图标自动收起，只剩文字。
         */
        delegate: WinMenuItem {}

        // Win11 菜单容器：圆角 8、近实色底、1px 淡边。
        // 宽度写死 196（菜单项 188 + 两侧各 4 内缩）；高度量 contentItem 的
        // 内容高（这个是可靠的）再加边框。别用 contentItem.implicitWidth ——
        // Menu 的 contentItem 横向 implicitWidth 是 0，拿来算宽度菜单会缩没。
        background: Rectangle {
            implicitWidth: 196
            implicitHeight: Math.max(32, (contentItem ? contentItem.implicitHeight : 0) + 2)
            radius: Theme.rMd
            color: Theme.bgMenu
            border.width: 1
            border.color: Theme.border
        }

        // 分隔线也内缩 4px，和菜单项的高亮块左右对齐
        component MenuSep: MenuSeparator {
            implicitHeight: 9

            contentItem: Rectangle {
                implicitWidth: 188
                implicitHeight: 1
                anchors.margins: 4
                color: Theme.divider
            }
        }

        WinMenuItem {
            text: qsTr("打开")
            iconGlyph: String.fromCharCode(UiFont.Open)
            onTriggered: root.activated()
        }

        MenuSep {}

        WinMenuItem {
            text: qsTr("以管理员身份打开")
            iconGlyph: String.fromCharCode(UiFont.Admin)
            onTriggered: root.adminRequested()
        }

        WinMenuItem {
            text: qsTr("打开文件所在位置")
            iconGlyph: String.fromCharCode(UiFont.Reveal)
            onTriggered: root.revealRequested()
        }

        MenuSep {}

        // 省略号表示会再开一个窗口（软件自己的卸载向导）
        WinMenuItem {
            text: qsTr("卸载此程序…")
            iconGlyph: String.fromCharCode(UiFont.Uninstall)
            onTriggered: root.uninstallRequested()
        }

        MenuSep {}

        WinMenuItem {
            text: qsTr("从启动台移除")
            iconGlyph: String.fromCharCode(UiFont.Remove)
            onTriggered: root.removeRequested()
        }
    }

    // ---- 鼠标悬停背景框 ----
    Rectangle {
        id: background
        anchors.fill: parent
        anchors.margins: 4
        radius: Theme.rLg
        color: area.containsMouse ? Theme.tileHover : "transparent"
        border.width: 1
        border.color: area.containsMouse ? Theme.tileHoverBorder : "transparent"

        Behavior on color {
            ColorAnimation { duration: Theme.durFast }
        }
        Behavior on border.color {
            ColorAnimation { duration: Theme.durFast }
        }
    }

    /*
     * 图标 + 名称，用显式锚点排，**不要用 Column**。
     *
     * QML 的 positioner（Column / Row）里再给子项挂 anchors.horizontalCenter，
     * positioner 会把该子项的隐式尺寸当 0 来算 —— 实测后果就是
     * 「图标掉到格子顶部、名字整行不显示」。显式锚点没有这个坑。
     */
    Item {
        id: iconBox
        width: root.iconSize
        height: root.iconSize
        anchors.horizontalCenter: parent.horizontalCenter
        // 图标 + 名字作为一组垂直居中：图标整体上移一个固定量，给名字腾位
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: -12

        // 没有图标时的占位块
        Rectangle {
            objectName: "tilePlaceholder"
            anchors.fill: parent
            radius: Math.round(root.iconSize * 0.24)
            visible: !root.hasIcon
            color: Theme.tilePlaceholder

            Text {
                anchors.centerIn: parent
                text: root.app ? root.app.name.charAt(0).toUpperCase() : "?"
                font.pixelSize: Math.round(root.iconSize * 0.44)
                font.bold: true
                color: Theme.textHi
                opacity: 0.75
            }
        }

        Image {
            id: image
            objectName: "tileIcon"
            anchors.fill: parent
            source: root.app ? root.app.iconUrl : ""
            sourceSize: Qt.size(root.iconSize * 2, root.iconSize * 2)
            fillMode: Image.PreserveAspectFit
            smooth: true
            asynchronous: true
        }
    }

    Text {
        id: label
        objectName: "tileLabel"
        anchors.top: iconBox.bottom
        anchors.topMargin: 6
        anchors.horizontalCenter: parent.horizontalCenter
        // 取 max(0,…)：瓦片宽度还没定下来时 parent.width 可能是 0，
        // 负宽 Text 什么都不画（名字整行消失就是这么来的）
        width: Math.max(0, parent.width - 16)
        text: root.app ? root.app.name : ""
        color: Theme.textHi
        font.pixelSize: Theme.fsBody
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
        // 行距 0.95 会把两行名字压在一起，1.05 才是可读的行距
        lineHeight: 1.05
        lineHeightMode: Text.ProportionalHeight
    }

    MouseArea {
        id: area
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: function (mouse) {
            if (mouse.button === Qt.RightButton)
                contextMenu.popup()
            else
                root.activated()
        }
    }

    scale: area.pressed ? 0.94 : 1.0
    Behavior on scale {
        NumberAnimation { duration: Theme.durFast; easing.type: Easing.OutCubic }
    }
}
