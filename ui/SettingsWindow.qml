import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import WinLauncher.Core

/*
 * 设置窗口（独立、不透明）。
 * 只放设置项；图标增删在「编辑」窗口里，两者刻意分开 —— 混在一起太长。
 */
Window {
    id: root

    property bool dark: AppConfig.darkMode
    readonly property color textColor: dark ? "#F2F2F7" : "#1C1C1E"
    readonly property color subTextColor: dark ? "#9A9AA0" : "#6E6E73"
    readonly property color bgColor: dark ? "#232327" : "#F2F2F4"
    readonly property color lineColor: dark ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(0, 0, 0, 0.08)

    width: 440
    height: 610
    visible: false
    title: qsTr("WinLauncher 设置")
    color: bgColor
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.NoDropShadowWindowHint

    function openWindow() {
        visible = true
        raise()
        requestActivate()
    }

    Rectangle {
        anchors.fill: parent
        color: root.bgColor
        border.width: 1
        border.color: root.lineColor
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        // ---- 标题栏（可拖动）----
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
                text: qsTr("设置")
                color: root.textColor
                font.pixelSize: 14
                font.bold: true
            }

            Rectangle {
                width: 26
                height: 26
                radius: 7
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                color: closeArea.containsMouse
                       ? (root.dark ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(0, 0, 0, 0.09))
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
                    onClicked: root.visible = false
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: root.lineColor
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: 16

                SectionTitle {
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.topMargin: 14
                    Layout.fillWidth: true
                    text: qsTr("启动与唤起")
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

                Rectangle {
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: root.lineColor
                }

                SectionTitle {
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.fillWidth: true
                    text: qsTr("搜索与外观")
                    textColor: root.textColor
                    subTextColor: root.subTextColor
                }

                SettingRow {
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.fillWidth: true
                    label: qsTr("默认搜索源")
                    hint: Ctrl.everything.available
                          ? qsTr("Everything 已就绪：%1").arg(Ctrl.everything.loadedPath)
                          : qsTr("未检测到 Everything64.dll，仅本地过滤")
                    textColor: root.textColor
                    subTextColor: root.subTextColor

                    ComboBox {
                        implicitWidth: 130
                        model: [qsTr("本地过滤"), qsTr("全盘（Everything）")]
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
                    label: qsTr("每行图标数")
                    hint: qsTr("当前 %1 列").arg(AppConfig.gridColumns)
                    textColor: root.textColor
                    subTextColor: root.subTextColor

                    SpinBox {
                        from: 4
                        to: 16
                        value: AppConfig.gridColumns
                        onValueModified: AppConfig.gridColumns = value
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
                    label: qsTr("已隐藏的图标")
                    hint: Ctrl.hiddenCount > 0
                          ? qsTr("从启动台移除了 %1 个").arg(Ctrl.hiddenCount)
                          : qsTr("在图标上点右键可以移除")
                    textColor: root.textColor
                    subTextColor: root.subTextColor

                    PillButton {
                        text: qsTr("全部恢复")
                        dark: root.dark
                        textColor: root.textColor
                        enabled: Ctrl.hiddenCount > 0
                        opacity: enabled ? 1.0 : 0.45
                        onClicked: Ctrl.unhideAll()
                    }
                }

                Rectangle {
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: root.lineColor
                }

                RowLayout {
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.bottomMargin: 18
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

                    Text {
                        text: qsTr("v%1").arg(Ctrl.appVersion)
                        color: root.subTextColor
                        font.pixelSize: 11
                    }
                }
            }
        }
    }
}
