import QtQuick

Rectangle {
    id: root

    property color panelColor: "#0D223F"
    property color outlineColor: "#2A527D"
    property color glowColor: "transparent"

    radius: 12
    gradient: Gradient {
        GradientStop { position: 0.0; color: Qt.lighter(root.panelColor, 1.09) }
        GradientStop { position: 1.0; color: Qt.darker(root.panelColor, 1.12) }
    }
    border.width: 1
    border.color: outlineColor

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        height: 1
        color: root.glowColor
        opacity: root.glowColor === "transparent" ? 0 : 0.82
    }
}
