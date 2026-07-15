import QtQuick
import QtQuick.Layouts

Panel {
    id: root

    required property int deviceIndex
    required property int unitId
    required property string deviceState
    required property int consecutiveFailures
    required property int backoffStep
    required property int lastFunction
    required property int lastException
    required property int successfulPolls
    required property int timeoutCount
    required property int protocolErrors
    required property double lastSuccessMs
    required property double nextActionMs
    required property var coilValues
    required property var discreteValues
    required property var holdingValues
    required property var inputValues
    property bool active: false
    property double boardUptimeMs: 0

    readonly property color stateColor: active ? "#31A8FF"
                                               : deviceState === "online" ? "#75E887"
                                               : deviceState === "offline" ? "#FF6B74"
                                               : "#FFC857"
    readonly property string stateText: active ? "正在通信"
                                                : deviceState === "online" ? "在线"
                                                : deviceState === "suspect" ? "疑似离线"
                                                : deviceState === "offline" ? "离线退避"
                                                : deviceState === "probing" ? "离线探测"
                                                : deviceState

    function valuesText(values) {
        if (!values || values.length === 0)
            return "—"
        var result = ""
        for (var i = 0; i < values.length; ++i)
            result += (i === 0 ? "" : " · ") + values[i]
        return result
    }

    function nextActionText() {
        var remaining = Math.max(0, nextActionMs - boardUptimeMs)
        if (active)
            return "当前事务执行中"
        if (remaining < 1000)
            return "即将调度"
        return (remaining / 1000).toFixed(1) + " 秒后调度"
    }

    implicitWidth: 360
    implicitHeight: 278
    outlineColor: Qt.rgba(stateColor.r, stateColor.g, stateColor.b, active ? 0.75 : 0.28)
    glowColor: stateColor

    Behavior on outlineColor { ColorAnimation { duration: 220 } }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -2
        radius: parent.radius + 2
        color: "transparent"
        border.width: 1
        border.color: root.stateColor
        opacity: 0

        SequentialAnimation on opacity {
            running: root.active
            loops: Animation.Infinite
            NumberAnimation { from: 0.55; to: 0.05; duration: 800; easing.type: Easing.OutCubic }
        }
        SequentialAnimation on scale {
            running: root.active
            loops: Animation.Infinite
            NumberAnimation { from: 1.0; to: 1.018; duration: 800; easing.type: Easing.OutCubic }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 17
        spacing: 11

        RowLayout {
            Layout.fillWidth: true

            Rectangle {
                Layout.preferredWidth: 38
                Layout.preferredHeight: 38
                radius: 11
                color: Qt.rgba(root.stateColor.r, root.stateColor.g, root.stateColor.b, 0.12)
                border.width: 1
                border.color: Qt.rgba(root.stateColor.r, root.stateColor.g, root.stateColor.b, 0.35)

                Text {
                    anchors.centerIn: parent
                    text: root.unitId
                    color: root.stateColor
                    font.pixelSize: 15
                    font.weight: Font.Bold
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: "Modbus 从机 " + (root.deviceIndex + 1)
                    color: "#F3F7FF"
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }

                Text {
                    text: "UNIT ID  " + root.unitId + "   ·   FC " + root.lastFunction
                    color: "#9EB8DA"
                    font.pixelSize: 10
                    font.letterSpacing: 0.8
                }
            }

            StatusPill {
                text: root.stateText
                statusColor: root.stateColor
                pulse: root.active || root.deviceState === "online"
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: "#2A527D"
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 22
            rowSpacing: 9

            Repeater {
                model: [
                    { label: "线圈 0x", value: root.valuesText(root.coilValues) },
                    { label: "离散输入 1x", value: root.valuesText(root.discreteValues) },
                    { label: "保持寄存器 4x", value: root.valuesText(root.holdingValues) },
                    { label: "输入寄存器 3x", value: root.valuesText(root.inputValues) }
                ]

                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: modelData.label
                        color: "#86A4C8"
                        font.pixelSize: 10
                    }

                    Text {
                        Layout.fillWidth: true
                        text: modelData.value
                        color: "#DCE9F8"
                        font.family: "Cascadia Mono"
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true

            ColumnLayout {
                spacing: 2
                Text {
                    text: root.nextActionText()
                    color: root.stateColor
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "连续失败 " + root.consecutiveFailures + "  ·  退避级别 " + root.backoffStep
                    color: "#86A4C8"
                    font.pixelSize: 10
                }
            }

            Item { Layout.fillWidth: true }

            ColumnLayout {
                spacing: 2
                Text {
                    Layout.alignment: Qt.AlignRight
                    text: root.successfulPolls + " 成功"
                    color: "#75E887"
                    font.pixelSize: 10
                }
                Text {
                    Layout.alignment: Qt.AlignRight
                    text: root.timeoutCount + " 超时 · " + root.protocolErrors + " 协议错误"
                    color: "#D68A94"
                    font.pixelSize: 10
                }
            }
        }
    }
}
