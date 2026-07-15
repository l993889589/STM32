import QtQuick

Rectangle {
    id: root

    property string text: "状态"
    property color statusColor: "#9EB8DA"
    property bool pulse: false

    implicitWidth: label.implicitWidth + 32
    implicitHeight: 34
    radius: 10
    color: Qt.rgba(statusColor.r, statusColor.g, statusColor.b, 0.14)
    border.width: 1
    border.color: Qt.rgba(statusColor.r, statusColor.g, statusColor.b, 0.38)

    Row {
        anchors.centerIn: parent
        spacing: 8

        Item {
            width: 8
            height: 8

            Rectangle {
                id: halo
                anchors.centerIn: parent
                width: 14
                height: 14
                radius: 7
                color: "transparent"
                border.width: 1
                border.color: root.statusColor
                opacity: 0

                SequentialAnimation on opacity {
                    running: root.pulse
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.7; to: 0; duration: 950; easing.type: Easing.OutCubic }
                }

                SequentialAnimation on scale {
                    running: root.pulse
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.55; to: 1.35; duration: 950; easing.type: Easing.OutCubic }
                }
            }

            Rectangle {
                anchors.centerIn: parent
                width: 7
                height: 7
                radius: 4
                color: root.statusColor
            }
        }

        Text {
            id: label
            text: root.text
            color: root.statusColor
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
    }
}
