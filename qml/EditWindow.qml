import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import WinLauncher.Core

/*
 * 独立编辑窗口：增删图标 + 基础设置（热键 / 自启 / 搜索源 / 网格）。
 * 支持把 .lnk / .exe / .url 直接拖进来添加。
 */
Window {
    id: root

    property bool dark: AppConfig.darkMode
    readonly property color textColor: dark ? "#F2F2F7" : "#1C1C1E"
    readonly property color subTextColor: dark ? "#9A9AA0" : "#6E6E73"

    width: 580
    height: 640
    visible: false
    title: qsTr("WinLauncher 编辑")
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.NoDropShadowWindowHint

    function openWindow() {
        visible = true
        raise()
        requestActivate()
    }

    function closeWindow() {
        visible = false
    }

    GlassPanel {
        anchors.fill: parent
        dark: root.dark
        radius: 14
    }

    // ---- 拖放添加 ----
    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            if (drop.hasUrls)
                Ctrl.addPaths(drop.urls)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        // ---- 标题栏（可拖动） ----
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: "transparent"

            MouseArea {
                anchors.fill: parent
                onPressed: root.startSystemMove()
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("编辑图标")
                color: root.textColor
                font.pixelSize: 14
                font.bold: true
            }

            Text {
                anchors.right: closeBtn.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("把 .lnk / .exe / .url 拖进窗口即可添加")
                color: root.subTextColor
                font.pixelSize: 11
            }

            Rectangle {
                id: closeBtn
                width: 26
                height: 26
                radius: 7
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter

                color: closeArea.containsMouse
                       ? (root.dark ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(0, 0, 0, 0.1))
                       : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    font.pixelSize: 11
                    color: root.textColor
                }

                MouseArea {
                    id: closeArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.closeWindow()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: root.dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.06)
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: 14

                // ---- 应用列表 ----
                SectionTitle {
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.topMargin: 14
                    Layout.fillWidth: true
                    text: qsTr("应用（%1）").arg(Ctrl.totalCount)
                    subText: qsTr("手动添加的条目可删除，「来自系统扫描」的条目请到源程序卸载")
                    textColor: root.textColor
                    subTextColor: root.subTextColor
                }

                ListView {
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(260, Math.max(60, contentHeight))
                    clip: true
                    model: Ctrl.apps
                    spacing: 2
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 40
                        radius: 8
                        color: rowArea.containsMouse
                               ? (root.dark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0, 0, 0, 0.045))
                               : "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 10

                            Image {
                                Layout.preferredWidth: 22
                                Layout.preferredHeight: 22
                                source: modelData ? modelData.iconUrl : ""
                                sourceSize: Qt.size(44, 44)
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0

                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: modelData ? modelData.name : ""
                                    color: root.textColor
                                    font.pixelSize: 12
                                }

                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                    text: modelData && modelData.target.length > 0
                                          ? modelData.target
                                          : qsTr("（未解析到可执行目标）")
                                    color: root.subTextColor
                                    font.pixelSize: 10
                                }
                            }

                            Rectangle {
                                visible: modelData ? modelData.removable : false
                                Layout.preferredWidth: 50
                                Layout.preferredHeight: 24
                                radius: 6
                                color: removeArea.containsMouse
                                       ? Qt.rgba(1, 0.35, 0.35, 0.22)
                                       : (root.dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.05))

                                Text {
                                    anchors.centerIn: parent
                                    text: qsTr("移除")
                                    font.pixelSize: 11
                                    color: root.textColor
                                }

                                MouseArea {
                                    id: removeArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: Ctrl.removeApp(modelData.id)
                                }
                            }
                        }

                        MouseArea {
                            id: rowArea
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.NoButton
                        }
                    }
                }

                RowLayout {
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.fillWidth: true
                    spacing: 8

                    PillButton {
                        text: qsTr("重新扫描")
                        dark: root.dark
                        textColor: root.textColor
                        onClicked: Ctrl.rescan()
                    }

                    PillButton {
                        text: qsTr("重建图标缓存")
                        dark: root.dark
                        textColor: root.textColor
                        onClicked: Ctrl.clearCacheAndRescan()
                    }

                    PillButton {
                        text: qsTr("打开配置目录")
                        dark: root.dark
                        textColor: root.textColor
                        onClicked: Ctrl.openConfigFolder()
                    }

                    Item { Layout.fillWidth: true }
                }

                Rectangle {
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: root.dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.06)
                }

                // ---- 设置 ----
                SectionTitle {
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.fillWidth: true
                    text: qsTr("设置")
                    subText: qsTr("改动即时保存到 %1").arg(AppConfig.configPath)
                    textColor: root.textColor
                    subTextColor: root.subTextColor
                }

                SettingRow {
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.fillWidth: true
                    label: qsTr("全局热键")
                    hint: qsTr("点击后按下组合键；Esc 取消")
                    textColor: root.textColor
                    subTextColor: root.subTextColor

                    HotkeyRecorder {
                        dark: root.dark
                        textColor: root.textColor
                        subTextColor: root.subTextColor
                    }
                }

                SettingRow {
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.fillWidth: true
                    label: qsTr("开机自启")
                    hint: qsTr("写入 HKCU\\...\\Run，登录后静默常驻托盘")
                    textColor: root.textColor
                    subTextColor: root.subTextColor

                    Switch {
                        checked: AppConfig.autostart
                        onToggled: AppConfig.autostart = checked
                    }
                }

                SettingRow {
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.fillWidth: true
                    label: qsTr("失去焦点自动收起")
                    hint: qsTr("关闭后面板会一直留在屏幕上")
                    textColor: root.textColor
                    subTextColor: root.subTextColor

                    Switch {
                        checked: AppConfig.hideOnFocusOut
                        onToggled: AppConfig.hideOnFocusOut = checked
                    }
                }

                SettingRow {
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.fillWidth: true
                    label: qsTr("扫描注册表卸载项")
                    hint: qsTr("补上开始菜单里没有的免安装程序")
                    textColor: root.textColor
                    subTextColor: root.subTextColor

                    Switch {
                        checked: AppConfig.includeRegistry
                        onToggled: {
                            AppConfig.includeRegistry = checked
                            Ctrl.rescan()
                        }
                    }
                }

                SettingRow {
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.fillWidth: true
                    label: qsTr("搜索源")
                    hint: Ctrl.everything.available
                          ? qsTr("Everything 已就绪")
                          : qsTr("未检测到 Everything64.dll，仅本地过滤")
                    textColor: root.textColor
                    subTextColor: root.subTextColor

                    ComboBox {
                        implicitWidth: 130
                        model: [qsTr("本地过滤"), qsTr("Everything")]
                        currentIndex: AppConfig.searchSource === "everything" ? 1 : 0
                        onActivated: (index) => {
                            if (index === 1 && !Ctrl.everything.available) {
                                Ctrl.notifyError(qsTr("未检测到 Everything64.dll，仍使用本地过滤"))
                                currentIndex = 0
                                return
                            }
                            AppConfig.searchSource = index === 1 ? "everything" : "local"
                        }
                    }
                }

                SettingRow {
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.fillWidth: true
                    Layout.bottomMargin: 18
                    label: qsTr("每行图标数")
                    hint: qsTr("当前 %1 列").arg(AppConfig.gridColumns)
                    textColor: root.textColor
                    subTextColor: root.subTextColor

                    SpinBox {
                        from: 3
                        to: 12
                        value: AppConfig.gridColumns
                        onValueModified: AppConfig.gridColumns = value
                    }
                }
            }
        }
    }
}
