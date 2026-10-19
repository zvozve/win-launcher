import QtQuick
import QtQuick.Window

/*
 * 最小复现：GridView + 和应用瓦片同形状的 delegate。
 * 只给 --qml-test 用（诊断），不参与正常启动。
 *
 * 目的：把「标签不显示」定位到「GridView / delegate 布局」还是
 * 「AppTile 内部」或「C++ model」。这里 model 是纯 JS 数组，
 * delegate 也是内联的，任何一个出问题都能一眼看出。
 */
Window {
    id: win
    width: 900
    height: 420
    visible: true
    color: "#F3F3F3"
    title: "QML layout test"

    property var rows: 24

    GridView {
        id: grid
        anchors.fill: parent
        anchors.margins: 10
        clip: true
        cellWidth: width / 5
        cellHeight: Math.floor(height / 2)

        model: {
            const a = []
            for (let i = 0; i < win.rows; ++i)
                a.push({ name: "Item " + i })
            return a
        }

        delegate: Item {
            // 和应用里一样：delegate 自带 width/height
            width: grid.cellWidth
            height: grid.cellHeight

            Rectangle {
                id: iconBox
                width: 56
                height: 56
                radius: 12
                color: "#7A8BA0"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: -20
            }

            Text {
                id: label
                objectName: "testLabel"
                width: parent.width - 14
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: iconBox.bottom
                anchors.topMargin: 6
                text: modelData.name
                color: "#1C1C1E"
                font.pixelSize: Theme.fsBody
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }
    }
}
