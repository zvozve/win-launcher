import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import WinLauncher.Core

/*
 * 设置窗口（独立、不透明）。
 *
 * 外观按 Win11「设置」应用做：分组用圆角卡片、行高 56、控件用 WinButton / WinSwitch；
 * 标题栏右侧是 Win11 那种 hover 变红的关闭键。
 * 只放设置项；图标增删在「编辑」窗口里，两者刻意分开 —— 混在一起太长。
 *
 * 但**配色不再单独一套**：改造前这里是 Win11 的 #202020 / #F3F3F3 / #0067C0，
 * 和面板的 macOS 系配色并排放着像两个软件。现在统一走 Theme，
 * 只保留 Win11 的几何形态（卡片圆角、行高、关闭键变红）。
 */
Window {
    id: root

    width: 500
    height: 700
    visible: false
    title: qsTr("WinLauncher 设置")
    color: Theme.bgWindow
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.NoDropShadowWindowHint

    function openWindow() {
        visible = true
        raise()
        requestActivate()
    }

    // frameless 窗口没有系统圆角，自己裁一个 8px 的（Win11 的窗口圆角就是 8）
    onVisibleChanged: if (visible) Ctrl.roundWindow(root, 8)
    onWidthChanged: if (visible) Ctrl.roundWindow(root, 8)
    onHeightChanged: if (visible) Ctrl.roundWindow(root, 8)

    // ================= 内联组件 =================

    // 分组卡片：子项直接写进里面即可
    component Card: Rectangle {
        default property alias content: inner.data

        implicitHeight: inner.implicitHeight + 2
        radius: Theme.rMd
        color: Theme.bgCard
        border.width: 1
        border.color: Theme.border

        ColumnLayout {
            id: inner
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 1
            spacing: 0
        }
    }

    // 卡片内的行分隔线：内缩 20，和 SettingRow 的左右内边距对齐
    component Sep: Rectangle {
        Layout.fillWidth: true
        Layout.leftMargin: 20
        Layout.rightMargin: 20
        Layout.preferredHeight: 1
        color: Theme.divider
    }

    // 分组标题：上方留 18（和上一组拉开），下方留 8（贴近自己的卡片），左右 26
    // —— Win11 设置里标题是「属于下一张卡片」的，所以要往卡片靠。
    //
    // 字号和行标签**同号**（fsTitle 14），只靠 DemiBold 区分层级。
    // 改造前这里用 fsBody 12，比行标签的 14 还小 —— 层级倒挂，
    // 一屏里大字号全是行名、小字号反而是标题，看着就不统一。
    component GroupTitle: Text {
        Layout.leftMargin: 26
        Layout.rightMargin: 26
        Layout.fillWidth: true
        Layout.topMargin: 18
        Layout.bottomMargin: 8
        color: Theme.textMid
        font.pixelSize: Theme.fsTitle
        font.weight: Font.DemiBold
    }

    // Win11 开关见 WinSwitch.qml（独立组件，配色只有 Theme 一个来源）

    // ================= 界面 =================

    Rectangle {
        anchors.fill: parent
        color: Theme.bgWindow
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- 标题栏 ----
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
                color: Theme.textHi
                font.pixelSize: Theme.fsTitle
                font.weight: Font.DemiBold
            }

            // Win11 的关闭键：整块 46×44 的矩形，hover 变红
            Rectangle {
                width: 46
                height: parent.height
                anchors.right: parent.right
                color: closeArea.containsMouse ? Theme.closeHover : "transparent"

                Behavior on color {
                    ColorAnimation { duration: Theme.durFast }
                }

                Text {
                    anchors.centerIn: parent
                    text: "\u2715"
                    font.pixelSize: Theme.fsLabel
                    color: closeArea.containsMouse ? "#FFFFFF" : Theme.textHi
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

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                // Win11 设置里卡片之间是松的（10px），卡片内是紧的（由 Sep 控制）。
                // 注意 GroupTitle 自己带了上下 margin，所以这里的 spacing 只是兜底。
                spacing: 10

                Item { Layout.preferredHeight: 6 }

                // ---- 启动与唤起 ----
                GroupTitle { text: qsTr("启动与唤起") }

                Card {
                    Layout.leftMargin: 26
                    Layout.rightMargin: 26
                    Layout.fillWidth: true

                    SettingRow {
                        label: qsTr("全局热键")
                        hint: qsTr("点击后按下组合键；Esc 取消")

                        HotkeyRecorder {}
                    }

                    Sep {}

                    SettingRow {
                        label: qsTr("开机自启")
                        hint: qsTr("写入 HKCU\\...\\Run，登录后静默常驻托盘")

                        WinSwitch {
                            checked: AppConfig.autostart
                            onToggled: AppConfig.autostart = !AppConfig.autostart
                        }
                    }

                    Sep {}

                    SettingRow {
                        label: qsTr("失去焦点自动收起")
                        hint: qsTr("关闭后面板会一直留在屏幕上")

                        WinSwitch {
                            checked: AppConfig.hideOnFocusOut
                            onToggled: AppConfig.hideOnFocusOut = !AppConfig.hideOnFocusOut
                        }
                    }
                }

                // ---- 搜索与外观 ----
                GroupTitle { text: qsTr("搜索与外观") }

                Card {
                    Layout.leftMargin: 26
                    Layout.rightMargin: 26
                    Layout.fillWidth: true

                    SettingRow {
                        label: qsTr("全盘搜索")
                        hint: Ctrl.everything.available
                              ? qsTr("已接入 Everything（%1）。搜索「文档/图片/视频/音乐/压缩包」时自动查全盘，词里写扩展名（如「报告 pdf」）也会查全盘。")
                                    .arg(Ctrl.everything.loadedPath)
                              : qsTr("未检测到 Everything64.dll，文件搜索只能靠本地过滤。运行 scripts\\fetch-everything-sdk.bat 可取回。")
                    }

                    Sep {}

                    SettingRow {
                        label: qsTr("每行图标数")
                        hint: qsTr("当前 %1 列 × 4 行").arg(AppConfig.gridColumns)

                        WinSpinBox {
                            from: 4
                            to: 16
                            value: AppConfig.gridColumns
                            onValueModified: AppConfig.gridColumns = value
                        }
                    }

                    Sep {}

                    SettingRow {
                        label: qsTr("归组阈值")
                        hint: qsTr("同一开始菜单文件夹里至少这么多条才折成一个文件夹")

                        WinSpinBox {
                            from: 2
                            to: 12
                            value: AppConfig.groupThreshold
                            onValueModified: AppConfig.groupThreshold = value
                        }
                    }
                }

                // ---- 扫描与图标 ----
                GroupTitle { text: qsTr("扫描与图标") }

                Card {
                    Layout.leftMargin: 26
                    Layout.rightMargin: 26
                    Layout.fillWidth: true

                    SettingRow {
                        label: qsTr("扫描注册表卸载项")
                        hint: qsTr("补上开始菜单里没有的免安装程序")

                        WinSwitch {
                            checked: AppConfig.includeRegistry
                            onToggled: {
                                AppConfig.includeRegistry = !AppConfig.includeRegistry
                                Ctrl.rescan()
                            }
                        }
                    }

                    Sep {}

                    SettingRow {
                        label: qsTr("已隐藏的图标")
                        hint: Ctrl.hiddenCount > 0
                              ? qsTr("从启动台移除了 %1 个").arg(Ctrl.hiddenCount)
                              : qsTr("在图标上点右键可以移除")

                        WinButton {
                            text: qsTr("全部恢复")
                            enabled: Ctrl.hiddenCount > 0
                            onClicked: Ctrl.unhideAll()
                        }
                    }
                }

                // ---- 操作按钮 ----
                RowLayout {
                    Layout.leftMargin: 26
                    Layout.rightMargin: 26
                    Layout.topMargin: 6
                    Layout.bottomMargin: 18
                    Layout.fillWidth: true
                    spacing: 8

                    WinButton {
                        text: qsTr("重新扫描")
                        onClicked: Ctrl.rescan()
                    }

                    WinButton {
                        text: qsTr("重建图标缓存")
                        onClicked: Ctrl.clearCacheAndRescan()
                    }

                    WinButton {
                        text: qsTr("打开配置目录")
                        onClicked: Ctrl.openConfigFolder()
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: qsTr("v%1").arg(Ctrl.appVersion)
                        color: Theme.textMid
                        font.pixelSize: Theme.fsBody
                    }
                }
            }
        }
    }
}
