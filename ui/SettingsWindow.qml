import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import WinLauncher.Core

/*
 * 设置窗口（独立、不透明）。
 *
 * 外观按 Win11「设置」应用做：窗口底 #F3F3F3 / #202020，分组用 8px 圆角卡片，
 * 行高 56，控件用 WinButton / WinSwitch；标题栏右侧是 Win11 那种 hover 变红的关闭键。
 * 只放设置项；图标增删在「编辑」窗口里，两者刻意分开 —— 混在一起太长。
 */
Window {
    id: root

    property bool dark: AppConfig.darkMode

    // ---- Win11 配色 ----
    readonly property color winBg: dark ? "#202020" : "#F3F3F3"
    readonly property color cardBg: dark ? "#2B2B2B" : "#FBFBFB"
    readonly property color cardBorder: dark ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.06)
    readonly property color textColor: dark ? "#FFFFFF" : "#1A1A1A"
    readonly property color subTextColor: dark ? "#C5C5C5" : "#5D5D5D"
    readonly property color lineColor: dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.07)

    width: 500
    height: 700
    visible: false
    title: qsTr("WinLauncher 设置")
    color: winBg
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
        radius: 8
        color: root.cardBg
        border.width: 1
        border.color: root.cardBorder

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
        color: root.lineColor
    }

    // 分组标题：上方留 18（和上一组拉开），下方留 8（贴近自己的卡片），左右 26
    // —— Win11 设置里标题是「属于下一张卡片」的，所以要往卡片靠。
    component GroupTitle: Text {
        Layout.leftMargin: 26
        Layout.rightMargin: 26
        Layout.fillWidth: true
        Layout.topMargin: 18
        Layout.bottomMargin: 8
        color: root.subTextColor
        font.pixelSize: 12
        font.weight: Font.DemiBold
    }

    // Win11 开关：胶囊 + 滑块，开=强调色。
    // 只发信号不改自己的 checked —— 这样外部的绑定不会被内部赋值打断。
    component WinSwitch: Rectangle {
        property bool checked: false

        signal toggled()

        implicitWidth: 40
        implicitHeight: 20
        radius: height / 2
        color: checked ? (root.dark ? "#4CC2FF" : "#0067C0")
                       : (root.dark ? "#5A5A5A" : "#8A8A8A")

        Behavior on color {
            ColorAnimation { duration: 120 }
        }

        Rectangle {
            width: 12
            height: 12
            radius: 6
            anchors.verticalCenter: parent.verticalCenter
            x: parent.checked ? parent.width - width - 4 : 4
            color: parent.checked ? (root.dark ? "#000000" : "#FFFFFF") : "#FFFFFF"

            Behavior on x {
                NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.toggled()
        }
    }

    // ================= 界面 =================

    Rectangle {
        anchors.fill: parent
        color: root.winBg
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
                color: root.textColor
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }

            // Win11 的关闭键：整块 46×44 的矩形，hover 变红
            Rectangle {
                width: 46
                height: parent.height
                anchors.right: parent.right
                color: closeArea.containsMouse ? "#C42B1C" : "transparent"

                Behavior on color {
                    ColorAnimation { duration: 90 }
                }

                Text {
                    anchors.centerIn: parent
                    text: "\u2715"
                    font.pixelSize: 12
                    color: closeArea.containsMouse ? "#FFFFFF" : root.textColor
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
                        textColor: root.textColor
                        subTextColor: root.subTextColor

                        HotkeyRecorder {
                            dark: root.dark
                            textColor: root.textColor
                            subTextColor: root.subTextColor
                        }
                    }

                    Sep {}

                    SettingRow {
                        label: qsTr("开机自启")
                        hint: qsTr("写入 HKCU\\...\\Run，登录后静默常驻托盘")
                        textColor: root.textColor
                        subTextColor: root.subTextColor

                        WinSwitch {
                            checked: AppConfig.autostart
                            onToggled: AppConfig.autostart = !AppConfig.autostart
                        }
                    }

                    Sep {}

                    SettingRow {
                        label: qsTr("失去焦点自动收起")
                        hint: qsTr("关闭后面板会一直留在屏幕上")
                        textColor: root.textColor
                        subTextColor: root.subTextColor

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

                    Sep {}

                    SettingRow {
                        label: qsTr("每行图标数")
                        hint: qsTr("当前 %1 列 × 4 行").arg(AppConfig.gridColumns)
                        textColor: root.textColor
                        subTextColor: root.subTextColor

                        SpinBox {
                            from: 4
                            to: 16
                            value: AppConfig.gridColumns
                            onValueModified: AppConfig.gridColumns = value
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
                        textColor: root.textColor
                        subTextColor: root.subTextColor

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
                        textColor: root.textColor
                        subTextColor: root.subTextColor

                        WinButton {
                            text: qsTr("全部恢复")
                            dark: root.dark
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
                        dark: root.dark
                        onClicked: Ctrl.rescan()
                    }

                    WinButton {
                        text: qsTr("重建图标缓存")
                        dark: root.dark
                        onClicked: Ctrl.clearCacheAndRescan()
                    }

                    WinButton {
                        text: qsTr("打开配置目录")
                        dark: root.dark
                        onClicked: Ctrl.openConfigFolder()
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: qsTr("v%1").arg(Ctrl.appVersion)
                        color: root.subTextColor
                        font.pixelSize: 12
                    }
                }
            }
        }
    }
}
