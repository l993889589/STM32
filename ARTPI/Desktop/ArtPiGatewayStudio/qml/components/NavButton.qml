import QtQuick
import QtQuick.Controls

Button {
    id: root

    property string iconText: "●"
    property bool selected: false

    implicitWidth: 112
    implicitHeight: 62
    hoverEnabled: true

    background: Rectangle {
        radius: 10
        color: root.selected ? "#153A70" : (root.hovered ? "#102D5A" : "transparent")
        border.width: root.selected ? 1 : 0
        border.color: "#245E96"

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: root.selected ? 3 : 0
            radius: 1
            color: "#31A8FF"

            Behavior on height { NumberAnimation { duration: 160 } }
        }
    }

    contentItem: Row {
        spacing: 9

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.iconText
            color: root.selected ? "#31A8FF" : "#9EB8DA"
            font.pixelSize: 17
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.selected ? "#F3F7FF" : "#B2C4DD"
            font.pixelSize: 15
            font.weight: root.selected ? Font.DemiBold : Font.Normal
        }
    }
}
