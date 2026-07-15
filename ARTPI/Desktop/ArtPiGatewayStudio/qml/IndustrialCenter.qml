import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import ArtPiGatewayStudio
import "components"

Item {
    id: root

    property var hostWindow
    property color accent: "#31A8FF"
    property color success: "#75E887"
    property color warning: "#FFC857"
    property color danger: "#FF6B74"
    property color muted: "#9EB8DA"
    property int section: 0
    property int selectedTagId: industrial.tags.length > 0 ? Number(industrial.tags[0].id) : 0
    property int historyHours: 24

    function mapValue(map, key, fallback) {
        return map && map[key] !== undefined && map[key] !== null ? map[key] : fallback
    }

    function reloadHistory() {
        if (selectedTagId > 0)
            industrial.selectHistory(selectedTagId, historyHours)
    }

    onSectionChanged: {
        if (section === 1)
            reloadHistory()
    }

    component SectionButton: Button {
        id: control
        property bool selected: false
        implicitHeight: 38
        leftPadding: 16
        rightPadding: 16
        background: Rectangle {
            radius: 8
            color: control.selected ? "#1A477F" : (control.hovered ? "#153A70" : "transparent")
            border.width: control.selected ? 1 : 0
            border.color: root.accent
        }
        contentItem: Text {
            text: control.text
            color: control.selected ? "#F3F7FF" : root.muted
            font.pixelSize: 12
            font.weight: control.selected ? Font.DemiBold : Font.Normal
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    component ActionButton: Button {
        id: control
        property bool primary: false
        implicitHeight: 36
        leftPadding: 14
        rightPadding: 14
        background: Rectangle {
            radius: 7
            color: control.primary ? (control.hovered ? "#58BEFF" : root.accent)
                                   : (control.hovered ? "#1D4C84" : "#153A70")
            border.width: control.primary ? 0 : 1
            border.color: "#2C5581"
            opacity: control.enabled ? 1 : 0.4
        }
        contentItem: Text {
            text: control.text
            color: control.primary ? "#061326" : "#F3F7FF"
            font.pixelSize: 12
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    component InputField: TextField {
        id: control
        implicitHeight: 38
        color: "#F3F7FF"
        placeholderTextColor: "#6582A6"
        leftPadding: 10
        background: Rectangle {
            radius: 7
            color: control.activeFocus ? "#153A70" : "#0B2147"
            border.width: 1
            border.color: control.activeFocus ? root.accent : "#2A527D"
        }
    }

    component Caption: Text {
        color: root.muted
        font.pixelSize: 10
        font.letterSpacing: 0.5
    }

    component MiniMetric: Rectangle {
        property string title: "指标"
        property string value: "0"
        property string detail: ""
        property color metricColor: root.accent
        Layout.fillWidth: true
        implicitHeight: 108
        radius: 10
        color: "#0D254D"
        border.width: 1
        border.color: "#234E7B"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 4
            Caption { text: parent.parent.title.toUpperCase() }
            Text {
                text: parent.parent.value
                color: parent.parent.metricColor
                font.pixelSize: 23
                font.weight: Font.DemiBold
                font.family: "Cascadia Mono"
            }
            Text {
                Layout.fillWidth: true
                text: parent.parent.detail
                color: "#7895B8"
                font.pixelSize: 10
                elide: Text.ElideRight
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 22
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 2
                Text { text: "工业运行中心"; color: "#F3F7FF"; font.pixelSize: 23; font.weight: Font.DemiBold }
                Text {
                    text: "SQLite 历史 · 点表与报警 · 工艺监控 · 配置版本 · MQTT 出口"
                    color: root.muted
                    font.pixelSize: 11
                }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                implicitWidth: alarmText.implicitWidth + 26
                implicitHeight: 34
                radius: 17
                color: industrial.activeAlarmCount > 0 ? "#4B2030" : "#123B38"
                border.width: 1
                border.color: industrial.activeAlarmCount > 0 ? root.danger : root.success
                Text {
                    id: alarmText
                    anchors.centerIn: parent
                    text: industrial.activeAlarmCount > 0 ? industrial.activeAlarmCount + " 条活动报警" : "无活动报警"
                    color: industrial.activeAlarmCount > 0 ? root.danger : root.success
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 50
            radius: 10
            color: "#0B2147"
            border.width: 1
            border.color: "#21496F"
            RowLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 4
                SectionButton { text: "工艺总览"; selected: root.section === 0; onClicked: root.section = 0 }
                SectionButton { text: "点表与趋势"; selected: root.section === 1; onClicked: { root.section = 1; root.reloadHistory() } }
                SectionButton { text: "报警与审计"; selected: root.section === 2; onClicked: root.section = 2 }
                SectionButton { text: "诊断与版本"; selected: root.section === 3; onClicked: root.section = 3 }
                SectionButton { text: "维护与安全"; selected: root.section === 4; onClicked: root.section = 4 }
                Item { Layout.fillWidth: true }
                Text {
                    text: industrial.securityInitialized
                          ? (industrial.authenticated ? industrial.currentUser + " · " + industrial.currentRole : "未登录")
                          : "本机模式"
                    color: industrial.authenticated ? root.success : root.warning
                    font.pixelSize: 10
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.section

            // P2: process mimic and live values.
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 14

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 4
                        columnSpacing: 12
                        MiniMetric {
                            title: "点位"
                            value: String(industrial.tags.length)
                            detail: "5 秒采样 · 7 天保留"
                            metricColor: root.accent
                        }
                        MiniMetric {
                            title: "历史样本"
                            value: String(root.mapValue(industrial.maintenance, "sample_count", 0))
                            detail: "SQLite WAL 持久化"
                            metricColor: "#9F8CFF"
                        }
                        MiniMetric {
                            title: "RS485"
                            value: gateway.onlineDeviceCount + " / " + (gateway.onlineDeviceCount + gateway.offlineDeviceCount)
                            detail: root.mapValue(gateway.rs485, "polls_completed", 0) + " 次轮询完成"
                            metricColor: root.success
                        }
                        MiniMetric {
                            title: "协议出口"
                            value: industrial.outletState.toUpperCase()
                            detail: "MQTT 3.1.1 · QoS 0"
                            metricColor: industrial.outletState === "online" ? root.success : root.warning
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 14

                        Panel {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            glowColor: root.accent
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 10
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "数据链路工艺图"; color: "#E7F1FA"; font.pixelSize: 15; font.weight: Font.DemiBold }
                                    Item { Layout.fillWidth: true }
                                    Text {
                                        text: root.mapValue(gateway.rs485, "active_transaction", 0) ? "数据流动中" : "调度待机"
                                        color: root.mapValue(gateway.rs485, "active_transaction", 0) ? root.warning : root.success
                                        font.pixelSize: 10
                                    }
                                }

                                Item {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 170

                                    Canvas {
                                        anchors.fill: parent
                                        onPaint: {
                                            var ctx = getContext("2d")
                                            ctx.clearRect(0, 0, width, height)
                                            ctx.strokeStyle = gateway.connected ? "#31A8FF" : "#5B6A7B"
                                            ctx.lineWidth = 2
                                            ctx.setLineDash([8, 6])
                                            ctx.beginPath()
                                            ctx.moveTo(145, height / 2)
                                            ctx.lineTo(width - 170, height / 2)
                                            ctx.stroke()
                                            ctx.setLineDash([])
                                            for (var i = 0; i < 10; ++i) {
                                                var x = width - 145 + (i % 2) * 78
                                                var y = 13 + Math.floor(i / 2) * 30
                                                ctx.strokeStyle = "#315B83"
                                                ctx.beginPath()
                                                ctx.moveTo(width - 170, height / 2)
                                                ctx.lineTo(x, y + 10)
                                                ctx.stroke()
                                            }
                                        }
                                        onWidthChanged: requestPaint()
                                        onHeightChanged: requestPaint()
                                    }

                                    Rectangle {
                                        x: 18; anchors.verticalCenter: parent.verticalCenter
                                        width: 126; height: 70; radius: 10
                                        color: "#123B69"; border.width: 2
                                        border.color: gateway.connected ? root.success : root.danger
                                        Column {
                                            anchors.centerIn: parent; spacing: 3
                                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "ART-Pi H750"; color: "#F3F7FF"; font.bold: true }
                                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: gateway.boardIp || "离线"; color: root.accent; font.pixelSize: 10 }
                                        }
                                    }

                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 160; height: 76; radius: 12
                                        color: "#173A68"; border.width: 2; border.color: root.warning
                                        Column {
                                            anchors.centerIn: parent; spacing: 3
                                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "RS485 / Modbus RTU"; color: "#F3F7FF"; font.bold: true }
                                            Text {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                text: "队列 " + root.mapValue(gateway.rs485, "command_queue_depth", 0) + " · Unit " + root.mapValue(gateway.rs485, "active_unit_id", 0)
                                                color: root.warning; font.pixelSize: 10
                                            }
                                        }
                                        SequentialAnimation on opacity {
                                            running: Number(root.mapValue(gateway.rs485, "active_transaction", 0)) !== 0
                                            loops: Animation.Infinite
                                            NumberAnimation { to: 0.55; duration: 420 }
                                            NumberAnimation { to: 1; duration: 420 }
                                        }
                                    }

                                    Grid {
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        columns: 2
                                        rowSpacing: 5
                                        columnSpacing: 5
                                        Repeater {
                                            model: gateway.devices
                                            delegate: Rectangle {
                                                width: 72; height: 25; radius: 5
                                                color: deviceState === "online" ? "#123B38" : "#402332"
                                                border.width: 1
                                                border.color: deviceState === "online" ? root.success : root.danger
                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "U" + unitId + "  " + (deviceState === "online" ? "ON" : "OFF")
                                                    color: deviceState === "online" ? root.success : root.danger
                                                    font.pixelSize: 9; font.family: "Cascadia Mono"
                                                }
                                            }
                                        }
                                    }
                                }

                                Rectangle { Layout.fillWidth: true; height: 1; color: "#23486D" }
                                GridLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    columns: 5
                                    columnSpacing: 8
                                    rowSpacing: 8
                                    Repeater {
                                        model: industrial.tags.slice(0, 10)
                                        delegate: Rectangle {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            radius: 8
                                            color: "#0C2144"
                                            border.width: 1
                                            border.color: modelData.quality === "good" ? "#265E78" : "#594054"
                                            Column {
                                                anchors.centerIn: parent; spacing: 3
                                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.name; color: root.muted; font.pixelSize: 9; elide: Text.ElideRight; width: 110; horizontalAlignment: Text.AlignHCenter }
                                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: Number(modelData.value || 0).toFixed(1) + " " + modelData.unit; color: modelData.quality === "good" ? root.accent : root.danger; font.pixelSize: 16; font.family: "Cascadia Mono" }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // P1: tags and history.
            Item {
                RowLayout {
                    anchors.fill: parent
                    spacing: 14
                    Panel {
                        Layout.preferredWidth: 510
                        Layout.fillHeight: true
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 14; spacing: 10
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "工程点表"; color: "#E7F1FA"; font.pixelSize: 15; font.weight: Font.DemiBold }
                                Item { Layout.fillWidth: true }
                                ActionButton { text: "+ 新建点位"; primary: true; enabled: industrial.canConfigure; onClicked: tagDialog.createTag() }
                            }
                            ListView {
                                id: tagList
                                Layout.fillWidth: true; Layout.fillHeight: true
                                clip: true; spacing: 5
                                model: industrial.tags
                                ScrollBar.vertical: ScrollBar { }
                                delegate: Rectangle {
                                    required property var modelData
                                    width: tagList.width; height: 68; radius: 8
                                    color: root.selectedTagId === Number(modelData.id) ? "#173E70" : "#0C2144"
                                    border.width: 1
                                    border.color: root.selectedTagId === Number(modelData.id) ? root.accent : "#23496F"
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: { root.selectedTagId = Number(modelData.id); root.reloadHistory() }
                                    }
                                    RowLayout {
                                        anchors.fill: parent; anchors.margins: 10; spacing: 9
                                        Rectangle { width: 7; height: 7; radius: 4; color: modelData.quality === "good" ? root.success : root.warning }
                                        ColumnLayout {
                                            Layout.fillWidth: true; spacing: 2
                                            Text { text: modelData.name; color: "#F0F6FC"; font.pixelSize: 12; font.weight: Font.DemiBold }
                                            Text { text: "设备 " + (Number(modelData.device_index) + 1) + " · " + modelData.register_class + "[" + modelData.value_index + "]"; color: root.muted; font.pixelSize: 9 }
                                        }
                                        Text { text: Number(modelData.value || 0).toFixed(2) + " " + modelData.unit; color: root.accent; font.family: "Cascadia Mono"; font.pixelSize: 13 }
                                        ActionButton { text: "编辑"; enabled: industrial.canConfigure; onClicked: tagDialog.editTag(modelData) }
                                    }
                                }
                            }
                        }
                    }

                    Panel {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        glowColor: "#9F8CFF"
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 16; spacing: 10
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "历史趋势"; color: "#E7F1FA"; font.pixelSize: 15; font.weight: Font.DemiBold }
                                Item { Layout.fillWidth: true }
                                ComboBox {
                                    id: historyRange
                                    model: ["1 小时", "6 小时", "24 小时", "7 天"]
                                    currentIndex: 2
                                    onActivated: {
                                        root.historyHours = [1, 6, 24, 168][currentIndex]
                                        root.reloadHistory()
                                    }
                                }
                                ActionButton { text: "刷新"; onClicked: root.reloadHistory() }
                            }
                            Rectangle {
                                Layout.fillWidth: true; Layout.fillHeight: true
                                radius: 9; color: "#081A36"; border.width: 1; border.color: "#25476C"
                                Canvas {
                                    id: trendCanvas
                                    anchors.fill: parent; anchors.margins: 12
                                    onPaint: {
                                        var ctx = getContext("2d")
                                        ctx.clearRect(0, 0, width, height)
                                        ctx.strokeStyle = "#203F61"; ctx.lineWidth = 1
                                        for (var gy = 0; gy <= 5; ++gy) {
                                            ctx.beginPath(); ctx.moveTo(0, gy * height / 5); ctx.lineTo(width, gy * height / 5); ctx.stroke()
                                        }
                                        for (var gx = 0; gx <= 8; ++gx) {
                                            ctx.beginPath(); ctx.moveTo(gx * width / 8, 0); ctx.lineTo(gx * width / 8, height); ctx.stroke()
                                        }
                                        var points = industrial.history
                                        if (!points || points.length < 1)
                                            return
                                        var min = Number(points[0].value), max = min
                                        for (var i = 1; i < points.length; ++i) {
                                            min = Math.min(min, Number(points[i].value)); max = Math.max(max, Number(points[i].value))
                                        }
                                        if (max === min) { max += 1; min -= 1 }
                                        ctx.strokeStyle = "#31A8FF"; ctx.lineWidth = 2.2; ctx.beginPath()
                                        for (var p = 0; p < points.length; ++p) {
                                            var x = points.length === 1 ? width / 2 : p * width / (points.length - 1)
                                            var y = height - (Number(points[p].value) - min) * height / (max - min)
                                            if (p === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                                        }
                                        ctx.stroke()
                                        ctx.fillStyle = "#9EB8DA"; ctx.font = "11px Cascadia Mono"
                                        ctx.fillText(max.toFixed(2), 6, 15); ctx.fillText(min.toFixed(2), 6, height - 6)
                                    }
                                    onWidthChanged: requestPaint()
                                    onHeightChanged: requestPaint()
                                    Connections { target: industrial; function onHistoryChanged() { trendCanvas.requestPaint() } }
                                }
                                Text {
                                    anchors.centerIn: parent
                                    visible: industrial.history.length === 0
                                    text: "等待历史样本 · 每 5 秒采样"
                                    color: "#617E9E"; font.pixelSize: 12
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: industrial.history.length + " 个样本"; color: root.muted; font.pixelSize: 10 }
                                Item { Layout.fillWidth: true }
                                Text { text: "数据库：" + industrial.databasePath; color: "#6582A6"; font.pixelSize: 9; elide: Text.ElideMiddle; Layout.maximumWidth: 520 }
                            }
                        }
                    }
                }
            }

            // P1: alarm and audit.
            Item {
                RowLayout {
                    anchors.fill: parent; spacing: 14
                    Panel {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        glowColor: root.danger
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 14; spacing: 10
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "报警中心"; color: "#F3F7FF"; font.pixelSize: 15; font.weight: Font.DemiBold }
                                Item { Layout.fillWidth: true }
                                ActionButton { text: "全部确认"; enabled: industrial.canOperate; onClicked: industrial.acknowledgeAllAlarms() }
                            }
                            ListView {
                                id: alarmList
                                Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 5
                                model: industrial.alarms
                                ScrollBar.vertical: ScrollBar { }
                                delegate: Rectangle {
                                    required property var modelData
                                    width: alarmList.width; height: 72; radius: 8
                                    color: Number(modelData.active) ? "#3A2030" : "#10283E"
                                    border.width: 1
                                    border.color: Number(modelData.active) ? root.danger : "#31506B"
                                    RowLayout {
                                        anchors.fill: parent; anchors.margins: 10; spacing: 10
                                        Rectangle { width: 8; height: 8; radius: 4; color: Number(modelData.active) ? root.danger : root.success }
                                        ColumnLayout {
                                            Layout.fillWidth: true; spacing: 2
                                            Text { text: modelData.message; color: "#F4EAF0"; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                                            Text { text: modelData.raised_at + " · " + (Number(modelData.active) ? "活动" : "已恢复"); color: root.muted; font.pixelSize: 9 }
                                        }
                                        Text { text: Number(modelData.value).toFixed(2); color: root.warning; font.family: "Cascadia Mono" }
                                        ActionButton { text: Number(modelData.acknowledged) ? "已确认" : "确认"; enabled: !Number(modelData.acknowledged) && industrial.canOperate; onClicked: industrial.acknowledgeAlarm(Number(modelData.id)) }
                                    }
                                }
                                Text { anchors.centerIn: parent; visible: alarmList.count === 0; text: "暂无报警记录"; color: "#637F9E" }
                            }
                        }
                    }
                    Panel {
                        Layout.preferredWidth: 560; Layout.fillHeight: true
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 14; spacing: 10
                            Text { text: "操作审计"; color: "#F3F7FF"; font.pixelSize: 15; font.weight: Font.DemiBold }
                            ListView {
                                id: auditList
                                Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4
                                model: industrial.audit
                                ScrollBar.vertical: ScrollBar { }
                                delegate: Rectangle {
                                    required property var modelData
                                    width: auditList.width; height: 59; radius: 7
                                    color: "#0C2144"; border.width: 1; border.color: "#23496F"
                                    ColumnLayout {
                                        anchors.fill: parent; anchors.margins: 8; spacing: 2
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text { text: modelData.action; color: root.accent; font.family: "Cascadia Mono"; font.pixelSize: 10 }
                                            Item { Layout.fillWidth: true }
                                            Text { text: modelData.username; color: root.success; font.pixelSize: 9 }
                                        }
                                        Text { text: modelData.timestamp + " · " + modelData.target + " · " + modelData.detail; color: root.muted; font.pixelSize: 9; elide: Text.ElideRight; Layout.fillWidth: true }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // P0/P2: diagnostics, versions and multiple gateway profiles.
            Item {
                RowLayout {
                    id: diagnosticsLayout
                    anchors.fill: parent; spacing: 14
                    ColumnLayout {
                        Layout.preferredWidth: Math.max(620, diagnosticsLayout.width * 0.62)
                        Layout.maximumWidth: Math.max(620, diagnosticsLayout.width * 0.62)
                        Layout.minimumWidth: 0
                        Layout.fillHeight: true
                        spacing: 14
                        Panel {
                            Layout.fillWidth: true; Layout.preferredHeight: 260
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 16; spacing: 9
                                Text { text: "通讯诊断"; color: "#F3F7FF"; font.pixelSize: 15; font.weight: Font.DemiBold }
                                GridLayout {
                                    Layout.fillWidth: true; columns: 4; columnSpacing: 12; rowSpacing: 10
                                    Repeater {
                                        model: [
                                            { n: "RX FRAMES", v: root.mapValue(gateway.ethernet,"rx_frames",0), c: root.accent },
                                            { n: "TX FRAMES", v: root.mapValue(gateway.ethernet,"tx_frames",0), c: root.success },
                                            { n: "RX ERRORS", v: root.mapValue(gateway.ethernet,"rx_errors",0), c: root.danger },
                                            { n: "DMA ERRORS", v: root.mapValue(gateway.ethernet,"dma_errors",0), c: root.warning },
                                            { n: "CRC ERRORS", v: root.mapValue(gateway.rs485,"crc_errors",0), c: root.danger },
                                            { n: "TIMEOUTS", v: root.mapValue(gateway.rs485,"poll_timeouts",0), c: root.warning },
                                            { n: "LDC DROP", v: root.mapValue(gateway.rs485,"ldc_drop",0), c: root.danger },
                                            { n: "RECONNECT", v: gateway.consecutiveFailures, c: root.accent }
                                        ]
                                        delegate: Rectangle {
                                            required property var modelData
                                            Layout.fillWidth: true; implicitHeight: 72; radius: 8; color: "#0C2144"; border.width: 1; border.color: "#23496F"
                                            Column {
                                                anchors.centerIn: parent
                                                spacing: 3
                                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.n; color: root.muted; font.pixelSize: 8 }
                                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: String(modelData.v); color: modelData.c; font.family: "Cascadia Mono"; font.pixelSize: 17 }
                                            }
                                        }
                                    }
                                }
                                Text { text: "Firmware " + gateway.firmwareVersion + " · API v" + gateway.apiVersion + " · " + gateway.connectionState; color: root.muted; font.pixelSize: 10 }
                            }
                        }
                        Panel {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 14; spacing: 9
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "持久运行日志"; color: "#F3F7FF"; font.pixelSize: 15; font.weight: Font.DemiBold }
                                    Item { Layout.fillWidth: true }
                                    ActionButton { text: "清理"; enabled: industrial.canConfigure; onClicked: industrial.clearPersistentLogs() }
                                }
                                ListView {
                                    id: persistentLogList
                                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 3
                                    model: industrial.persistentLogs
                                    ScrollBar.vertical: ScrollBar { }
                                    delegate: RowLayout {
                                        required property var modelData
                                        width: persistentLogList.width; height: 28
                                        Text { text: modelData.timestamp; color: "#597797"; font.pixelSize: 8; font.family: "Cascadia Mono" }
                                        Text { text: modelData.source; color: root.accent; font.pixelSize: 9; Layout.preferredWidth: 68 }
                                        Text { text: modelData.message; color: "#C2D3E3"; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideRight }
                                    }
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.fillHeight: true
                        spacing: 14
                        Panel {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 14; spacing: 8
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "配置版本"; color: "#F3F7FF"; font.pixelSize: 15; font.weight: Font.DemiBold }
                                    Item { Layout.fillWidth: true }
                                    ActionButton { text: "保存当前版本"; enabled: industrial.canConfigure && gateway.config.devices !== undefined; onClicked: industrial.saveConfigurationVersion("手动快照", gateway.endpoint, gateway.config) }
                                }
                                ListView {
                                    id: versionList
                                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4
                                    model: industrial.configVersions
                                    delegate: Rectangle {
                                        required property var modelData
                                        width: versionList.width; height: 58; radius: 7; color: "#0C2144"; border.width: 1; border.color: "#23496F"
                                        RowLayout {
                                            anchors.fill: parent; anchors.margins: 8
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 1
                                                Text { text: modelData.label; color: "#F3F7FF"; font.pixelSize: 11 }
                                                Text { text: modelData.created_at + " · " + modelData.endpoint; color: root.muted; font.pixelSize: 8; elide: Text.ElideRight; Layout.fillWidth: true }
                                            }
                                            ActionButton { text: "载入"; enabled: industrial.canConfigure; onClicked: { var c = industrial.restoreConfigurationVersion(Number(modelData.id)); if (root.hostWindow) root.hostWindow.loadConfigSnapshot(c) } }
                                            ActionButton { text: "删除"; enabled: industrial.canConfigure; onClicked: industrial.removeConfigurationVersion(Number(modelData.id)) }
                                        }
                                    }
                                }
                            }
                        }
                        Panel {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 14; spacing: 8
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "多网关档案"; color: "#F3F7FF"; font.pixelSize: 15; font.weight: Font.DemiBold }
                                    Item { Layout.fillWidth: true }
                                    ActionButton { text: "+ 添加"; enabled: industrial.canConfigure; onClicked: gatewayDialog.open() }
                                }
                                ListView {
                                    id: gatewayList
                                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4
                                    model: industrial.gateways
                                    delegate: Rectangle {
                                        required property var modelData
                                        width: gatewayList.width; height: 62; radius: 7; color: gateway.endpoint === modelData.endpoint ? "#173E70" : "#0C2144"; border.width: 1; border.color: gateway.endpoint === modelData.endpoint ? root.accent : "#23496F"
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.margins: 8
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 1
                                                Text { text: modelData.name; color: "#F3F7FF"; font.pixelSize: 11 }
                                                Text { text: modelData.endpoint + " · " + modelData.last_seen; color: root.muted; font.pixelSize: 8; Layout.fillWidth: true; elide: Text.ElideRight }
                                            }
                                            ActionButton { text: "切换"; onClicked: industrial.activateGateway(Number(modelData.id)) }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // P3: maintenance, security, report and MQTT protocol outlet.
            Item {
                ScrollView {
                    id: maintenanceScroll
                    anchors.fill: parent
                    clip: true
                    contentWidth: availableWidth
                    contentHeight: maintenanceColumn.implicitHeight
                    ColumnLayout {
                        id: maintenanceColumn
                        width: maintenanceScroll.availableWidth
                        spacing: 14
                        RowLayout {
                            Layout.fillWidth: true; spacing: 14
                            Panel {
                                Layout.fillWidth: true; Layout.preferredHeight: 245
                                ColumnLayout {
                                    anchors.fill: parent; anchors.margins: 16; spacing: 9
                                    Text { text: "数据维护与报表"; color: "#F3F7FF"; font.pixelSize: 15; font.weight: Font.DemiBold }
                                    GridLayout {
                                        Layout.fillWidth: true; columns: 2; columnSpacing: 12; rowSpacing: 7
                                        Caption { text: "DATABASE" }
                                        Text { text: industrial.databasePath; color: root.accent; font.pixelSize: 9; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                        Caption { text: "SIZE" }
                                        Text { text: (Number(root.mapValue(industrial.maintenance,"database_bytes",0))/1024).toFixed(1) + " KiB"; color: "#F3F7FF" }
                                        Caption { text: "LAST BACKUP" }
                                        Text { text: root.mapValue(industrial.maintenance,"last_backup","从未"); color: root.muted }
                                        Caption { text: "RETENTION" }
                                        Text { text: "历史 7 天 · 日志/审计各 5000 条"; color: root.muted }
                                    }
                                    Item { Layout.fillHeight: true }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        ActionButton { text: "备份数据库"; enabled: industrial.canConfigure; onClicked: backupDialog.open() }
                                        ActionButton { text: "导出 24h CSV"; onClicked: reportDialog.open() }
                                        ActionButton { text: "刷新统计"; onClicked: industrial.refreshMaintenance() }
                                        Item { Layout.fillWidth: true }
                                    }
                                    Text { text: "固件升级保持受控：桌面端不伪造 OTA，当前仍通过 ST-LINK/签名维护包执行。"; color: root.warning; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                }
                            }
                            Panel {
                                Layout.fillWidth: true; Layout.preferredHeight: 245
                                glowColor: root.success
                                ColumnLayout {
                                    anchors.fill: parent; anchors.margins: 16; spacing: 9
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { text: "本机用户与权限"; color: "#F3F7FF"; font.pixelSize: 15; font.weight: Font.DemiBold }
                                        Item { Layout.fillWidth: true }
                                        Text { text: industrial.securityInitialized ? (industrial.authenticated ? "已登录" : "已锁定") : "未启用"; color: industrial.authenticated ? root.success : root.warning; font.pixelSize: 10 }
                                    }
                                    Text { text: industrial.securityInitialized ? "当前：" + (industrial.currentUser || "anonymous") + " / " + industrial.currentRole : "可启用本机账户、密码哈希与 Viewer / Operator / Engineer / Administrator 四级权限。"; color: root.muted; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                    Item { Layout.fillHeight: true }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        ActionButton { text: industrial.securityInitialized ? "登录" : "初始化管理员"; onClicked: industrial.securityInitialized ? loginDialog.open() : securityDialog.open() }
                                        ActionButton { text: "新增/重置用户"; visible: industrial.securityInitialized; enabled: industrial.authenticated && industrial.currentRole === "administrator"; onClicked: userDialog.open() }
                                        ActionButton { text: "退出"; visible: industrial.securityInitialized && industrial.authenticated; onClicked: industrial.logout() }
                                        Item { Layout.fillWidth: true }
                                    }
                                    Flow {
                                        Layout.fillWidth: true; spacing: 5
                                        Repeater {
                                            model: industrial.users
                                            delegate: Rectangle { required property var modelData; width: userLabel.implicitWidth + 18; height: 25; radius: 12; color: "#12354C"; border.width: 1; border.color: "#2D607B"; Text { id: userLabel; anchors.centerIn: parent; text: modelData.username + " · " + modelData.role; color: root.muted; font.pixelSize: 8 } }
                                        }
                                    }
                                }
                            }
                        }

                        Panel {
                            Layout.fillWidth: true; Layout.preferredHeight: 330
                            glowColor: "#9F8CFF"
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 16; spacing: 10
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "MQTT 协议出口"; color: "#F3F7FF"; font.pixelSize: 15; font.weight: Font.DemiBold }
                                    Rectangle { width: 8; height: 8; radius: 4; color: industrial.outletState === "online" ? root.success : industrial.outletState === "error" ? root.danger : root.warning }
                                    Text { text: industrial.outletState; color: root.muted; font.pixelSize: 10 }
                                    Item { Layout.fillWidth: true }
                                    Switch { id: outletEnabled; checked: Boolean(root.mapValue(industrial.outlet,"enabled",false)); text: "启用" }
                                }
                                Text { text: "内置 MQTT 3.1.1 客户端，以 QoS 0 周期发布板端状态和工程点位；密码仅保留在本次运行内。"; color: root.muted; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                GridLayout {
                                    Layout.fillWidth: true; columns: 4; columnSpacing: 10; rowSpacing: 7
                                    Caption { text: "BROKER HOST" }
                                    Caption { text: "PORT" }
                                    Caption { text: "TOPIC" }
                                    Caption { text: "INTERVAL (S)" }
                                    InputField { id: mqttHost; Layout.fillWidth: true; text: String(root.mapValue(industrial.outlet,"host","127.0.0.1")) }
                                    SpinBox { id: mqttPort; from: 1; to: 65535; value: Number(root.mapValue(industrial.outlet,"port",1883)); editable: true }
                                    InputField { id: mqttTopic; Layout.fillWidth: true; text: String(root.mapValue(industrial.outlet,"topic","artpi/status")) }
                                    SpinBox { id: mqttInterval; from: 5; to: 3600; value: Number(root.mapValue(industrial.outlet,"intervalSeconds",10)); editable: true }
                                    Caption { text: "CLIENT ID" }
                                    Caption { text: "USERNAME" }
                                    Caption { text: "PASSWORD" }
                                    Item { }
                                    InputField { id: mqttClientId; Layout.fillWidth: true; text: String(root.mapValue(industrial.outlet,"clientId","artpi-gateway-studio")) }
                                    InputField { id: mqttUser; Layout.fillWidth: true; text: String(root.mapValue(industrial.outlet,"username","")) }
                                    InputField { id: mqttPassword; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: "本次运行" }
                                    RowLayout {
                                        ActionButton { text: "保存并应用"; primary: true; enabled: industrial.canConfigure; onClicked: industrial.configureOutlet({"enabled":outletEnabled.checked,"host":mqttHost.text,"port":mqttPort.value,"topic":mqttTopic.text,"clientId":mqttClientId.text,"username":mqttUser.text,"password":mqttPassword.text,"intervalSeconds":mqttInterval.value}) }
                                        ActionButton { text: "测试发布"; onClicked: industrial.testOutlet() }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: tagDialog
        property int editId: -1
        function createTag() {
            editId = -1; tagName.text = ""; tagDevice.value = 1; tagClass.currentIndex = 3; tagIndex.value = 0
            tagScale.text = "1"; tagOffset.text = "0"; tagUnit.text = "raw"; lowEnabled.checked = false; highEnabled.checked = false
            lowLimit.text = "0"; highLimit.text = "0"; open()
        }
        function editTag(t) {
            editId = Number(t.id); tagName.text = t.name; tagDevice.value = Number(t.device_index) + 1
            tagClass.currentIndex = ["coil","discrete","holding","input"].indexOf(t.register_class)
            tagIndex.value = Number(t.value_index); tagScale.text = String(t.scale); tagOffset.text = String(t.value_offset); tagUnit.text = t.unit
            lowEnabled.checked = Boolean(Number(t.low_enabled)); lowLimit.text = String(t.low_limit)
            highEnabled.checked = Boolean(Number(t.high_enabled)); highLimit.text = String(t.high_limit); open()
        }
        parent: Overlay.overlay; anchors.centerIn: parent; width: 620; height: 490; modal: true
        background: Rectangle { radius: 12; color: "#0B2147"; border.width: 1; border.color: root.accent }
        contentItem: ColumnLayout {
            anchors.fill: parent; anchors.margins: 20; spacing: 9
            Text { text: tagDialog.editId < 0 ? "新建工程点位" : "编辑工程点位"; color: "#F3F7FF"; font.pixelSize: 18; font.weight: Font.DemiBold }
            Caption { text: "NAME" }
            InputField { id: tagName; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Caption { text: "DEVICE" }
                    SpinBox { id: tagDevice; from: 1; to: 10; value: 1 }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Caption { text: "REGISTER CLASS" }
                    ComboBox { id: tagClass; Layout.fillWidth: true; model: ["coil","discrete","holding","input"]; currentIndex: 3 }
                }
                ColumnLayout {
                    Caption { text: "VALUE INDEX" }
                    SpinBox { id: tagIndex; from: 0; to: 15 }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout { Layout.fillWidth: true; Caption { text: "SCALE" } InputField { id: tagScale; Layout.fillWidth: true } }
                ColumnLayout { Layout.fillWidth: true; Caption { text: "OFFSET" } InputField { id: tagOffset; Layout.fillWidth: true } }
                ColumnLayout { Layout.fillWidth: true; Caption { text: "UNIT" } InputField { id: tagUnit; Layout.fillWidth: true } }
            }
            RowLayout {
                Layout.fillWidth: true
                Switch { id: lowEnabled; text: "低限报警" }
                InputField { id: lowLimit; Layout.fillWidth: true; enabled: lowEnabled.checked }
                Switch { id: highEnabled; text: "高限报警" }
                InputField { id: highLimit; Layout.fillWidth: true; enabled: highEnabled.checked }
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                ActionButton { text: "删除"; visible: tagDialog.editId >= 0; onClicked: { industrial.removeTag(tagDialog.editId); tagDialog.close() } }
                Item { Layout.fillWidth: true }
                ActionButton { text: "取消"; onClicked: tagDialog.close() }
                ActionButton {
                    text: "保存"
                    primary: true
                    onClicked: {
                        var d = {"name":tagName.text,"device_index":tagDevice.value-1,"register_class":tagClass.currentText,"value_index":tagIndex.value,"scale":Number(tagScale.text),"value_offset":Number(tagOffset.text),"unit":tagUnit.text,"low_enabled":lowEnabled.checked,"low_limit":Number(lowLimit.text),"high_enabled":highEnabled.checked,"high_limit":Number(highLimit.text),"enabled":true}
                        var ok = tagDialog.editId < 0 ? industrial.addTag(d) >= 0 : industrial.updateTag(tagDialog.editId,d)
                        if (ok) tagDialog.close()
                    }
                }
            }
        }
    }

    Dialog {
        id: gatewayDialog
        parent: Overlay.overlay; anchors.centerIn: parent; width: 520; height: 330; modal: true
        background: Rectangle { radius: 12; color: "#0B2147"; border.width: 1; border.color: root.accent }
        contentItem: ColumnLayout {
            anchors.fill: parent; anchors.margins: 20; spacing: 9
            Text { text: "添加网关档案"; color: "#F3F7FF"; font.pixelSize: 18 }
            Caption { text: "NAME" }
            InputField { id: profileName; Layout.fillWidth: true }
            Caption { text: "ENDPOINT" }
            InputField { id: profileEndpoint; Layout.fillWidth: true; text: "http://192.168.1.20" }
            Caption { text: "NOTES" }
            InputField { id: profileNotes; Layout.fillWidth: true }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { text: "取消"; onClicked: gatewayDialog.close() }
                ActionButton { text: "保存"; primary: true; onClicked: { if(industrial.addGateway(profileName.text,profileEndpoint.text,profileNotes.text)>=0) gatewayDialog.close() } }
            }
        }
    }

    Dialog {
        id: securityDialog
        parent: Overlay.overlay; anchors.centerIn: parent; width: 480; height: 280; modal: true
        background: Rectangle { radius: 12; color: "#0B2147"; border.width: 1; border.color: root.warning }
        contentItem: ColumnLayout {
            anchors.fill: parent; anchors.margins: 20; spacing: 10
            Text { text: "初始化本机管理员"; color: "#F3F7FF"; font.pixelSize: 18 }
            Text { text: "用户名固定为 admin。启用后下次启动需要登录。"; color: root.muted; font.pixelSize: 10 }
            Caption { text: "PASSWORD (MIN 8)" }
            InputField { id: adminPassword; Layout.fillWidth: true; echoMode: TextInput.Password }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { text: "取消"; onClicked: securityDialog.close() }
                ActionButton { text: "启用"; primary: true; onClicked: { if(industrial.initializeSecurity(adminPassword.text)) securityDialog.close() } }
            }
        }
    }

    Dialog {
        id: loginDialog
        parent: Overlay.overlay; anchors.centerIn: parent; width: 460; height: 300; modal: true
        background: Rectangle { radius: 12; color: "#0B2147"; border.width: 1; border.color: root.accent }
        contentItem: ColumnLayout {
            anchors.fill: parent; anchors.margins: 20; spacing: 9
            Text { text: "本机登录"; color: "#F3F7FF"; font.pixelSize: 18 }
            Caption { text: "USERNAME" }
            InputField { id: loginUser; Layout.fillWidth: true; text: "admin" }
            Caption { text: "PASSWORD" }
            InputField { id: loginPassword; Layout.fillWidth: true; echoMode: TextInput.Password }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { text: "取消"; onClicked: loginDialog.close() }
                ActionButton { text: "登录"; primary: true; onClicked: { if(industrial.login(loginUser.text,loginPassword.text)) loginDialog.close() } }
            }
        }
    }

    Dialog {
        id: userDialog
        parent: Overlay.overlay; anchors.centerIn: parent; width: 500; height: 370; modal: true
        background: Rectangle { radius: 12; color: "#0B2147"; border.width: 1; border.color: root.accent }
        contentItem: ColumnLayout {
            anchors.fill: parent; anchors.margins: 20; spacing: 9
            Text { text: "新增或重置用户"; color: "#F3F7FF"; font.pixelSize: 18 }
            Caption { text: "USERNAME" }
            InputField { id: newUsername; Layout.fillWidth: true }
            Caption { text: "PASSWORD (MIN 8)" }
            InputField { id: newPassword; Layout.fillWidth: true; echoMode: TextInput.Password }
            Caption { text: "ROLE" }
            ComboBox { id: newRole; Layout.fillWidth: true; model: ["viewer","operator","engineer","administrator"] }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { text: "取消"; onClicked: userDialog.close() }
                ActionButton { text: "保存"; primary: true; onClicked: { if(industrial.saveUser(newUsername.text,newPassword.text,newRole.currentText,true)) userDialog.close() } }
            }
        }
    }

    FileDialog { id: backupDialog; title: "备份工业数据库"; fileMode: FileDialog.SaveFile; nameFilters: ["SQLite database (*.db)"]; defaultSuffix: "db"; onAccepted: industrial.backupDatabase(selectedFile.toString()) }
    FileDialog { id: reportDialog; title: "导出工业运行报表"; fileMode: FileDialog.SaveFile; nameFilters: ["CSV report (*.csv)"]; defaultSuffix: "csv"; onAccepted: industrial.exportReport(selectedFile.toString(), 24) }

    Connections {
        target: industrial
        function onNotification(message, ok) { if (root.hostWindow) root.hostWindow.showToast(message, ok) }
    }
}
