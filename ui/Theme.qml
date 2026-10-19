// ======================================================================
//  Theme.qml  -  win-launcher 全局主题（单例）
//
//  所有颜色 / 字号 / 间距 / 圆角 / 动效时长**只在这里定义一次**，
//  界面组件一律写 Theme.xxx，不再各写各的。
//
//  注册方式（对齐 icloud-mon）：
//    resources/qmldir  ->  singleton Theme 1.0 Theme.qml
//    resources/winlauncher.qrc  ->  alias="qml/Theme.qml" + alias="qml/qmldir"
//  同目录的 QML 直接写 `Theme.bgCard` 即可，不用额外 import。
//
//  深浅模式**只有一个来源**：AppConfig.darkMode。它是 C++ 单例上带
//  NOTIFY 的属性，切主题时所有绑定自动刷新 —— 不需要谁再把 dark 参数
//  一层层往下递。改造前每个组件都揣着一个 `property bool dark` 和
//  两个 color 属性，从 Main 一路透传到瓦片，改一处颜色要动十几个文件。
// ======================================================================
pragma Singleton

import QtQuick
import WinLauncher.Core

QtObject {
    id: t

    // ── 模式 ──
    readonly property bool dark: AppConfig.darkMode

    // ================= 强调色 =================
    // 改造前这里有两个蓝：面板 / 搜索栏用 macOS 的 #0A84FF，设置窗口用
    // Win11 的 #0067C0 —— 同一个应用两套强调色，并排看就是不干净。
    // 现在统一到系统蓝；Win11 只保留它的**几何形态**（按钮 4px 圆角、
    // 菜单 32 行高），不再保留它的色号。
    readonly property color accent:        dark ? "#0A84FF" : "#007AFF"
    readonly property color accentHover:   dark ? "#3D9BFF" : "#1A87FF"
    readonly property color accentPressed: dark ? "#0A6FD8" : "#0063D1"
    // ★ 名字不能叫 onAccent：QML 把 `on` + 大写字母开头的属性当**信号处理器**
    //   （报 "Cannot assign a value to a signal"），整个文件连类型都注册不上。
    readonly property color accentText:    "#FFFFFF"
    // 半透明强调底：录制态底、聚焦环用
    readonly property color accentSoft:    dark ? Qt.rgba(0.04, 0.52, 1.0, 0.22)
                                                : Qt.rgba(0.00, 0.48, 1.0, 0.12)
    readonly property color focusRing:     dark ? Qt.rgba(0.04, 0.52, 1.0, 0.85)
                                                : Qt.rgba(0.00, 0.48, 1.0, 0.80)

    // ================= 文字 =================
    readonly property color textHi:  dark ? "#F2F2F7" : "#1C1C1E"   // 主要文字
    readonly property color textMid: dark ? "#A1A1AA" : "#6E6E73"   // 次要说明
    readonly property color textLo:  dark ? "#7A7A82" : "#8E8E93"   // 占位 / 弱提示
    readonly property color textDis: dark ? "#55555C" : "#AEAEB2"   // 禁用

    // ================= 描边 / 分隔 =================
    readonly property color border:   dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.08)
    readonly property color borderHi: dark ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(0, 0, 0, 0.14)
    readonly property color divider:  dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.07)

    // ================= 玻璃面板 =================
    // ★ 这几个值是上一轮视觉调优的结果，别随手改：
    //   深色必须偏**黑**（发灰会显脏），浅色必须近乎不透明的**白**
    //   （macOS 的浅色 vibrancy 就是一块很亮的奶玻璃）。
    //   panelSolid = 抓屏失败 / 远程桌面时的不透明兜底。
    readonly property color panelSolid:       dark ? Qt.rgba(0.09, 0.09, 0.11, 0.92)
                                                   : Qt.rgba(0.96, 0.96, 0.98, 0.94)
    readonly property color panelTint:        dark ? Qt.rgba(0.06, 0.06, 0.08, 0.62)
                                                   : Qt.rgba(1, 1, 1, 0.82)
    readonly property color panelBorderSolid: dark ? Qt.rgba(1, 1, 1, 0.14)
                                                   : Qt.rgba(0, 0, 0, 0.10)
    readonly property color panelBorder:      dark ? Qt.rgba(1, 1, 1, 0.10)
                                                   : Qt.rgba(0, 0, 0, 0.06)

    // ================= 窗口 / 浮层 =================
    readonly property color bgWindow: dark ? "#1F1F22" : "#F3F3F3"  // 独立窗口底
    readonly property color bgCard:   dark ? "#2A2A2E" : "#FBFBFB"  // 分组卡片
    readonly property color bgMenu:   dark ? "#2C2C2E" : "#F9F9F9"  // 菜单 / 下拉浮层
    readonly property color bgInput:  dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.05)

    // ================= 滚动条 =================
    // 手柄三档：静止 / 悬停 / 按住拖动。面板本身是半透明玻璃，滚动条要是也
    // 用低 alpha，压在图标上会糊成一片 —— 所以这里比普通控件明显"实"得多。
    readonly property color scrollThumb:       dark ? Qt.rgba(1, 1, 1, 0.34) : Qt.rgba(0, 0, 0, 0.30)
    readonly property color scrollThumbHover:  dark ? Qt.rgba(1, 1, 1, 0.50) : Qt.rgba(0, 0, 0, 0.44)
    readonly property color scrollThumbActive: dark ? Qt.rgba(1, 1, 1, 0.66) : Qt.rgba(0, 0, 0, 0.56)

    // ================= 滚动位置字母气泡 =================
    // 两种主题都固定用「深底 + 白字」：这个气泡是浮在图标上面的，靠对比度
    // 取胜，跟随主题反而会在浅色面板里跟白底糊在一起。
    readonly property color badgeBg:   Qt.rgba(0, 0, 0, dark ? 0.74 : 0.66)
    readonly property color badgeText: "#FFFFFF"

    // ================= 通用交互态 =================
    // 默认底 / 悬停 / 按下 三档。控件直接挑一档用，不再各自调 alpha
    // （以前光"淡一点的底"就有 0.045 / 0.05 / 0.06 / 0.07 / 0.08 五种写法）。
    readonly property color stateIdle:    dark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0, 0, 0, 0.045)
    readonly property color stateHover:   dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.06)
    readonly property color statePressed: dark ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(0, 0, 0, 0.09)

    // ================= 网格瓦片 =================
    // 瓦片比普通控件大得多，hover 高亮要更"实"才看得出来（0.10 在大格子上完全看不到）
    readonly property color tileHover:       dark ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(0, 0, 0, 0.08)
    readonly property color tileHoverBorder: dark ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(0, 0, 0, 0.08)
    readonly property color tilePlaceholder: dark ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(0, 0, 0, 0.10)
    // 文件夹底板比单图标亮一档，一眼能看出"这是一叠"
    readonly property color folderBoard:       dark ? Qt.rgba(1, 1, 1, 0.13) : Qt.rgba(1, 1, 1, 0.55)
    readonly property color folderBoardBorder: dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.06)

    // ================= 分类胶囊 =================
    // 选中 = 实心白胶囊 + 深色字（macOS 那一排就是这么做的，深色模式下
    // 白胶囊也依然是最清楚的"当前项"）。
    //
    // 浅色模式要多一层描边：面板本身就是一块很亮的奶玻璃，纯白胶囊压在白底上
    // 几乎看不出边界（实测截图里"全部"和"开发"只差一点点亮度）。
    // 未选中**不加描边** —— 满排十几个描边胶囊看着很碎。
    readonly property color chipBg:            dark ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
    readonly property color chipBgHover:       dark ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(0, 0, 0, 0.06)
    readonly property color chipBgActive:      dark ? Qt.rgba(1, 1, 1, 0.92) : "#FFFFFF"
    readonly property color chipBorderActive:  dark ? "transparent" : Qt.rgba(0, 0, 0, 0.12)
    readonly property color chipTextActive:    "#1C1C1E"

    // ================= 语义色 =================
    readonly property color ok:   dark ? "#30D158" : "#248A3D"
    readonly property color warn: dark ? "#FF9F0A" : "#B25000"
    readonly property color err:  dark ? "#FF453A" : "#D70015"
    readonly property color info: dark ? "#64D2FF" : "#0071A4"
    // 危险操作的半透明底（「移除」按钮 hover）
    readonly property color errSoft: dark ? Qt.rgba(1.00, 0.27, 0.23, 0.20)
                                          : Qt.rgba(0.84, 0.00, 0.08, 0.10)

    // 窗口右上角关闭键 hover 的红（Win11 就是这一支）
    readonly property color closeHover: "#C42B1C"

    // ================= 开关 =================
    readonly property color trackOn:  accent
    readonly property color trackOff: dark ? "#5A5A5E" : "#8A8A8E"
    readonly property color knobOn:   dark ? "#000000" : "#FFFFFF"
    readonly property color knobOff:  "#FFFFFF"

    // ================= 字号 =================
    // 只有七档，**不要**再往界面里写裸数字。层级（以设置窗口为准）：
    //
    //   fsTiny   10   符号（▾ / ✕ / 路径尾巴）
    //   fsSmall  11   状态行 / 时间戳一类弱信息
    //   fsBody   12   正文、瓦片名字、设置项的**说明文字**
    //   fsLabel  13   设置窗口里所有交互控件的文字
    //                 （按钮 / 下拉框 / 数字框 / 热键框 —— 同档才显得是一套）
    //   fsTitle  14   窗口标题、分组标题、设置项标签、搜索框
    //   fsHead   15   预留（菜单图标字号在用）
    //   fsBig    20   滚动位置字母气泡
    //
    // 分组标题和行标签**同号**（14），靠 DemiBold / Normal 区分粗细 ——
    // 改造前分组标题 12、行标签 14，层级倒挂，看着就是不统一。
    readonly property int fsTiny:  10
    readonly property int fsSmall: 11
    readonly property int fsBody:  12
    readonly property int fsLabel: 13
    readonly property int fsTitle: 14
    readonly property int fsHead:  15
    readonly property int fsBig:   20

    // ================= 字重 =================
    // 全项目只有两档：Normal / DemiBold。不要混 `font.bold: true`
    // （那是 Font.Bold=75，比 DemiBold=63 粗，同一屏并排看着不齐）。

    // ================= 间距 =================
    readonly property int spXs: 4
    readonly property int spSm: 6
    readonly property int sp:   8
    readonly property int spMd: 12
    readonly property int spLg: 16
    readonly property int spXl: 22

    // ================= 圆角 =================
    readonly property int rXs:    4    // Win11 按钮 / 菜单项
    readonly property int rSm:    6    // 小控件（开关轨道以外的胶囊按高度自适应）
    readonly property int rMd:    8    // 菜单容器 / 设置卡片
    readonly property int rInput: 9    // 搜索框
    readonly property int rLg:    10   // 网格瓦片 hover 框
    readonly property int rXl:    12   // 大块浮层
    readonly property int rPanel: 26   // 弹出面板，**必须等于 LauncherWindow.cornerRadius**

    // ================= 动效时长 =================
    readonly property int durFast: 90    // hover 反馈
    readonly property int durBase: 120   // 状态切换
    readonly property int durSlow: 180   // 面板 / 大块渐隐
}
