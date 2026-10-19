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

    /*
     * 键盘选中。由 LauncherPanel 在网格拿到焦点时按 GridView.isCurrentItem 设置，
     * 和鼠标 hover 互斥：网格一旦拿到键盘焦点，鼠标 hover 就让位给键盘选中，
     * 不会出现「鼠标停一格、键盘选中另一格、两个同时亮」。
     */
    property bool keySelected: false

    // 网格是否持有键盘焦点（LauncherPanel 透传 grid.activeFocus）。
    // 为 true 时鼠标 hover 高亮让位给键盘选中（二者互斥，避免双高亮）。
    property bool keyboardActive: false

    // 自己在网格中的下标（LauncherPanel 透传 index），用于把 hover 事件上报回去
    property int tileIndex: -1

    // 鼠标进入 / 离开本格：LauncherPanel 据此记录 hover 起点、并切回鼠标模式
    signal tileEntered(int index)
    signal tileExited(int index)

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

        /*
         * ★ 宽写死 196、padding 写死 6、内部 2px 元素缝压掉。
         *
         * [宽] QQuickMenu 默认把 popup 撑成「最宽项 contentItem 隐式宽」，而
         * 子项的固定 width（图标列 22px）不进隐式宽求和 —— 不写死的话菜单
         * 只有 ~132 宽，图标列再砍 22，标签仅剩 64px，"以管理员身份打开"
         * 直接被省略号截断（qml 实测 truncated=true）。写死 196 后菜单项
         * 宽 184、标签 146px 完整（truncated=false）。
         *
         * [padding] ★ 必须显式写。main.cpp 里 QQuickStyle::setStyle("Basic")，
         * **Basic 样式的 Menu 默认 padding = 0**：popup 高 = 各菜单项累加
         * = 5×36 + 3×11 = 213，首项从 y=0 起、末项 bottom 正好等于 popup 高，
         * 于是「底部文字贴底」、首末项 hover 高亮紧贴圆角边框看着像被裁掉。
         * 显式 padding: 6（Win11 新版大间距）后 popup = 6 + 213 + 6 = 225，
         * 高亮块离外边框 10px。
         *
         * 之前记的「高 197 / 上下各 5px 内边距」是在**未指定样式**的 qml.exe
         * 里量的（那时走默认样式，padding=5），不是程序真实表现 —— 已作废。
         * 量菜单几何必须带 QT_QUICK_CONTROLS_STYLE=Basic。
         *
         * [缝] 内部 ListView 默认 spacing=2，会在每个元素之间塞 2px 缝 ——
         * Win11 菜单行与行贴平，视觉空隙由高亮块自带的 4px 缩进提供；
         * 8 个元素共 7 道缝 = 14px，菜单会看着发"散"。contentItem 在
         * Component.onCompleted 时已存在，直接设即可。
         */
        width: 196
        padding: 6
        Component.onCompleted: if (contentItem) contentItem.spacing = 0

        // Win11 菜单容器：圆角 8、近实色底、1px 淡边。
        // 宽和上面写死的 196 对齐；高只是占位（见上）。
        background: Rectangle {
            implicitWidth: 196
            implicitHeight: 32
            radius: Theme.rMd
            color: Theme.bgMenu
            border.width: 1
            border.color: Theme.border
        }

        // 分隔线也内缩 6px，和菜单项的高亮块左右对齐。
        // ★ 高度写 height（决定菜单总高中的 11px/条）；1px 的线靠 contentItem
        //   anchors.fill + 上下各 5px 边距居中（11-5-5=1），线才不会变粗。
        //   不 fill 的话线贴在槽的顶部，就是「分隔线紧贴文字」的观感
        //   （2026-10-16 实测：旧版线在槽顶、新版居中）。
        component MenuSep: MenuSeparator {
            height: 11

            contentItem: Rectangle {
                anchors.fill: parent
                // 左右与菜单项高亮块对齐；上下各留 5px 空隙，
                // 1px 的线直接贴着文字就是「菜单好挤」的来源
                anchors.leftMargin: 4
                anchors.rightMargin: 4
                anchors.topMargin: 5
                anchors.bottomMargin: 5
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

    /*
     * 悬停 / 键盘选中 背景框。
     * 鼠标 hover 也用 accentSoft 填充 + focusRing 描边（和键盘选中同一套视觉），
     * 否则大格子上 0.08 的灰底根本看不见，点了一片不知道选的是哪个。
     * 键盘选中描边 2px、鼠标 hover 1px，保留「键盘更醒目」的层级。
     */
    Rectangle {
        id: background
        anchors.fill: parent
        anchors.margins: 4
        radius: Theme.rLg
        color: root.keySelected ? Theme.accentSoft
                                : (area.containsMouse ? Theme.accentSoft : "transparent")
        border.width: root.keySelected ? 2 : 1
        border.color: root.keySelected ? Theme.focusRing
                                       : (area.containsMouse ? Theme.focusRing : "transparent")

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
                font.weight: Font.DemiBold
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
        onEntered: root.tileEntered(root.tileIndex)
        onExited: root.tileExited(root.tileIndex)
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
