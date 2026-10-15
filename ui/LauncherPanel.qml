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

    readonly property int categoryIndex: Math.max(0, Ctrl.searchCategoryKeys.indexOf(
                                                      AppConfig.searchCategory))
    readonly property string categoryLabel: Ctrl.searchCategoryLabels[categoryIndex]

    function focusSearch() {
        searchBar.focusInput()
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
                    // 回车打开第一个格子的东西：是应用就启动，是文件夹就进去
                    const first = Ctrl.gridItems.length > 0 ? Ctrl.gridItems[0] : null
                    if (!first)
                        return
                    if (first.isFolder)
                        Ctrl.openGroup(first.groupKey)
                    else
                        root.launchRequested(first.item)
                }
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
            Layout.fillWidth: true
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
                text: qsTr("%1 个").arg(Ctrl.matchCount)
                color: Theme.textMid
                font.pixelSize: Theme.fsSmall
                verticalAlignment: Text.AlignVCenter
            }
        }

        // ---- 中部：7 × 4 的图标网格（自由纵向滚动）----
        GridView {
            id: grid
            objectName: "appGrid"   // --dump-icons 靠它打格子几何
            Layout.fillWidth: true
            Layout.fillHeight: true

            clip: true
            boundsBehavior: Flickable.StopAtBounds

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

                AppTile {
                    width: parent.width
                    height: parent.height
                    visible: !(modelData && modelData.isFolder)
                    app: modelData ? modelData.item : null
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
                    onActivated: Ctrl.openGroup(modelData.groupKey)
                }
            }

            // 不挂 ScrollBar：macOS 那边没有滚动条，靠滚轮/拖动

            Text {
                anchors.centerIn: parent
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: Ctrl.gridItems.length === 0
                color: Theme.textMid
                font.pixelSize: Theme.fsLabel
                text: Ctrl.scanning
                      ? qsTr("正在扫描已安装应用…")
                      : (Ctrl.query.length > 0
                         ? qsTr("没有匹配「%1」的%2").arg(Ctrl.query).arg(root.categoryLabel)
                         : (AppConfig.searchSource === "everything"
                            ? qsTr("输入关键词后从全盘搜索%1").arg(root.categoryLabel)
                            : qsTr("没有可显示的%1，试试切到「全盘」").arg(root.categoryLabel)))
            }
        }

        // ---- Everything 全盘搜索结果 ----
        ColumnLayout {
            id: everythingRow
            Layout.fillWidth: true
            spacing: 4
            visible: Ctrl.searchResults.length > 0

            Text {
                text: qsTr("全盘搜索（Everything）· %1").arg(root.categoryLabel)
                color: Theme.textMid
                font.pixelSize: Theme.fsSmall
            }

            ListView {
                Layout.fillWidth: true
                height: 46
                orientation: ListView.Horizontal
                spacing: 6
                clip: true
                model: Ctrl.searchResults

                delegate: Rectangle {
                    width: 46
                    height: 46
                    radius: Theme.rLg
                    color: hitArea.containsMouse ? Theme.stateHover : "transparent"

                    Image {
                        anchors.centerIn: parent
                        width: 32
                        height: 32
                        source: modelData ? modelData.iconUrl : ""
                        sourceSize: Qt.size(64, 64)
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
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
                         ? qsTr("%1 / %2").arg(Ctrl.matchCount).arg(Ctrl.totalCount)
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
