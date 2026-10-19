import QtQuick
import WinLauncher.Core

/*
 * 顶部分类标签条（对齐 macOS Applications 面板那一排 Social / Creativity / …）。
 *
 * 数据来自 Ctrl.appCategories（每项 { key, label, count }），点一下就把
 * AppConfig.appCategory 改掉，网格随即只显示该分类的条目。
 *
 * 用 Flickable 而不是 ScrollView：分类多的时候要能横向滑，但不需要滚动条和那一层
 * 控件开销。滚轮也接上了 —— 鼠标竖着滚就横向走。
 *
 * 胶囊配色走 Theme：选中 = 实心白胶囊 + 深色字（macOS 就是这样），
 * 未选中只有一层很淡的底、**不描边** —— 满排十几个描边胶囊看着很碎。
 */
Item {
    id: root

    implicitHeight: 28

    readonly property string current: AppConfig.appCategory

    /*
     * 键盘导航（Tab 方案的第二块区域）：
     *   Tab 从搜索框进来 → ←→ 在胶囊间移动高亮 → Enter 确认分类；
     *   ↑ / Shift+Tab 回搜索框，↓ / Tab 去网格。焦点由 LauncherPanel 用
     *   forceActiveFocus 手动交给 root（不走 activeFocusOnTab 链，
     *   否则 Tab 会绕进「编辑/设置」按钮）。
     */
    signal moveUp()
    signal moveDown()

    // 键盘高亮落在哪个胶囊上；-1 = 当前不处于键盘选择状态
    property int keyIndex: -1

    function focusBar() {
        if (Ctrl.appCategories.length === 0)
            return
        // 进来时高亮先落在当前生效的分类上，←→ 再从那里挪
        let idx = 0
        for (let i = 0; i < Ctrl.appCategories.length; ++i) {
            if (Ctrl.appCategories[i].key === AppConfig.appCategory) {
                idx = i
                break
            }
        }
        keyIndex = idx
        ensureChipVisible(idx)
        forceActiveFocus()
    }

    // ←→ 移动后如果高亮胶囊滑出了可视区（分类多到要横滚），把它拉回来
    function ensureChipVisible(i) {
        const chip = catRepeater.itemAt(i)
        if (!chip)
            return
        const x0 = chip.x
        const x1 = chip.x + chip.width
        const maxScroll = Math.max(0, scroller.contentWidth - scroller.width)
        if (x0 < scroller.contentX) {
            scroller.contentX = Math.max(0, x0 - 6)
        } else if (x1 > scroller.contentX + scroller.width) {
            scroller.contentX = Math.min(maxScroll, x1 - scroller.width + 6)
        }
    }

    function applyKey() {
        if (keyIndex < 0 || keyIndex >= Ctrl.appCategories.length)
            return
        AppConfig.appCategory = Ctrl.appCategories[keyIndex].key
    }

    Keys.onPressed: (event) => {
        const n = Ctrl.appCategories.length
        switch (event.key) {
        case Qt.Key_Left:
            if (keyIndex > 0) {
                keyIndex--
                ensureChipVisible(keyIndex)
            }
            event.accepted = true
            break
        case Qt.Key_Right:
            if (keyIndex < n - 1) {
                keyIndex++
                ensureChipVisible(keyIndex)
            }
            event.accepted = true
            break
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Space:
            applyKey()
            event.accepted = true
            break
        case Qt.Key_Up:
        case Qt.Key_Backtab:
            root.moveUp()
            event.accepted = true
            break
        case Qt.Key_Down:
        case Qt.Key_Tab:
            root.moveDown()
            event.accepted = true
            break
        }
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        contentWidth: row.width
        contentHeight: height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick

        WheelHandler {
            onWheel: function (event) {
                const max = Math.max(0, scroller.contentWidth - scroller.width)
                scroller.contentX = Math.max(0, Math.min(max, scroller.contentX - event.angleDelta.y))
                event.accepted = true
            }
        }

        Row {
            id: row
            height: parent.height
            spacing: 6

            Repeater {
                id: catRepeater
                model: Ctrl.appCategories

                delegate: Rectangle {
                    id: chip

                    readonly property bool active: modelData.key === root.current
                    readonly property bool hovered: hit.containsMouse
                    // 键盘高亮：只有分类条真的持有焦点时才亮（鼠标 hover 不算）
                    readonly property bool keySel: root.activeFocus && index === root.keyIndex

                    width: labelText.implicitWidth + 26
                    height: 26
                    anchors.verticalCenter: parent.verticalCenter
                    radius: height / 2

                    color: active
                           ? Theme.chipBgActive
                           : (hovered || keySel ? Theme.chipBgHover : Theme.chipBg)
                    // 浅色模式下白胶囊需要一层描边才看得出边界（深色下该值为 transparent）
                    // 键盘选中项额外描一圈强调色，和网格/全盘结果的键盘高亮一致
                    border.width: (active || keySel) ? 1 : 0
                    border.color: keySel ? Theme.focusRing : Theme.chipBorderActive

                    Behavior on color {
                        ColorAnimation { duration: Theme.durBase }
                    }

                    Text {
                        id: labelText
                        anchors.centerIn: parent
                        text: modelData.label
                        font.pixelSize: Theme.fsBody
                        font.weight: chip.active ? Font.DemiBold : Font.Normal
                        color: chip.active ? Theme.chipTextActive : Theme.textHi
                    }

                    MouseArea {
                        id: hit
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: AppConfig.appCategory = modelData.key
                    }
                }
            }
        }
    }
}
