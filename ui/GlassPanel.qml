import QtQuick

/*
 * 面板的「涂层」：叠在 C++ 烘好的玻璃底图上。
 *
 * 底图（模糊 + 圆角 + 外阴影）由 Main.qml 里的 Image 铺满整个窗口；
 * 这里只负责面板范围内的色调与高光描边，所以 radius 必须和
 * LauncherWindow.cornerRadius 一致 —— 底图就是按那个半径裁出来的。
 *
 * 颜色全部来自 Theme（深浅模式由 Theme.dark 内部决定），
 * 所以这里不再需要 `dark` 参数。
 *
 * 底图缺失时（抓屏失败 / 远程桌面 / 锁屏）退化成一层更实的纯色，仍然可读。
 */
Rectangle {
    id: root

    // C++ 烘的玻璃底图是否已就绪
    property bool glassReady: false
    property real cornerRadius: Theme.rPanel

    radius: cornerRadius

    /*
     * 有真实模糊底图时叠一层色调；没有时用 panelSolid 兜底。
     *
     * 浅色是**近乎不透明白**：macOS 浅色 vibrancy 看过去就是一块很亮的奶玻璃，
     * 透出来的只是极淡的底色。之前用 0.52 的结果是「模糊的桌面 + 一层薄白」，
     * 整体发灰发脏 —— 这个值必须够大才显得干净。
     * 深色同理，要偏黑而不是灰。
     */
    color: !glassReady ? Theme.panelSolid : Theme.panelTint

    border.width: 1
    border.color: !glassReady ? Theme.panelBorderSolid : Theme.panelBorder

    Behavior on color {
        ColorAnimation { duration: Theme.durSlow }
    }
}
