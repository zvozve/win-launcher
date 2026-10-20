import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import WinLauncher.Core

/*
 * 弹出面板主体：搜索栏 + 图标网格 + 状态行。
 *
 * 网格密度对齐 macOS 26/27 的 Applications 面板：**7 列 × 4 行**，
 * 但滚动方向是**纵向**的（macOS 那边是一个纵向滚动的分组列表，不是横向翻页，
 * 所以这里也没有底部圆点）。cellHeight 取容器高度的 1/4，于是不管面板多高，
 * 可见区域永远正好 4 行，滚轮/拖动按行吸附。
 *
 * 与 macOS 的一处**有意偏离**：右侧挂了一条可拖动的滚动条，且滚动时浮出
 * 当前首行的首字母。一屏 28 个、总共两百多项，纯靠滚轮找东西太慢。
 *
 * 颜色一律走 Theme（深浅模式由 Theme.dark 内部决定），
 * 这里不再需要 dark / textColor / subTextColor 参数，更不用往下透传。
 */
Item {
    id: root

    signal launchRequested(var item)
    signal editRequested()
    signal settingsRequested()
    signal closeRequested()

    // 圆角半径必须和 LauncherWindow.cornerRadius 一致（底图按这个半径裁的）
    property real cornerRadius: Theme.rPanel
    // C++ 烘的玻璃底图是否已就绪；没就绪时 GlassPanel 走不透明兜底
    property bool glassReady: false

    // 一屏可见的网格密度（对齐 macOS：7 列 × 4 行）
    readonly property int visibleRows: 4
    readonly property int columns: Math.max(3, AppConfig.gridColumns)

    /*
     * 进文件夹前记住顶层网格的滚动位置，退回来时回到原处。
     *
     * 存 contentY 而不是「条目下标」：回来时顶层列表是完全一样的（同分类、
     * 同搜索词、同排序，行高也一样），contentY 就是最准的锚点。
     * -1 = 当前不处于「从文件夹返回」的状态。
     */
    property real savedTopY: -1

    // 鼠标当前悬停的网格下标（tile 进入时上报）；-1 = 不在任何格上
    property int hoverIndex: -1
    // 交互模式：false = 鼠标模式（显示 hover 高亮、隐藏键盘高亮），
    // true = 键盘模式（反过来）。切换：鼠标进入任意格 -> 鼠标模式；
    // 键盘接管网格（Tab 进入 / 鼠标悬停时按方向键）-> 键盘模式。
    property bool keyboardMode: false

    // 进文件夹统一走这里：先记位置再进去（直接调 Ctrl.openGroupKey 会丢掉位置）
    function openFolder(key) {
        if (!key)
            return
        // 只在顶层记录；已经在文件夹里就不覆盖
        if (savedTopY < 0 && Ctrl.openGroup.length === 0)
            savedTopY = grid.contentY
        Ctrl.openGroupKey(key)
    }

    /*
     * 退出文件夹后把滚动位置放回去。
     *
     * 为什么要试两次：openGroupChanged 发出时 model 才刚换回来，GridView 的
     * contentHeight 有可能还是旧值（懒布局），这时直接赋 contentY 会被夹住，
     * 退回来看起来还是「停在中途」甚至「回到顶部」。所以先 callLater 贴一次，
     * 60ms 后再贴一次兜底 —— 第二次时布局一定算完了，赋值是幂等的。
     */
    property real restoreY: -1

    function applyRestore() {
        if (restoreY < 0)
            return
        const maxY = Math.max(0, grid.contentHeight - grid.height)
        grid.contentY = Math.max(0, Math.min(restoreY, maxY))
    }

    Timer {
        id: restoreTimer
        interval: 60
        repeat: false
        onTriggered: {
            root.applyRestore()
            root.restoreY = -1
        }
    }

    Connections {
        target: Ctrl

        function onOpenGroupChanged() {
            if (Ctrl.openGroup.length > 0)
                return
            if (savedTopY < 0)
                return

            root.restoreY = savedTopY
            savedTopY = -1
            Qt.callLater(root.applyRestore)
            restoreTimer.restart()
        }
    }

    // 鼠标悬停在网格上、还没进键盘模式时，按方向键直接从鼠标位置开始键盘导航
    Keys.onLeftPressed:  (e) => root.beginKeyboardFromHover(e, -1, 0)
    Keys.onRightPressed: (e) => root.beginKeyboardFromHover(e,  1, 0)
    Keys.onUpPressed:    (e) => root.beginKeyboardFromHover(e,  0, -1)
    Keys.onDownPressed:  (e) => root.beginKeyboardFromHover(e,  0,  1)

    /*
     * 鼠标模式 + 焦点不在网格时按方向键：从鼠标悬停格进入键盘导航，
     * 并顺着方向先移动一格（"继承然后移动"）。
     * 已在键盘模式 / 网格有焦点 -> 交给 GridView 内置导航，不抢；
     * 焦点在搜索框 / 分类条 -> 方向键不跨区（Tab 才是跨区入口）。
     */
    function beginKeyboardFromHover(event, dx, dy) {
        if (root.keyboardMode || grid.activeFocus)
            return
        if (searchBar.activeFocus || catBar.activeFocus)
            return
        if (root.hoverIndex < 0 || Ctrl.gridItems.length === 0)
            return

        root.keyboardMode = true
        const n = Ctrl.gridItems.length
        let i = root.hoverIndex
        if (dx !== 0)
            i = Math.max(0, Math.min(n - 1, i + dx))
        else
            i = Math.max(0, Math.min(n - 1, i + dy * root.columns))
        grid.currentIndex = i
        grid.forceActiveFocus()
        grid.positionViewAtIndex(grid.currentIndex, GridView.Contain)
        event.accepted = true
    }

    readonly property int categoryIndex: Math.max(0, Ctrl.searchCategoryKeys.indexOf(
                                                      AppConfig.searchCategory))
    readonly property string categoryLabel: Ctrl.searchCategoryLabels[categoryIndex]

    /*
     * ---- 键盘导航（不想为了启动一个搜到的应用去摸鼠标）----
     *
     * 三块区域，Tab 顺序循环（Shift+Tab 反向）：
     *   搜索框 → 分类条 → 网格(或全盘结果) → 搜索框 …
     * 方向键只负责模块内部移动：搜索框里是光标（←→），分类条里 ←→ 换胶囊
     * （Enter 确认），网格里 ↑↓←→ 换格子（Enter 启动/进文件夹）。
     * 方向键绝不跨区——跨区只能按 Tab / Shift+Tab。
     */
    function focusSearch() {
        root.keyboardMode = false
        searchBar.focusInput()
    }

    function focusCategories() {
        root.keyboardMode = false
        catBar.focusBar()
    }

    function focusGrid() {
        if (Ctrl.gridItems.length === 0)
            return false
        // 从鼠标停留的位置继承，而不是每次都从第 0 格开始
        if (root.hoverIndex >= 0 && root.hoverIndex < Ctrl.gridItems.length)
            grid.currentIndex = root.hoverIndex
        else if (grid.currentIndex < 0 || grid.currentIndex >= Ctrl.gridItems.length)
            grid.currentIndex = 0
        root.keyboardMode = true
        grid.forceActiveFocus()
        grid.positionViewAtIndex(grid.currentIndex, GridView.Contain)
        return true
    }

    function focusDiskResults() {
        if (Ctrl.searchResults.length === 0)
            return false
        if (hitList.currentIndex < 0 || hitList.currentIndex >= Ctrl.searchResults.length)
            hitList.currentIndex = 0
        hitList.forceActiveFocus()
        hitList.positionViewAtIndex(hitList.currentIndex, ListView.Contain)
        return true
    }

    // 启动网格里键盘选中的那一格；文件夹则进去（进去后把选中项重新落到第 0 个）
    function activateGridItem() {
        const items = Ctrl.gridItems
        const i = grid.currentIndex
        if (i < 0 || i >= items.length)
            return
        const entry = items[i]
        if (entry.isFolder) {
            root.openFolder(entry.groupKey)
            grid.currentIndex = 0
            Qt.callLater(root.focusGrid)
        } else {
            root.launchRequested(entry.item)
        }
    }

    function activateDiskItem() {
        const hits = Ctrl.searchResults
        const i = hitList.currentIndex
        if (i < 0 || i >= hits.length)
            return
        root.launchRequested(hits[i])
    }

    /*
     * 面板每次弹出都从「干净状态」开始：清掉搜索词、分类回到「全部」。
     * 不清的话，上次留下的搜索词会让面板一打开就只剩几个图标，看起来就像
     * 「什么都没有了」——用户很难意识到是搜索词还在。
     */
    function resetFilters() {
        // 只改「值」，不主动重算：上面的 setter 在值真的变了时才触发重算，
        // 值没变（面板上次关掉时就是干净状态，最常见）就一次计算都不做 ——
        // 数据一直在常驻进程里，没必要每次弹面板都重读一遍。
        Ctrl.query = ""
        AppConfig.appCategory = "all"
        // 进过文件夹的话也回到顶层（Ctrl.closeGroup 在顶层时是空操作）
        Ctrl.closeGroup()
        // 键盘选中也归零，否则下次 ↓ 会落在上次那个位置（看起来像跳了一格）
        grid.currentIndex = 0
        hitList.currentIndex = 0
    }

    /*
     * 取一个网格条目的「首字母」，给滚动位置气泡用。
     *
     * 跳过前面的空白/符号（很多开始菜单项名字以空格或 · 打头），
     * 数字统一归成 "#"（跟通讯录一个习惯：数字排在字母前面）。
     * 中文等非 ASCII 字符直接原样返回 —— 拼音首字母映射不值得为它引一张表。
     */
    function initialOf(entry) {
        const name = (entry && entry.label) ? String(entry.label) : ""
        for (let i = 0; i < name.length; ++i) {
            const c = name.charAt(i)
            if (c >= "0" && c <= "9")
                return "#"
            if ((c >= "A" && c <= "Z") || (c >= "a" && c <= "z"))
                return c.toUpperCase()
            if (c.charCodeAt(0) > 127)
                return c
        }
        return "#"
    }

    GlassPanel {
        anchors.fill: parent
        glassReady: root.glassReady
        cornerRadius: root.cornerRadius
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        anchors.topMargin: 14
        anchors.bottomMargin: 10
        spacing: 10

        // ---- 顶部：搜索栏 + 操作 ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            SearchBar {
                id: searchBar
                Layout.fillWidth: true
                /*
                 * 两个方向：
                 *   - text: Ctrl.query —— 外部复位（面板重开清空）回填输入框；
                 *   - onTextEdited —— 用户敲字/清空上报给 Ctrl.query。
                 * 之前只有「onTextChanged 往上报」一个方向，复位传不进来；
                 * 现在用信号 + 绑定，谁也不会打断谁。
                 */
                text: Ctrl.query
                onTextEdited: (value) => Ctrl.query = value
                onEscaped: {
                    // 先退一层：文件夹里按 Esc 回顶层，顶层再按才收起面板
                    if (Ctrl.openGroup.length > 0)
                        Ctrl.closeGroup()
                    else
                        root.closeRequested()
                }
                onAccepted: {
                    /*
                     * 回车 = 直接启动第一项，不用先用 ↓ 选中。
                     * 两层结果：网格（应用/文件夹）优先，网格为空时用全盘命中
                     * —— 默认只在「应用」档查本地，搜 pdf/xlsx 时网格是空的，
                     * 以前这种时候回车毫无反应，只能去点下面那排图标。
                     */
                    grid.currentIndex = 0
                    hitList.currentIndex = 0
                    if (Ctrl.gridItems.length > 0)
                        root.activateGridItem()
                    else if (Ctrl.searchResults.length > 0)
                        root.activateDiskItem()
                }
                onTabForward: root.focusCategories()
            }

            PillButton {
                text: qsTr("编辑")
                onClicked: root.editRequested()
            }

            PillButton {
                text: qsTr("设置")
                onClicked: root.settingsRequested()
            }
        }

        // ---- 分类标签（对齐 macOS Applications 面板那一排）----
        CategoryBar {
            id: catBar
            Layout.fillWidth: true
            onMoveUp: root.focusSearch()
            onMoveDown: {
                // 分类条往下 = 网格；搜索态下网格可能为空，那就落全盘结果
                if (!root.focusGrid())
                    root.focusDiskResults()
            }
        }

        // ---- 面包屑：进了文件夹才出现 ----
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 26 : 0
            spacing: 8
            visible: Ctrl.openGroup.length > 0

            // 「‹ 全部应用」：一键回顶层。Ctrl.closeGroup 只改列表，
            // 顺带把分类筛选留在原处（顶部分类标签会说明当前筛的是什么）
            Rectangle {
                implicitWidth: backLabel.implicitWidth + 20
                implicitHeight: 24
                radius: 12
                color: backArea.containsMouse ? Theme.stateHover : Theme.stateIdle

                Behavior on color {
                    ColorAnimation { duration: Theme.durFast }
                }

                Text {
                    id: backLabel
                    anchors.centerIn: parent
                    text: qsTr("‹ 全部应用")
                    color: Theme.textHi
                    font.pixelSize: Theme.fsBody
                }

                MouseArea {
                    id: backArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Ctrl.closeGroup()
                }
            }

            Text {
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: Ctrl.openGroupLabel
                color: Theme.textHi
                font.pixelSize: Theme.fsLabel
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }

            Text {
                text: qsTr("%1 个").arg(Ctrl.tileCount)
                color: Theme.textMid
                font.pixelSize: Theme.fsSmall
                verticalAlignment: Text.AlignVCenter
            }
        }

        /*
         * ---- 中部：7 × 4 的图标网格（自由纵向滚动 + 可拖动滚动条）----
         *
         * 外面这层 Item 只是给网格、滚动条、字母气泡一个**共同坐标系**。
         * 为什么不直接塞进上面的 ColumnLayout：ColumnLayout 会把每个直接子项
         * 当成一个布局条目排一行，浮层进去会被算成占位的一行，网格会被挤扁。
         */
        Item {
            id: gridWrap
            Layout.fillWidth: true
            Layout.fillHeight: true
            /*
             * 试过「只搜到全盘文件时把网格整块 visible: false」收掉空白 ——
             * 观感是错的：ColumnLayout 在没有 fillHeight 项时会把剩余高度
             * 摊到每个子项上，搜索栏、分类条全被拉低（实测 search bar 下移 47px、
             * 分类条下移 147px）。宁可留空白，也别动这个布局。
             */

            // 滚动条贴在最右边，GridView 不需要再留右边距：格宽约 140、
            // 图标 57 居中，最后一列的图标离右边缘还有 50 多 px，
            // 14px 的滚动条压不到图标上。
            readonly property bool canScroll: grid.contentHeight > grid.height + 1

            // 正在滚：手拖内容 / 惯性滑动 / 按住滚动条手柄，三者任一
            readonly property bool scrolling: grid.moving || grid.flicking || vbar.pressed

            /*
             * 当前顶边所在的条目下标。
             *
             * GridView 没有 atY()，只有 content 坐标的 indexAt(x, y)，而且点
             * 落在格子之间的空隙里会返回 -1。所以从顶边往下每 4px 探一次、
             * 取第一个命中的（内容正好停在行边界时，第一探可能正好压空）。
             * 都探不到就退回 0，气泡显示首项而不是空着。
             */
            readonly property int topIndex: {
                for (let dy = 1; dy <= grid.cellHeight; dy += 4) {
                    const i = grid.indexAt(1, grid.contentY + dy)
                    if (i >= 0)
                        return i
                }
                return 0
            }

            /*
             * 当前可见首行的首字母。
             * 直接读 label —— GroupTile 和应用两种条目的 label 都是「显示名」，
             * 不用再按 isFolder 分支。
             */
            readonly property string topLetter: {
                const items = Ctrl.gridItems
                const i = topIndex
                if (!items || i < 0 || i >= items.length)
                    return ""
                return root.initialOf(items[i])
            }

            // 气泡不是每次滚动都亮灭，而是「开始滚 → 亮，停手 0.9s → 灭」
            property bool letterVisible: false

            onScrollingChanged: {
                if (scrolling) {
                    letterDwell.stop()
                    letterVisible = true
                } else {
                    letterDwell.restart()
                }
            }

            Timer {
                id: letterDwell
                interval: 900
                onTriggered: gridWrap.letterVisible = false
            }

            GridView {
                id: grid
                objectName: "appGrid"   // --dump-icons 靠它打格子几何
                anchors.fill: parent

                clip: true
                boundsBehavior: Flickable.StopAtBounds

                /*
                 * 键盘焦点：只在用户按 ↓ 从搜索框跳下来时给（见 focusGrid）。
                 * 平时保持 false，否则面板弹出时 Ctrl+F 之类的焦点会在网格上，
                 * 搜索框反而敲不进字。
                 */
                focus: false

                Keys.onReturnPressed: root.activateGridItem()
                Keys.onEnterPressed: root.activateGridItem()
                // Tab 继续循环：网格 → 搜索框；Shift+Tab 反向回分类条
                // （方向键只负责网格内部移动，↑ 在首行就停在首行、不跨区）
                Keys.onTabPressed: (event) => {
                    root.focusSearch()
                    event.accepted = true
                }
                Keys.onBacktabPressed: (event) => {
                    root.focusCategories()
                    event.accepted = true
                }

                /*
                 * 鼠标接管键盘后重新用键盘：网格焦点一直没放（Tab 进来后），
                 * 鼠标悬停只翻了 keyboardMode=false，于是方向键被 GridView 内部静默
                 * 吃掉、高亮又不亮，表现为「键盘没反应」。这里在网格自己的按键里
                 * 重新进入键盘模式：从鼠标悬停位置继承 currentIndex，再让 GridView
                 * 按方向移一格（事件不吞，GridView 的默认移动照常发生）。
                 * 已在键盘模式则只置位、不重置 currentIndex，保证连续导航不跳。
                 */
                Keys.onPressed: (event) => {
                    if (event.key !== Qt.Key_Left && event.key !== Qt.Key_Right &&
                        event.key !== Qt.Key_Up && event.key !== Qt.Key_Down)
                        return
                    if (!root.keyboardMode && root.hoverIndex >= 0) {
                        const n = Ctrl.gridItems.length
                        grid.currentIndex = Math.max(0, Math.min(n - 1, root.hoverIndex))
                    }
                    root.keyboardMode = true
                }

                cellWidth: width / root.columns
                // 行高取容器高度的 1/4（向下取整）：静止时一屏正好 4 行，
                // 滚动本身是自由的（不做吸附、不锁位置）。取整是为了让 4 行严格
                // 不超过容器高度，否则第 5 行会露出半截。
                cellHeight: Math.floor(height / root.visibleRows)

                // 网格 model 是「应用 + 文件夹」的混合列表（Ctrl.gridItems）：
                // 文件夹是同一来源的一堆入口折叠成的一格（J-Link 一家 44 个图标）
                model: Ctrl.gridItems

            /*
             * 一格两种可能：应用瓦片 / 文件夹瓦片，按 modelData.isFolder 二选一显示。
             *
             * 这里**不能**用 Loader + 具名 Component：QML 的 Component 是独立作用域，
             * 里面的代码看不到 delegate 的隐式 modelData，也看不到外层的 id
             * （实测报 "ReferenceError: tileLoader is not defined" 然后整格空白）。
             * 两个都放着、用 visible 切换，虽然多养一个不可见的 item，
             * 但作用域直接就是 delegate 自己，没有坑。
             *
             * ★ 尺寸必须显式给：这个 delegate 根 Item 拿到的是 0×0
             *   （实测 --dump-icons 的可视树里 `appTile[0x0]`），
             *   `anchors.fill: parent` 于是把 0 传给了瓦片，后果是三连：
             *     - iconSize 里 width*0.44 / height*0.42 全是 0 → 掉到 Math.max(32,…)
             *       下限，图标只有 32px（本该 57px）
             *     - tileLabel.width = parent.width - 16 = -16 → 负宽 Text 整行不渲染
             *     - iconBox 以 parent.horizontalCenter(=0，即每格左边界)为圆心，
             *       图标整体偏左半个图标宽
             *   所以这里不用 implicitWidth，直接绑 cell 尺寸。
             */
            delegate: Item {
                width: grid.cellWidth
                height: grid.cellHeight

                /*
                 * ★ isCurrent 必须在 delegate 根 Item 上取，再传到子组件。
                 * GridView.isCurrentItem 这个附加属性只在 delegate 根可靠；
                 * 直接写进嵌套的 AppTile / FolderTile 组件里经常不刷新，
                 * 表现就是「方向键能移动选中、但高亮永远不亮」（鼠标 hover 正常，
                 * 因为它的高亮不靠这个属性）。
                 */
                readonly property bool isCurr: GridView.isCurrentItem

                AppTile {
                    width: parent.width
                    height: parent.height
                    visible: !(modelData && modelData.isFolder)
                    app: modelData ? modelData.item : null
                    // 键盘选中：只在键盘模式（keyboardMode）下才亮，鼠标 hover 时不算。
                    // 用 keyboardMode 而非 grid.activeFocus —— Tab 跳出网格后 activeFocus
                    // 可能残留，会导致旧选中项还亮着。
                    keySelected: root.keyboardMode && isCurr
                    // 鼠标模式标志（透传 keyboardMode）+ 自己在网格中的下标
                    keyboardActive: root.keyboardMode
                    tileIndex: index
                    onTileEntered: (i) => { root.hoverIndex = i; root.keyboardMode = false }
                    onTileExited: (i) => { if (root.hoverIndex === i) root.hoverIndex = -1 }
                    onActivated: root.launchRequested(app)
                    onAdminRequested: Ctrl.runAsAdmin(app)
                    onRevealRequested: Ctrl.revealInExplorer(app)
                    onUninstallRequested: Ctrl.uninstallApp(app)
                    onRemoveRequested: Ctrl.hideApp(app)
                }

                FolderTile {
                    width: parent.width
                    height: parent.height
                    visible: modelData && modelData.isFolder
                    folder: modelData
                    keySelected: root.keyboardMode && isCurr
                    keyboardActive: root.keyboardMode
                    tileIndex: index
                    onTileEntered: (i) => { root.hoverIndex = i; root.keyboardMode = false }
                    onTileExited: (i) => { if (root.hoverIndex === i) root.hoverIndex = -1 }
                    onActivated: root.openFolder(modelData.groupKey)
                }
            }

            /*
             * 空网格的提示。三种情况要分开说：
             *   1. 有搜索词 + 下面那节已经有全盘命中 → 不写字（结果就在下面）
             *   2. 有搜索词 + 全盘也没找到 → 「全盘没有找到」
             *   3. 没搜索词 → 应用档说「没有可显示的」，文件档提示敲关键词
             */
            Text {
                anchors.centerIn: parent
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.textMid
                font.pixelSize: Theme.fsLabel
                text: Ctrl.scanning
                      ? qsTr("正在扫描已安装应用…")
                      : (Ctrl.query.length > 0
                         ? (Ctrl.searchResults.length > 0
                            ? ""
                            : (Ctrl.diskSearch
                               ? qsTr("全盘没有找到「%1」").arg(Ctrl.query)
                               : qsTr("没有匹配「%1」的%2").arg(Ctrl.query).arg(root.categoryLabel)))
                         : (Ctrl.diskSearch
                            ? qsTr("输入关键词后从全盘搜索%1").arg(root.categoryLabel)
                            : qsTr("没有可显示的%1").arg(root.categoryLabel)))
                visible: Ctrl.gridItems.length === 0 && text.length > 0
            }

            /*
             * 可拖动的滚动条。
             *
             * 用 attached 写法（ScrollBar.vertical）而不是手写矩形 + MouseArea：
             * 手柄的 size / position 由 Flickable 自己算，按住拖动、点轨道翻页
             * 也都是现成的，手写反而容易在「内容不足一屏」时算出负高度。
             *
             * 手柄宽度走 contentItem.implicitWidth（Qt 就是这么定手柄粗细的），
             * ScrollBar 自身的 implicitWidth 则是**命中区**，比手柄宽一倍，
             * 免得 5px 的条子要瞄半天。
             *
             * 常显（AlwaysOn），但内容不满一屏时整条藏掉 —— 搜索到 1 个结果时
             * 还戳着一根不能动的条子很怪。
             */
            ScrollBar.vertical: ScrollBar {
                id: vbar
                policy: ScrollBar.AlwaysOn
                implicitWidth: 14
                visible: gridWrap.canScroll

                contentItem: Rectangle {
                    implicitWidth: vbar.pressed ? 8 : (vbar.hovered ? 7 : 6)
                    radius: implicitWidth / 2
                    color: vbar.pressed ? Theme.scrollThumbActive
                                        : (vbar.hovered ? Theme.scrollThumbHover
                                                        : Theme.scrollThumb)

                    Behavior on implicitWidth {
                        NumberAnimation { duration: Theme.durFast }
                    }

                    Behavior on color {
                        ColorAnimation { duration: Theme.durFast }
                    }
                }

                // 轨道不画底：玻璃面板上多一条竖底会把右边的留白切得很碎
                background: null
            }
        }

        /*
         * 滚动位置字母气泡。
         *
         * 网格是 7 列，一屏 28 个，滚起来很容易"迷失"——所以滚动/拖动时在
         * 右端浮出当前首行的首字母（数字开头统一显示 #），停手 0.9s 后淡出。
         * 它是**只读指示器**：不做点击跳转，避免和滚动条抢那一小条热区。
         */
        Rectangle {
            id: letterBadge
            width: 42
            height: 50
            radius: Theme.rXl
            color: Theme.badgeBg
            // 贴着滚动条内侧，垂直居中
            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter

            opacity: gridWrap.letterVisible && gridWrap.canScroll ? 1 : 0
            visible: opacity > 0.01

            Behavior on opacity {
                NumberAnimation { duration: Theme.durSlow }
            }

            Text {
                anchors.centerIn: parent
                text: gridWrap.topLetter
                color: Theme.badgeText
                font.pixelSize: Theme.fsBig
                font.weight: Font.DemiBold
            }
        }
    }

        // ---- Everything 全盘搜索结果 ----
        ColumnLayout {
            id: everythingRow
            Layout.fillWidth: true
            spacing: 4
            visible: Ctrl.searchResults.length > 0

            Text {
                text: qsTr("全盘搜索（Everything）· %1 个").arg(Ctrl.searchResults.length)
                color: Theme.textMid
                font.pixelSize: Theme.fsSmall
            }

            /*
             * 全盘命中的多半是普通文件（pdf / xlsx / docx…），图标全是同一张
             * 文件类型图标 —— 只给图标等于没给信息，所以这里必须带文件名。
             * 名字一行、中间省略（扩展名留在尾巴上，比首省略有用），
             * 鼠标停留再给完整路径。
             */
            ListView {
                id: hitList
                Layout.fillWidth: true
                height: 64
                orientation: ListView.Horizontal
                spacing: 4
                clip: true
                model: Ctrl.searchResults

                // 同网格：默认不给焦点，按 Tab 时由 focusSearch 接管轮回
                focus: false
                Keys.onReturnPressed: root.activateDiskItem()
                Keys.onEnterPressed: root.activateDiskItem()
                Keys.onTabPressed: (event) => {
                    root.focusSearch()
                    event.accepted = true
                }

                delegate: Rectangle {
                    // 键盘选中项（横向列表用 ←→ 走）
                    readonly property bool keyCurrent: hitList.activeFocus && ListView.isCurrentItem
                    // 与网格同理：键盘模式（列表有焦点）时 hover 让位，避免双高亮
                    readonly property bool showHover: hitArea.containsMouse && !hitList.activeFocus

                    width: 108
                    height: 64
                    radius: Theme.rMd
                    color: keyCurrent || showHover ? Theme.stateHover : "transparent"
                    border.width: keyCurrent ? 1 : 0
                    border.color: Theme.focusRing

                    Image {
                        id: hitIcon
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 4
                        width: 32
                        height: 32
                        source: modelData ? modelData.iconUrl : ""
                        sourceSize: Qt.size(64, 64)
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: hitIcon.bottom
                        anchors.topMargin: 3
                        width: parent.width - 8
                        text: modelData ? modelData.name : ""
                        color: Theme.textMid
                        font.pixelSize: Theme.fsTiny
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideMiddle
                    }

                    MouseArea {
                        id: hitArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.launchRequested(modelData)
                    }

                    ToolTip.visible: hitArea.containsMouse
                    ToolTip.text: modelData ? modelData.target : ""
                    ToolTip.delay: 500
                }
            }
        }

        // ---- 底部状态行 ----
        RowLayout {
            id: statusRow
            Layout.fillWidth: true
            spacing: 10

            Text {
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: Ctrl.errorText.length > 0 ? Theme.err : Theme.textMid
                font.pixelSize: Theme.fsSmall
                text: Ctrl.errorText.length > 0 ? Ctrl.errorText : Ctrl.statusText
            }

            BusyIndicator {
                visible: Ctrl.scanning
                running: Ctrl.scanning
                implicitWidth: 14
                implicitHeight: 14
            }

            Text {
                color: Theme.textMid
                font.pixelSize: Theme.fsSmall
                /*
                 * 左边那行已经在说「共 N 个应用」了，这里只在**筛选生效**时
                 * 补一个「命中 / 总数」。没筛选就整个收起 —— 否则左右两处
                 * 都在报同一个数字，看着像出了什么问题。
                 */
                visible: text.length > 0
                text: Ctrl.scanning
                      ? qsTr("扫描 %1 / %2").arg(Ctrl.scanProgress).arg(Ctrl.scanTotal)
                      : (Ctrl.query.length > 0 || AppConfig.appCategory !== "all"
                         ? qsTr("%1 / %2").arg(Ctrl.tileCount).arg(Ctrl.tileTotal)
                         : "")
            }

            Text {
                color: Theme.textMid
                font.pixelSize: Theme.fsSmall
                text: AppConfig.hotkey
            }
        }
    }
}
