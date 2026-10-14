import QtQuick

/*
 * 玻璃底板：半透明 + 描边。
 * 真正的模糊来自窗口的原生材质（Win11 瞬时材质 / Win10 丙烯酸），
 * 这里只负责把内容托在一个「可读的半透明面」上。
 */
Rectangle {
    id: root

    property bool dark: true

    radius: 18
    color: dark ? Qt.rgba(0.10, 0.10, 0.12, 0.62)
                : Qt.rgba(0.97, 0.97, 0.99, 0.70)
    border.width: 1
    border.color: dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.06)

    Behavior on color {
        ColorAnimation { duration: 160 }
    }
}
