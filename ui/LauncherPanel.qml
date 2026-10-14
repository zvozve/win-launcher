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
 */
Item {
    id: root

    signal launchRequested(var item)
    signal editRequested()
    signal settingsRequested()
    signal closeRequested()

    // 圆角半径必须和 LauncherWindow.cornerRadius 一致（底图按这个半径裁的）
    property real cornerRadius: 26
    // C++ 烘的玻璃底图是否已就绪；没就绪时 GlassPanel 走不透明兜底
    property bool glassReady: false

    // 一屏可见的网格密度（对齐 macOS：7 列 × 4 行）
    readonly property int visibleRows: 4
    readonly property int columns: Math.max(3, AppConfig.gridColumns)

    readonly property bool dark: AppConfig.darkMode
    readonly property color textColor: dark ? "#F2F2F7" : "#1C1C1E"
    readonly property color subTextColor: dark ? "#9A9AA0" : "#6E6E73"

    readonly property int categoryIndex: Math.max(0, Ctrl.searchCategoryKeys.indexOf(
                                                      AppConfig.searchCategory))
    readonly property string categoryLabel: Ctrl.searchCategoryLabels[categoryIndex]

    function focusSearch() {
        searchBar.focusInput()
    }

    GlassPanel {
        anchors.fill: parent
        dark: root.dark
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
                dark: root.dark
                textColor: root.textColor
                hintColor: root.subTextColor
                onTextChanged: Ctrl.query = text
                onAccepted: {
                    if (Ctrl.visibleApps.length > 0)
                        root.launchRequested(Ctrl.visibleApps[0])
                }
            }

            PillButton {
                text: qsTr("编辑")
                dark: root.dark
                textColor: root.textColor
                onClicked: root.editRequested()
            }

            PillButton {
                text: qsTr("设置")
                dark: root.dark
                textColor: root.textColor
                onClicked: root.settingsRequested()
            }
        }

        // ---- 分类标签（对齐 macOS Applications 面板那一排）----
        CategoryBar {
            Layout.fillWidth: true
            dark: root.dark
            textColor: root.textColor
        }

        // ---- 中部：7 × 4 的图标网格（自由纵向滚动）----
        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true

            clip: true
            boundsBehavior: Flickable.StopAtBounds

            cellWidth: width / root.columns
            // 行高取容器高度的 1/4（向下取整）：静止时一屏正好 4 行，
            // 滚动本身是自由的（不做吸附、不锁位置）。取整是为了让 4 行严格
            // 不超过容器高度，否则第 5 行会露出半截。
            cellHeight: Math.floor(height / root.visibleRows)

            model: Ctrl.visibleApps

            delegate: AppTile {
                width: grid.cellWidth
                height: grid.cellHeight
                dark: root.dark
                textColor: root.textColor
                app: modelData
                onActivated: root.launchRequested(app)
                onAdminRequested: Ctrl.runAsAdmin(app)
                onRevealRequested: Ctrl.revealInExplorer(app)
                onRemoveRequested: Ctrl.hideApp(app)
            }

            // 不挂 ScrollBar：macOS 那边没有滚动条，靠滚轮/拖动

            Text {
                anchors.centerIn: parent
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: Ctrl.visibleApps.length === 0
                color: root.subTextColor
                font.pixelSize: 13
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
                color: root.subTextColor
                font.pixelSize: 11
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
                    radius: 10
                    color: hitArea.containsMouse
                           ? (root.dark ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(0, 0, 0, 0.08))
                           : "transparent"

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
                color: Ctrl.errorText.length > 0 ? "#FF6B6B" : root.subTextColor
                font.pixelSize: 11
                text: Ctrl.errorText.length > 0 ? Ctrl.errorText : Ctrl.statusText
            }

            BusyIndicator {
                visible: Ctrl.scanning
                running: Ctrl.scanning
                implicitWidth: 14
                implicitHeight: 14
            }

            Text {
                color: root.subTextColor
                font.pixelSize: 11
                text: Ctrl.scanning
                      ? qsTr("扫描 %1 / %2").arg(Ctrl.scanProgress).arg(Ctrl.scanTotal)
                      : (Ctrl.query.length > 0
                         ? qsTr("%1 / %2").arg(Ctrl.matchCount).arg(Ctrl.totalCount)
                         : qsTr("%1 个应用").arg(Ctrl.totalCount))
            }

            Text {
                color: root.subTextColor
                font.pixelSize: 11
                text: AppConfig.hotkey
            }
        }
    }
}
