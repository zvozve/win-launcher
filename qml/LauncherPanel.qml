import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import WinLauncher.Core

/*
 * 弹出面板主体：搜索栏 + 图标网格 + 状态行。
 */
Item {
    id: root

    signal launchRequested(var item)
    signal editRequested()
    signal closeRequested()

    readonly property bool dark: AppConfig.darkMode
    readonly property color textColor: dark ? "#F2F2F7" : "#1C1C1E"
    readonly property color subTextColor: dark ? "#9A9AA0" : "#6E6E73"

    function focusSearch() {
        searchBar.focusInput()
    }

    GlassPanel {
        anchors.fill: parent
        dark: root.dark
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.topMargin: 16
        anchors.bottomMargin: 12
        spacing: 12

        // ---- 顶部：搜索栏 + 操作 ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

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
                text: qsTr("关闭")
                dark: root.dark
                textColor: root.textColor
                onClicked: root.closeRequested()
            }
        }

        // ---- 中部：图标网格 ----
        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true

            clip: true
            boundsBehavior: Flickable.StopAtBounds
            cellWidth: Math.max(64, Math.floor(width / Math.max(1, AppConfig.gridColumns)))
            cellHeight: cellWidth + 26

            model: Ctrl.visibleApps

            delegate: AppTile {
                width: grid.cellWidth
                height: grid.cellHeight
                dark: root.dark
                textColor: root.textColor
                app: modelData
                onActivated: root.launchRequested(app)
            }

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

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
                         ? qsTr("没有匹配「%1」的应用").arg(Ctrl.query)
                         : qsTr("没有可显示的应用"))
            }
        }

        // ---- Everything 全盘搜索结果（可选） ----
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: Ctrl.searchResults.length > 0

            Text {
                text: qsTr("全盘搜索（Everything）")
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
                           ? (root.dark ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.07))
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
                    ToolTip.delay: 600
                }
            }
        }

        // ---- 底部状态行 ----
        RowLayout {
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
