import QtQuick
import QtQuick.Layouts

Panel {
    id: root

    property string title: "指标"
    property string value: "0"
    property string detail: ""
    property string unit: ""
    property string glyph: "◇"
    property color accent: "#31A8FF"

    implicitWidth: 220
    implicitHeight: 116
    glowColor: accent

    RowLayout {
        anchors.fill: parent
        anchors.margins: 15
        spacing: 12

        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: 44
            Layout.preferredHeight: 44
            radius: 10
            color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.15)
            border.width: 1
            border.color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.3)

            Text {
                anchors.centerIn: parent
                text: root.glyph
                color: root.accent
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 3

            Text {
                text: root.title.toUpperCase()
                color: "#9EB8DA"
                font.pixelSize: 12
                font.letterSpacing: 0.7
            }

            Row {
                spacing: 5

                Text {
                    text: root.value
                    color: "#F3F7FF"
                    font.pixelSize: 25
                    font.weight: Font.DemiBold
                }

                Text {
                    anchors.baseline: parent.children[0].baseline
                    text: root.unit
                    color: root.accent
                    font.pixelSize: 12
                }
            }

            Item { Layout.fillHeight: true }

            Text {
                Layout.fillWidth: true
                text: root.detail
                color: "#9EB8DA"
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }
    }
}
