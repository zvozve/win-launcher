import QtQuick

/*
 * 面板底板。
 *
 * 模糊和底色由窗口原生材质（丙烯酸）提供，所以这里**不能**再压一层厚底色，
 * 否则会把模糊盖掉 —— 只留一层极淡的色调 + 描边，负责把内容托起来。
 * 原生材质没生效时（老系统）会退化成偏透的纯色，仍可读。
 */
Rectangle {
    id: root

    property bool dark: true

    radius: 18
    color: dark ? Qt.rgba(0.06, 0.06, 0.08, 0.10) : Qt.rgba(1, 1, 1, 0.16)
    border.width: 1
    border.color: dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.45)

    Behavior on color {
        ColorAnimation { duration: 160 }
    }
}
