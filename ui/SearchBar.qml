import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import WinLauncher.Core

/*
 * 搜索栏（左 → 右）：
 *   搜索类型选择器 | 放大镜 | 输入框 | 清空
 *
 * 类型选择器改的是 AppConfig.searchCategory，Controller 会同时用它做
 * 本地扩展名过滤和 Everything 的 ext: 过滤。**搜索源没有开关**：
 *   应用档 → 只查本地；其它档 → 自动查全盘；词里写了扩展名 → 也查全盘。
 * 细节见 Controller::rebuildVisible 第 5 步。
 *
 * 输入框支持 Everything 的原生写法：「报告 xlsx」「"季度 报告" pdf」
 * 「ext:pdf;doc」，多个词是 AND 关系（见 SearchQuery）。
 *
 * 颜色全部走 Theme，不再需要 dark / textColor / hintColor 三个透传参数。
 */
Item {
    id: root

    /*
     * 搜索词。这里**不能**用 `property alias text: field.text`：
     * alias 转发的是 TextField 自己的 changed 信号，而 alias 属性本身对外
     * 只有「读」的能力，外部（LauncherPanel / Main）没法反过来清空它。
     * 结果是「清空搜索栏」这类操作在别处做不了，只能靠面板重新弹出时重建。
     * 改成普通 property 后，两边都能写，双向都通。
     */
    property string text: ""

    implicitHeight: 38

    signal accepted()
    // ↓：从搜索框跳进结果区（网格 / 全盘那一排），纯键盘操作不用换到鼠标
    signal moveToGrid()
    // Tab：焦点交给下一块区域（分类条）。三块区域的循环在 LauncherPanel 接线
    signal tabForward()
    /*
     * Esc 按下且输入框是空的。面板据此决定「退出文件夹」还是「收起面板」。
     * 输入框非空时 Esc 只清空文字，不发这个信号。
     */
    signal escaped()
    /*
     * 用户编辑文本（敲字 / 清空 / Esc）时发出。外部（LauncherPanel）收到后写
     * Ctrl.query。注意不能在内部直接给 root.text 赋值来上报 —— root.text 被
     * 外部绑定着（text: Ctrl.query），命令式赋值会**打断绑定**，之后外部复位
     * （面板重开清空）就传不进来了。用信号上报 + 绑定回填，两个方向都不断。
     */
    signal textEdited(string value)

    function focusInput() {
        field.forceActiveFocus()
    }

    // 外部要清空就调这个（比直接改 text 多一步：把焦点还给输入框）
    function clear() {
        root.textEdited("")
        field.forceActiveFocus()
    }

    onTextChanged: {
        // root.text 变了（多半是外部绑定回填）同步进 TextField；
        // 相等时不赋值，避免打断用户正在进行的输入/选中。
        if (field.text !== root.text)
            field.text = root.text
    }

    Rectangle {
        id: box
        anchors.fill: parent
        radius: Theme.rInput
        color: Theme.bgInput
        border.width: 1
        // 聚焦环是强调色 —— 深浅模式共用一个蓝，和搜索源选中胶囊同色
        border.color: field.activeFocus ? Theme.focusRing : "transparent"

        Behavior on border.color {
            ColorAnimation { duration: 110 }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 8
            spacing: 6

            // ---- 搜索类型 ----
            CategorySelector {
                id: category
                Layout.preferredWidth: 78
                Layout.preferredHeight: 26
            }

            // 分隔线
            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 18
                color: Theme.border
            }

            // ---- 放大镜（画出来，不依赖字体符号）----
            Item {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16

                Rectangle {
                    width: 10
                    height: 10
                    radius: 5
                    color: "transparent"
                    border.width: 1.6
                    border.color: Theme.textLo
                }

                Rectangle {
                    width: 1.6
                    height: 6
                    radius: 0.8
                    color: Theme.textLo
                    x: 9
                    y: 9
                    rotation: -45
                    transformOrigin: Item.TopLeft
                }
            }

            TextField {
                id: field
                Layout.fillWidth: true
                Layout.fillHeight: true

                placeholderText: qsTr("搜索应用，或输入「文件名 xlsx」搜文件")
                placeholderTextColor: Theme.textLo
                color: Theme.textHi
                font.pixelSize: Theme.fsTitle
                background: null
                selectByMouse: true
                leftPadding: 0
                rightPadding: 0

                // 用户敲键盘走这里往上报（信号，不改 root.text —— 那是绑定的地盘）；
                // Ctrl.query 更新后绑定会把新值回填到 root.text → field.text，
                // 相等时不会再触发赋值，不会自激。
                onTextChanged: {
                    if (text !== root.text)
                        root.textEdited(text)
                }

                onAccepted: root.accepted()

                /*
                 * ↓ 跳进结果区。TextField 自己不吃这个键（单行输入框没有
                 * "下一行"的概念），不拦的话事件冒到面板上什么也不会发生。
                 */
                Keys.onDownPressed: (event) => {
                    root.moveToGrid()
                    event.accepted = true
                }

                /*
                 * Tab 不留给默认焦点链（那条链会绕进「编辑/设置」按钮），
                 * 按用户约定：Tab = 去下一块区域（分类条）。
                 */
                Keys.onTabPressed: (event) => {
                    root.tabForward()
                    event.accepted = true
                }

                Keys.onEscapePressed: (event) => {
                    if (field.text.length > 0) {
                        root.clear()
                        event.accepted = true
                    } else {
                        // 空的时候别把 Esc 吃掉：面板要么退回上一层（文件夹），
                        // 要么收起。见 LauncherPanel.onEscaped
                        root.escaped()
                        event.accepted = false
                    }
                }
            }

            // ---- 清空 ----
            Rectangle {
                id: clearButton
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                radius: 9
                visible: field.text.length > 0
                color: clearArea.containsMouse ? Theme.stateHover : Theme.stateIdle

                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    font.pixelSize: Theme.fsTiny
                    color: Theme.textHi
                }

                MouseArea {
                    id: clearArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.clear()
                }
            }
        }
    }
}
