import QtQuick
import WinLauncher.Core

/*
 * 热键录制控件：点击进入录制态，按下组合键即写回配置。
 * 只接受「至少一个修饰键 + 一个主键」的组合，避免录到裸字母。
 */
Rectangle {
    id: root

    property bool dark: true
    property color textColor: dark ? "#F2F2F7" : "#1C1C1E"
    property color subTextColor: dark ? "#9A9AA0" : "#6E6E73"
    property bool capturing: false

    implicitWidth: 170
    implicitHeight: 28
    radius: 7

    color: capturing
           ? Qt.rgba(0.04, 0.52, 1.0, 0.22)
           : (area.containsMouse
              ? (dark ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(0, 0, 0, 0.08))
              : (dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.05)))

    border.width: 1
    border.color: capturing ? "#0A84FF" : "transparent"

    focus: capturing
    Keys.onPressed: (event) => {
        if (!root.capturing)
            return

        if (event.key === Qt.Key_Escape) {
            root.capturing = false
            event.accepted = true
            return
        }

        var parts = []
        var m = event.modifiers
        if (m & Qt.ControlModifier) parts.push("Ctrl")
        if (m & Qt.AltModifier) parts.push("Alt")
        if (m & Qt.ShiftModifier) parts.push("Shift")
        if (m & Qt.MetaModifier) parts.push("Meta")

        var key = root.tokenFor(event.key)
        if (key.length === 0) {
            event.accepted = true // 只按修饰键 / 不支持的键
            return
        }
        if (parts.length === 0) {
            Ctrl.notifyError(qsTr("热键至少要带一个修饰键（Ctrl / Alt / Shift / Win）"))
            event.accepted = true
            return
        }

        parts.push(key)
        var sequence = parts.join("+")
        if (!Ctrl.setHotkey(sequence)) {
            root.capturing = false
            event.accepted = true
            return
        }
        root.capturing = false
        event.accepted = true
    }

    function tokenFor(key) {
        if (key >= Qt.Key_A && key <= Qt.Key_Z)
            return String.fromCharCode(key)
        if (key >= Qt.Key_0 && key <= Qt.Key_9)
            return String.fromCharCode(key)
        if (key >= Qt.Key_F1 && key <= Qt.Key_F24)
            return "F" + (key - Qt.Key_F1 + 1)

        switch (key) {
        case Qt.Key_Space:     return "Space"
        case Qt.Key_Tab:       return "Tab"
        case Qt.Key_Backspace: return "Backspace"
        case Qt.Key_Return:    return "Return"
        case Qt.Key_Enter:     return "Enter"
        case Qt.Key_Delete:    return "Del"
        case Qt.Key_Insert:    return "Ins"
        case Qt.Key_Home:      return "Home"
        case Qt.Key_End:       return "End"
        case Qt.Key_PageUp:    return "PgUp"
        case Qt.Key_PageDown:  return "PgDown"
        case Qt.Key_Left:      return "Left"
        case Qt.Key_Right:     return "Right"
        case Qt.Key_Up:        return "Up"
        case Qt.Key_Down:      return "Down"
        default:               return ""
        }
    }

    Text {
        anchors.centerIn: parent
        text: root.capturing ? qsTr("按下组合键…（Esc 取消）") : AppConfig.hotkey
        color: root.capturing ? "#FFFFFF" : root.textColor
        font.pixelSize: 12
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.capturing = !root.capturing
            if (root.capturing)
                root.forceActiveFocus()
        }
    }
}
