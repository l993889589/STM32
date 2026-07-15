import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ArtPiGatewayStudio
import "components"

ApplicationWindow {
    id: window

    width: 1520
    height: 940
    minimumWidth: 1180
    minimumHeight: 760
    visible: true
    title: "ART-Pi Gateway Studio"
    color: "#07162F"

    property int currentPage: 0
    property date wallClock: new Date()
    readonly property color accent: "#31A8FF"
    readonly property color success: "#75E887"
    readonly property color warning: "#FFC857"
    readonly property color danger: "#FF6B74"
    readonly property color panelColor: "#102D5A"
    readonly property color panelAlt: "#153A70"
    readonly property color muted: "#9EB8DA"

    palette.window: "#07162F"
    palette.windowText: "#F3F7FF"
    palette.base: "#0B2147"
    palette.text: "#F3F7FF"
    palette.button: "#153A70"
    palette.buttonText: "#F3F7FF"
    palette.highlight: accent
    palette.highlightedText: "#061018"

    function value(map, key, fallback) {
        if (!map || map[key] === undefined || map[key] === null)
            return fallback
        return map[key]
    }

    function showToast(message, ok) {
        toast.text = message
        toast.ok = ok
        toast.visible = true
        toastTimer.restart()
    }

    function populateConfig() {
        var c = gateway.config
        if (!c || !c.devices)
            return

        roleCombo.currentIndex = Number(c.rs485_role || 0)
        unitIdSpin.value = Number(c.modbus_unit_id || 1)
        deviceCountSpin.value = Number(c.master_device_count || c.devices.length)
        pollPeriodSpin.value = Number(c.poll_period_ms || 1000)
        probeCombo.currentIndex = Number(c.offline_probe_s) === 300 ? 1 : 0
        redLedSwitch.checked = Number(c.red_led_on) !== 0
        buzzerSwitch.checked = Number(c.buzzer_on) !== 0

        configDeviceModel.clear()
        for (var i = 0; i < c.devices.length && i < 10; ++i) {
            var d = c.devices[i]
            configDeviceModel.append({
                "unitId": Number(d.unit_id),
                "timeoutMs": Number(d.timeout_ms),
                "coilAddress": Number(d.coil.address),
                "coilQuantity": Number(d.coil.quantity),
                "discreteAddress": Number(d.discrete.address),
                "discreteQuantity": Number(d.discrete.quantity),
                "holdingAddress": Number(d.holding.address),
                "holdingQuantity": Number(d.holding.quantity),
                "inputAddress": Number(d.input.address),
                "inputQuantity": Number(d.input.quantity)
            })
        }
        configDialog.open()
    }

    component PageTitle: ColumnLayout {
        property string heading: "页面"
        property string subheading: ""
        spacing: 4

        Text {
            text: parent.heading
            color: "#F3F7FF"
            font.pixelSize: 23
            font.weight: Font.DemiBold
        }
        Text {
            text: parent.subheading
            color: "#9EB8DA"
            font.pixelSize: 12
        }
    }

    component StudioButton: Button {
        id: control
        property bool primary: false
        hoverEnabled: true
        implicitHeight: 40
        leftPadding: 18
        rightPadding: 18

        background: Rectangle {
            radius: 8
            color: control.primary
                   ? (control.hovered ? "#55BDFF" : "#31A8FF")
                   : (control.hovered ? "#1B477F" : "#153A70")
            border.width: control.primary ? 0 : 1
            border.color: "#2A527D"
            opacity: control.enabled ? 1 : 0.45
            Behavior on color { ColorAnimation { duration: 120 } }
        }

        contentItem: Text {
            text: control.text
            color: control.primary ? "#07162F" : "#F3F7FF"
            font.pixelSize: 13
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    component StudioTextField: TextField {
        id: control
        color: "#F3F7FF"
        placeholderTextColor: "#6F8DB1"
        selectionColor: window.accent
        selectedTextColor: "#07162F"
        implicitHeight: 40
        leftPadding: 12
        rightPadding: 12
        background: Rectangle {
            radius: 8
            color: control.activeFocus ? "#153A70" : "#0B2147"
            border.width: 1
            border.color: control.activeFocus ? window.accent : "#2A527D"
        }
    }

    component FieldLabel: Text {
        color: "#9EB8DA"
        font.pixelSize: 10
        font.letterSpacing: 0.7
    }

    background: Rectangle {
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#07162F" }
            GradientStop { position: 0.58; color: "#061328" }
            GradientStop { position: 1.0; color: "#040C19" }
        }

        Canvas {
            anchors.fill: parent
            opacity: 0.10
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.strokeStyle = "#245A8C"
                ctx.lineWidth = 1
                for (var x = 0; x < width; x += 44) {
                    ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke()
                }
                for (var y = 0; y < height; y += 44) {
                    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
                }
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
        }
    }

    header: Rectangle {
        height: 86
        color: "#071226"
        border.width: 0

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: "#24496F"
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            spacing: 12

            RowLayout {
                Layout.preferredWidth: 226
                spacing: 10

                Rectangle {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                    radius: 24
                    color: "#0B2147"
                    border.width: 3
                    border.color: window.accent

                    Rectangle {
                        anchors.centerIn: parent
                        width: 24
                        height: 24
                        radius: 12
                        color: "transparent"
                        border.width: 3
                        border.color: window.accent
                    }
                    Rectangle {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 17
                        height: 12
                        color: "#0B2147"
                    }
                }

                ColumnLayout {
                    spacing: 0
                    Text {
                        text: "LeduO"
                        color: "#F3F7FF"
                        font.pixelSize: 22
                        font.weight: Font.Bold
                    }
                    Text {
                        text: "ART-Pi 工业网关"
                        color: "#6F8DB1"
                        font.pixelSize: 10
                        font.letterSpacing: 0.8
                    }
                }
            }

            RowLayout {
                spacing: 4

                NavButton {
                    text: "总览"
                    iconText: "⌂"
                    selected: window.currentPage === 0
                    onClicked: window.currentPage = 0
                }
                NavButton {
                    text: "从机"
                    iconText: "▦"
                    selected: window.currentPage === 1
                    onClicked: window.currentPage = 1
                }
                NavButton {
                    text: "命令"
                    iconText: "↯"
                    selected: window.currentPage === 2
                    onClicked: window.currentPage = 2
                }
                NavButton {
                    text: "日志"
                    iconText: "≡"
                    selected: window.currentPage === 3
                    onClicked: window.currentPage = 3
                }
            }

            Item { Layout.fillWidth: true }

            StudioTextField {
                id: endpointField
                Layout.preferredWidth: 196
                implicitHeight: 36
                text: gateway.endpoint
                placeholderText: "http://192.168.1.20"
                onEditingFinished: gateway.endpoint = text
                Keys.onReturnPressed: {
                    gateway.endpoint = text
                    gateway.connectNow()
                }
            }

            StudioButton {
                implicitHeight: 36
                text: gateway.connected ? "重连" : "连接"
                primary: !gateway.connected
                onClicked: {
                    gateway.endpoint = endpointField.text
                    gateway.connectNow()
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 44
                color: "#365779"
            }

            ColumnLayout {
                Layout.preferredWidth: 80
                spacing: 0
                Text {
                    Layout.alignment: Qt.AlignRight
                    text: Qt.formatTime(window.wallClock, "hh:mm:ss")
                    color: "#F3F7FF"
                    font.pixelSize: 17
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.alignment: Qt.AlignRight
                    text: Qt.formatDate(window.wallClock, "yyyy-MM-dd")
                    color: "#9EB8DA"
                    font.pixelSize: 10
                }
            }

            StatusPill {
                text: gateway.connected ? "系统正常" : "连接断开"
                statusColor: gateway.connected ? window.success : window.danger
                pulse: gateway.connected
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: window.currentPage

            // Dashboard
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 18

                    RowLayout {
                        Layout.fillWidth: true
                        PageTitle {
                            heading: "运行总览"
                            subheading: "板端实时状态是唯一数据源 · " + gateway.refreshInterval + " ms 自动刷新"
                        }
                        Item { Layout.fillWidth: true }
                        StudioButton {
                            text: "立即刷新"
                            enabled: !gateway.busy
                            onClicked: gateway.refresh()
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 4
                        columnSpacing: 14
                        rowSpacing: 14

                        MetricCard {
                            Layout.fillWidth: true
                            title: "从机在线"
                            value: gateway.onlineDeviceCount + " / " + (gateway.onlineDeviceCount + gateway.offlineDeviceCount)
                            detail: gateway.offlineDeviceCount + " 台进入离线退避"
                            glyph: "●"
                            accent: window.success
                        }
                        MetricCard {
                            Layout.fillWidth: true
                            title: "RS485 调度"
                            value: Number(window.value(gateway.rs485, "role", 0)) === 1 ? "主机" : "从机"
                            detail: window.value(gateway.rs485, "polls_completed", 0) + " 次完成 · " + window.value(gateway.rs485, "poll_timeouts", 0) + " 次超时"
                            glyph: "↔"
                            accent: window.accent
                        }
                        MetricCard {
                            Layout.fillWidth: true
                            title: "以太网帧"
                            value: window.value(gateway.ethernet, "rx_frames", 0) + " ↓"
                            detail: window.value(gateway.ethernet, "tx_frames", 0) + " ↑ · " + window.value(gateway.ethernet, "rx_errors", 0) + " 错误"
                            glyph: "⌁"
                            accent: "#9f8cff"
                        }
                        MetricCard {
                            Layout.fillWidth: true
                            title: "优先队列"
                            value: window.value(gateway.rs485, "command_queue_depth", 0)
                            unit: "条"
                            detail: window.value(gateway.rs485, "commands_completed", 0) + " 完成 · " + window.value(gateway.rs485, "commands_failed", 0) + " 失败"
                            glyph: "↯"
                            accent: window.warning
                        }
                    }

                    Panel {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 12

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: "从机实时矩阵"
                                    color: "#d9e9f2"
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text: "当前事务  Unit " + window.value(gateway.rs485, "active_unit_id", 0) + " / FC " + window.value(gateway.rs485, "active_function", 0)
                                    color: "#617b91"
                                    font.pixelSize: 10
                                }
                            }

                            GridView {
                                id: dashboardGrid
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: gateway.devices
                                cellWidth: width >= 980 ? width / 2 : width
                                cellHeight: 292
                                boundsBehavior: Flickable.StopAtBounds
                                ScrollBar.vertical: ScrollBar { }

                                delegate: DeviceCard {
                                    width: dashboardGrid.cellWidth - 12
                                    height: dashboardGrid.cellHeight - 12
                                    active: Number(window.value(gateway.rs485, "active_transaction", 0)) !== 0 &&
                                            Number(window.value(gateway.rs485, "active_unit_id", 0)) === unitId
                                    boardUptimeMs: gateway.uptimeSeconds * 1000
                                }
                            }
                        }
                    }
                }
            }

            // Devices
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 18

                    RowLayout {
                        Layout.fillWidth: true
                        PageTitle {
                            heading: "从机矩阵"
                            subheading: "在线、首次失败、离线退避和恢复状态随板端调度实时变化"
                        }
                        Item { Layout.fillWidth: true }
                        StudioButton { text: "编辑轮询表"; primary: true; onClicked: window.populateConfig() }
                    }

                    GridView {
                        id: devicesGrid
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: gateway.devices
                        cellWidth: width >= 1080 ? width / 3 : width >= 720 ? width / 2 : width
                        cellHeight: 292
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { }

                        delegate: DeviceCard {
                            width: devicesGrid.cellWidth - 14
                            height: devicesGrid.cellHeight - 14
                            active: Number(window.value(gateway.rs485, "active_transaction", 0)) !== 0 &&
                                    Number(window.value(gateway.rs485, "active_unit_id", 0)) === unitId
                            boardUptimeMs: gateway.uptimeSeconds * 1000
                        }
                    }
                }
            }

            // Commands
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 18

                    PageTitle {
                        heading: "优先命令"
                        subheading: "写命令由板端插入轮询任务之前执行；HTTP 仅确认入队，最终结果由状态接口回报"
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 18

                        Panel {
                            Layout.fillHeight: true
                            Layout.preferredWidth: 480
                            glowColor: window.warning

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 24
                                spacing: 14

                                Text {
                                    text: "创建 Modbus 写事务"
                                    color: "#eaf4fa"
                                    font.pixelSize: 17
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: "命令进入板端固定深度队列，由 RS485 调度器处理，不会阻塞桌面界面。"
                                    color: "#71879b"
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                }

                                Rectangle { Layout.fillWidth: true; height: 1; color: "#213244" }

                                FieldLabel { text: "目标从机" }
                                ComboBox {
                                    id: commandDevice
                                    Layout.fillWidth: true
                                    model: 10
                                    textRole: ""
                                    delegate: ItemDelegate {
                                        width: commandDevice.width
                                        text: "设备 " + (index + 1) + "  ·  Unit ID " + (index + 1)
                                    }
                                    contentItem: Text {
                                        leftPadding: 12
                                        text: "设备 " + (commandDevice.currentIndex + 1)
                                        color: "#dbeaf3"
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }

                                FieldLabel { text: "命令类型" }
                                ComboBox {
                                    id: commandType
                                    Layout.fillWidth: true
                                    model: ["写单线圈（FC05）", "写单保持寄存器（FC06）"]
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        FieldLabel { text: "寄存器地址" }
                                        SpinBox { id: commandAddress; Layout.fillWidth: true; from: 0; to: 65535; editable: true }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        FieldLabel { text: commandType.currentIndex === 0 ? "线圈值（0 / 1）" : "寄存器值" }
                                        SpinBox {
                                            id: commandValue
                                            Layout.fillWidth: true
                                            from: 0
                                            to: commandType.currentIndex === 0 ? 1 : 65535
                                            editable: true
                                        }
                                    }
                                }

                                Item { Layout.fillHeight: true }

                                StudioButton {
                                    Layout.fillWidth: true
                                    text: "提交到优先队列  ↯"
                                    primary: true
                                    enabled: gateway.connected
                                    onClicked: gateway.sendCommand(commandDevice.currentIndex,
                                                                   commandType.currentIndex,
                                                                   commandAddress.value,
                                                                   commandValue.value)
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 18

                            Panel {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 230

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 22
                                    spacing: 10

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { text: "最近命令"; color: "#dcebf4"; font.pixelSize: 15; font.weight: Font.DemiBold }
                                        Item { Layout.fillWidth: true }
                                        StatusPill {
                                            text: window.value(gateway.lastCommand, "result", "none")
                                            statusColor: window.value(gateway.lastCommand, "result", "none") === "success" ? window.success
                                                                                                                         : window.value(gateway.lastCommand, "result", "none") === "none" ? "#71869b" : window.danger
                                        }
                                    }

                                    GridLayout {
                                        Layout.fillWidth: true
                                        columns: 4
                                        columnSpacing: 30
                                        rowSpacing: 6

                                        FieldLabel { text: "COMMAND ID" }
                                        FieldLabel { text: "UNIT ID" }
                                        FieldLabel { text: "FUNCTION" }
                                        FieldLabel { text: "EXCEPTION" }
                                        Text { text: window.value(gateway.lastCommand, "id", 0); color: "#f1f8fc"; font.pixelSize: 20; font.family: "Cascadia Mono" }
                                        Text { text: window.value(gateway.lastCommand, "unit_id", 0); color: window.accent; font.pixelSize: 20; font.family: "Cascadia Mono" }
                                        Text { text: window.value(gateway.lastCommand, "function", 0); color: window.warning; font.pixelSize: 20; font.family: "Cascadia Mono" }
                                        Text { text: window.value(gateway.lastCommand, "exception", 0); color: window.danger; font.pixelSize: 20; font.family: "Cascadia Mono" }
                                    }
                                }
                            }

                            Panel {
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 22
                                    spacing: 12
                                    Text { text: "调度统计"; color: "#dcebf4"; font.pixelSize: 15; font.weight: Font.DemiBold }
                                    Repeater {
                                        model: [
                                            { name: "已排队", value: window.value(gateway.rs485, "commands_queued", 0), color: window.accent },
                                            { name: "已完成", value: window.value(gateway.rs485, "commands_completed", 0), color: window.success },
                                            { name: "失败", value: window.value(gateway.rs485, "commands_failed", 0), color: window.danger },
                                            { name: "优先派发", value: window.value(gateway.rs485, "priority_dispatches", 0), color: window.warning }
                                        ]
                                        delegate: RowLayout {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            Text { text: modelData.name; color: "#71879b"; font.pixelSize: 12 }
                                            Item { Layout.fillWidth: true }
                                            Text { text: modelData.value; color: modelData.color; font.pixelSize: 16; font.family: "Cascadia Mono" }
                                        }
                                    }
                                    Item { Layout.fillHeight: true }
                                }
                            }
                        }
                    }
                }
            }

            // Logs
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 18

                    RowLayout {
                        Layout.fillWidth: true
                        PageTitle {
                            heading: "运行日志"
                            subheading: "仅记录连接状态变化、配置操作、命令入队和异常，周期轮询成功不会刷屏"
                        }
                        Item { Layout.fillWidth: true }
                        StudioButton { text: "清空日志"; onClicked: gateway.clearLogs() }
                    }

                    Panel {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ListView {
                            id: logList
                            anchors.fill: parent
                            anchors.margins: 12
                            clip: true
                            spacing: 4
                            model: gateway.logs
                            ScrollBar.vertical: ScrollBar { }
                            onCountChanged: positionViewAtEnd()

                            delegate: Rectangle {
                                required property int index
                                required property var modelData
                                width: logList.width
                                height: 42
                                radius: 7
                                color: index % 2 === 0 ? "#0e1924" : "#111d29"

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 14

                                    Text {
                                        text: modelData.time
                                        color: "#587086"
                                        font.family: "Cascadia Mono"
                                        font.pixelSize: 10
                                    }
                                    Rectangle {
                                        width: 6; height: 6; radius: 3
                                        color: modelData.level === "success" ? window.success
                                                                                 : modelData.level === "error" ? window.danger : window.accent
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.message
                                        color: "#c4d6e1"
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                visible: logList.count === 0
                                text: "暂无运行日志"
                                color: "#50677b"
                                font.pixelSize: 13
                            }
                        }
                    }
                }
            }
        }
    }

    footer: Rectangle {
        height: 48
        color: "#081A38"

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: "#24496F"
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 22
            anchors.rightMargin: 22
            spacing: 16

            Text {
                text: gateway.boardName || "ART-Pi STM32H750"
                color: "#F3F7FF"
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            Rectangle { width: 4; height: 4; radius: 2; color: gateway.connected ? window.success : window.danger }
            Text {
                text: gateway.boardIp || "等待板卡"
                color: window.accent
                font.family: "Cascadia Mono"
                font.pixelSize: 11
            }
            Text { text: "运行 " + gateway.uptimeText; color: window.muted; font.pixelSize: 11 }
            Text { text: "更新 " + (gateway.lastUpdated || "—"); color: window.muted; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            Text {
                text: Number(window.value(gateway.rs485, "role", 0)) === 1 ? "RS485 主机调度" : "RS485 从机响应"
                color: window.warning
                font.pixelSize: 11
            }
            Rectangle { width: 1; height: 22; color: "#365779" }
            StudioButton {
                implicitHeight: 32
                text: "网关参数配置"
                enabled: gateway.config.devices !== undefined
                onClicked: window.populateConfig()
            }
        }
    }

    ListModel { id: configDeviceModel }

    Dialog {
        id: configDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(window.width - 70, 1180)
        height: Math.min(window.height - 80, 780)
        modal: true
        focus: true
        padding: 0
        closePolicy: Popup.CloseOnEscape

        background: Rectangle {
            radius: 14
            color: "#0B2147"
            border.width: 1
            border.color: "#2A527D"
        }

        contentItem: ColumnLayout {
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 68
                Layout.leftMargin: 22
                Layout.rightMargin: 22

                ColumnLayout {
                    spacing: 2
                    Text { text: "网关与轮询表配置"; color: "#F3F7FF"; font.pixelSize: 18; font.weight: Font.DemiBold }
                    Text { text: "保存后由 W25Q128 持久化，板端调度器立即采用新配置"; color: "#9EB8DA"; font.pixelSize: 10 }
                }
                Item { Layout.fillWidth: true }
                StudioButton { text: "关闭"; onClicked: configDialog.close() }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#2A527D" }

            GridLayout {
                Layout.fillWidth: true
                Layout.margins: 18
                columns: 7
                columnSpacing: 12
                rowSpacing: 6

                FieldLabel { text: "RS485 角色" }
                FieldLabel { text: "本机 Unit ID" }
                FieldLabel { text: "从机数量" }
                FieldLabel { text: "轮询周期 ms" }
                FieldLabel { text: "长期离线探测" }
                FieldLabel { text: "红灯" }
                FieldLabel { text: "蜂鸣器" }

                ComboBox { id: roleCombo; Layout.fillWidth: true; model: ["从机", "主机"] }
                SpinBox { id: unitIdSpin; Layout.fillWidth: true; from: 1; to: 247; editable: true }
                SpinBox { id: deviceCountSpin; Layout.fillWidth: true; from: 1; to: 10; editable: true }
                SpinBox { id: pollPeriodSpin; Layout.fillWidth: true; from: 100; to: 60000; stepSize: 100; editable: true }
                ComboBox { id: probeCombo; Layout.fillWidth: true; model: ["60 秒", "5 分钟"] }
                Switch { id: redLedSwitch; Layout.alignment: Qt.AlignHCenter }
                Switch { id: buzzerSwitch; Layout.alignment: Qt.AlignHCenter }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#2A527D" }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 22
                Layout.rightMargin: 22
                Layout.topMargin: 10
                Layout.bottomMargin: 7
                FieldLabel { text: "设备 / 超时"; Layout.preferredWidth: 170 }
                FieldLabel { text: "线圈 0x（地址 / 数量）"; Layout.fillWidth: true }
                FieldLabel { text: "离散输入 1x（地址 / 数量）"; Layout.fillWidth: true }
                FieldLabel { text: "保持寄存器 4x（地址 / 数量）"; Layout.fillWidth: true }
                FieldLabel { text: "输入寄存器 3x（地址 / 数量）"; Layout.fillWidth: true }
            }

            ListView {
                id: configList
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                clip: true
                spacing: 7
                model: configDeviceModel
                ScrollBar.vertical: ScrollBar { }

                delegate: Rectangle {
                    id: configDeviceDelegate
                    required property int index
                    required property int unitId
                    required property int timeoutMs
                    required property int coilAddress
                    required property int coilQuantity
                    required property int discreteAddress
                    required property int discreteQuantity
                    required property int holdingAddress
                    required property int holdingQuantity
                    required property int inputAddress
                    required property int inputQuantity

                    width: configList.width
                    height: 76
                    radius: 10
                    color: index < deviceCountSpin.value ? "#102D5A" : "#091A38"
                    border.width: 1
                    border.color: index < deviceCountSpin.value ? "#2A527D" : "#1D3857"
                    opacity: index < deviceCountSpin.value ? 1 : 0.48

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 9
                        spacing: 10

                        RowLayout {
                            Layout.preferredWidth: 198
                            Rectangle {
                                width: 30; height: 30; radius: 8
                                color: "#153A70"
                                Text { anchors.centerIn: parent; text: index + 1; color: window.accent; font.weight: Font.Bold }
                            }
                            ColumnLayout {
                                FieldLabel { text: "UNIT ID / TIMEOUT" }
                                RowLayout {
                                    StudioTextField {
                                        Layout.preferredWidth: 58
                                        implicitHeight: 36
                                        text: String(unitId)
                                        horizontalAlignment: TextInput.AlignHCenter
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        validator: IntValidator { bottom: 1; top: 247 }
                                        onEditingFinished: {
                                            var normalized = Math.max(1, Math.min(247, Number(text)))
                                            text = String(normalized)
                                            configDeviceModel.setProperty(index, "unitId", normalized)
                                        }
                                    }
                                    StudioTextField {
                                        Layout.preferredWidth: 82
                                        implicitHeight: 36
                                        text: String(timeoutMs)
                                        horizontalAlignment: TextInput.AlignHCenter
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        validator: IntValidator { bottom: 20; top: 5000 }
                                        onEditingFinished: {
                                            var normalized = Math.max(20, Math.min(5000, Number(text)))
                                            text = String(normalized)
                                            configDeviceModel.setProperty(index, "timeoutMs", normalized)
                                        }
                                    }
                                }
                            }
                        }

                        Repeater {
                            model: [
                                { address: coilAddress, quantity: coilQuantity, addressRole: "coilAddress", quantityRole: "coilQuantity" },
                                { address: discreteAddress, quantity: discreteQuantity, addressRole: "discreteAddress", quantityRole: "discreteQuantity" },
                                { address: holdingAddress, quantity: holdingQuantity, addressRole: "holdingAddress", quantityRole: "holdingQuantity" },
                                { address: inputAddress, quantity: inputQuantity, addressRole: "inputAddress", quantityRole: "inputQuantity" }
                            ]
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                StudioTextField {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 76
                                    implicitHeight: 36
                                    text: String(modelData.address)
                                    horizontalAlignment: TextInput.AlignHCenter
                                    inputMethodHints: Qt.ImhDigitsOnly
                                    validator: IntValidator { bottom: 0; top: 65535 }
                                    onEditingFinished: {
                                        var normalized = Math.max(0, Math.min(65535, Number(text)))
                                        text = String(normalized)
                                        configDeviceModel.setProperty(configDeviceDelegate.index,
                                                                      modelData.addressRole,
                                                                      normalized)
                                    }
                                }
                                Text { text: "/"; color: "#50677b" }
                                StudioTextField {
                                    Layout.preferredWidth: 50
                                    implicitHeight: 36
                                    text: String(modelData.quantity)
                                    horizontalAlignment: TextInput.AlignHCenter
                                    inputMethodHints: Qt.ImhDigitsOnly
                                    validator: IntValidator { bottom: 0; top: 16 }
                                    onEditingFinished: {
                                        var normalized = Math.max(0, Math.min(16, Number(text)))
                                        text = String(normalized)
                                        configDeviceModel.setProperty(configDeviceDelegate.index,
                                                                      modelData.quantityRole,
                                                                      normalized)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#2A527D" }

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 68
                Layout.leftMargin: 22
                Layout.rightMargin: 22
                Text {
                    Layout.fillWidth: true
                    text: "数量以外的设备配置仍会保留，重新增加从机数量时可继续使用。"
                    color: "#61798e"
                    font.pixelSize: 10
                }
                StudioButton {
                    text: "保存到板卡"
                    primary: true
                    enabled: gateway.connected && configDeviceModel.count > 0
                    onClicked: {
                        var devices = []
                        for (var i = 0; i < configDeviceModel.count; ++i)
                            devices.push(configDeviceModel.get(i))
                        gateway.saveConfiguration({
                            "rs485Role": roleCombo.currentIndex,
                            "modbusUnitId": unitIdSpin.value,
                            "masterDeviceCount": deviceCountSpin.value,
                            "offlineProbeSeconds": probeCombo.currentIndex === 0 ? 60 : 300,
                            "pollPeriodMs": pollPeriodSpin.value,
                            "redLedOn": redLedSwitch.checked ? 1 : 0,
                            "buzzerOn": buzzerSwitch.checked ? 1 : 0
                        }, devices)
                        configDialog.close()
                    }
                }
            }
        }
    }

    Rectangle {
        id: toast
        property string text: ""
        property bool ok: true
        visible: false
        z: 1000
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        width: Math.min(420, toastLabel.implicitWidth + 56)
        height: 52
        radius: 12
        color: "#142332"
        border.width: 1
        border.color: ok ? window.success : window.danger

        Row {
            anchors.centerIn: parent
            spacing: 12
            Rectangle { width: 8; height: 8; radius: 4; color: toast.ok ? window.success : window.danger }
            Text { id: toastLabel; text: toast.text; color: "#e4f1f8"; font.pixelSize: 12 }
        }

        NumberAnimation on opacity {
            id: toastFade
            from: 0
            to: 1
            duration: 180
        }
        onVisibleChanged: if (visible) toastFade.restart()
    }

    Timer {
        id: toastTimer
        interval: 3200
        onTriggered: toast.visible = false
    }

    Timer {
        interval: 1000
        repeat: true
        running: true
        triggeredOnStart: true
        onTriggered: window.wallClock = new Date()
    }

    Connections {
        target: gateway
        function onCommandCompleted(ok, message) { window.showToast(message, ok) }
    }

    Component.onCompleted: gateway.connectNow()
}
