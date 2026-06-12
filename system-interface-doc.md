# 系统分层接口文档

> **上位机** = desktop-debug-assistant (Electron + React)
> **Boot** = STM32H563_Bootloader (Bare-metal, no RTOS)
> **App** = STM32H563_Threadx_usbx_cdc_acm (ThreadX RTOS + USBX)

---

## 1. 系统总览

```
┌──────────────────────────────────────────────────────────┐
│                    上位机 (PC Electron)                   │
│  serialport │ aedes MQTT │ Keil/STM32CubeProgrammer CLI  │
└──────┬───────────┬────────────────────┬──────────────────┘
       │           │                    │
   USB CDC     Wi-Fi/MQTT         SWD (烧录)
   (串口)      (局域网)           (调试器)
       │           │                    │
┌──────┴───────────┴────────────────────┴──────────────────┐
│                    STM32H563 MCU                          │
│  ┌────────────────────┐    ┌──────────────────────────┐  │
│  │   Boot (0x08000000) │───▶│  App  (0x08020000)       │  │
│  │   128 KB            │JUMP│  1920 KB                 │  │
│  └────────┬───────────┘    └────┬──────┬──────┬───────┘  │
│           │SPI                  │USB   │UART  │UART      │
│           ▼                     │CDC   │RS485 │W800 AT   │
│     GD25LQ128                   │ACM   │      │          │
│     16MB NOR                    ▼      ▼      ▼          │
│                            PC上位机  外部设备  W800 WiFi  │
└──────────────────────────────────────────────────────────┘
```

---

## 2. Flash 地址空间布局 (Boot 与 App 共享)

| 区域 | 地址范围 | 大小 | 说明 |
|------|----------|------|------|
| Bootloader | `0x0800_0000` – `0x0801_FFFF` | 128 KB | Boot 代码，不可被 OTA 覆写 |
| Application | `0x0802_0000` – `0x081F_FFFF` | 1920 KB | App 代码，OTA 目标区域 |
| SRAM | `0x2000_0000` – `0x2009_FFFF` | 640 KB | 运行时内存 |

---

## 3. 外部 SPI NOR Flash 布局 (GD25LQ128, 16MB)

| 区域 | 地址 | 大小 | 用途 |
|------|------|------|------|
| Manifest A | `0x0000_0000` | 4 KB | OTA 清单主副本 |
| Manifest B | `0x0000_1000` | 4 KB | OTA 清单备份副本 |
| Status Log | `0x0000_2000` | 4 KB | 状态日志(预留) |
| Download Slot | `0x0010_0000` | 6 MB | OTA 下载区 (App 镜像暂存) |
| Backup Slot | `0x0080_0000` | 6 MB | 旧版 App 备份区 (回滚用) |

---

## 4. Boot → App 跳转接口

Boot 完成 OTA 检查后，若 App 有效则跳转：

| 步骤 | 操作 | 说明 |
|------|------|------|
| 1 | 校验 App 向量表 | SP 在 SRAM 范围内, Reset Handler 在 Flash 范围内且 bit0=1 |
| 2 | 关全局中断 | `__disable_irq()` |
| 3 | 清 SysTick | CTRL=0, LOAD=0, VAL=0 |
| 4 | 清所有 NVIC 中断 | ICER[0..7] = 0xFFFFFFFF, ICPR[0..7] = 0xFFFFFFFF |
| 5 | 设 VTOR | `SCB->VTOR = 0x08020000` |
| 6 | 设 MSP | `__set_MSP(app_sp)` |
| 7 | 跳转 | `app_entry()` — App 的 Reset_Handler |

---

## 5. OTA Manifest 结构体 (Boot 与 App 共享定义)

定义文件: `ota_layout.h` (Boot 和 App 各持有一份相同副本)

```c
typedef struct {
    uint32_t magic;            // 0x4F54414D "OTAM"
    uint32_t version;          // 1
    uint32_t boot_state;       // ota_boot_state_t 枚举
    uint32_t image_size;       // App 镜像大小 (字节)
    uint32_t image_version;    // 镜像版本号
    uint32_t image_flags;      // 加密/签名标志
    uint32_t image_crc32;      // App 镜像 CRC32
    uint32_t package_address;  // 外部 Flash 中的包地址
    uint32_t package_size;     // 包大小
    uint32_t rollback_address; // 回滚备份地址
    uint32_t rollback_size;    // 回滚备份大小
    uint32_t rollback_crc32;   // 回滚备份 CRC32
    uint32_t load_address;     // 加载地址 = 0x08020000
    uint32_t entry_address;    // 入口地址 = Reset_Handler
    uint8_t  image_sha256[32];
    uint8_t  package_sha256[32];
    uint8_t  signature[64];
    uint32_t manifest_crc32;   // 本结构体的 CRC32 (此字段为0时计算)
} ota_manifest_t;              // 188 字节
```

### OTA 状态机

```
NORMAL ──(上位机写Manifest)──▶ PENDING_UPDATE
PENDING_UPDATE ──(Boot校验通过)──▶ INSTALLING
INSTALLING ──(写入Internal Flash成功)──▶ TRIAL_BOOT
TRIAL_BOOT ──(App确认存活)──▶ CONFIRMED
TRIAL_BOOT ──(App未确认, 再次重启)──▶ ROLLBACK_REQUIRED
ROLLBACK_REQUIRED ──(Boot回滚成功)──▶ NORMAL
```

---

## 6. 上位机 ↔ App 接口 (USB CDC ACM)

物理通道: USB Virtual COM Port (CDC ACM), VID=0x0483, PID=0x5740

### 6.1 USB CDC 日志/调试 (明文文本)

| 方向 | 内容 | 说明 |
|------|------|------|
| App → PC | `"usb rx: <data>\r\n"` + `"usb hex: <hex>\r\n"` | LDC 组帧后的 USB 接收数据回显 |
| App → PC | `"ota confirm: ok\r\n"` 等 | OTA 确认日志 |
| App → PC | `"w800 state: ...\r\n"` | W800 WiFi/MQTT 状态日志 |
| App → PC | `printf()` 输出 | 所有 stdout 经 `fputc()` 重定向到 USB CDC |
| PC → App | 任意字节 | 进入 `app_usb_cdc_process_rx()`, 优先尝试 OTA 解析 |

### 6.2 OTA 传输协议 (二进制帧, PC → App)

上位机通过 USB CDC 向 App 发送 OTA 帧，App 写入外部 Flash。

**帧格式 (小端序, CRC16 Modbus):**

```
[4C 44 4F 54] [CMD] [00] [SEQ_L SEQ_H] [ADDR_0 ADDR_1 ADDR_2 ADDR_3] [LEN_L LEN_H] [00 00] [PAYLOAD...] [CRC_L CRC_H]
  Magic(4B)    cmd   pad   seq(2B)        address(4B)                   len(2B)       pad    payload       crc16(2B)
```

- Magic: `"LDOT"` = `0x4C 0x44 0x4F 0x54`
- Header 固定 16 字节, Payload 最大 224 字节

**命令定义:**

| CMD | 名称 | Payload | 说明 |
|-----|------|---------|------|
| 1 | BEGIN | 8 字节: `[image_size(4B LE)] [image_crc32(4B LE)]` | 擦除 Download Slot, 准备接收 |
| 2 | DATA | 镜像数据块 (≤224B) | address = base + offset, seq 递增 |
| 3 | MANIFEST | 188 字节 ota_manifest_t | 写入 Manifest A (addr=0x0000) 或 B (addr=0x1000) |
| 4 | END | 空 | 确认接收完毕 |
| 5 | RESET | 空 | 软件复位, 进入 Boot 执行安装 |

**ACK 响应 (App → PC, 明文 ASCII):**

```
ota ack <cmd> <seq> <status>\r\n
```

| Status | 含义 |
|--------|------|
| 0 | OK |
| 1 | BAD_FRAME (帧格式/长度错误) |
| 2 | BAD_CRC |
| 3 | BAD_RANGE (地址越界) |
| 4 | FLASH_ERROR (擦写失败) |
| 5 | SEQUENCE (seq 或 offset 不匹配) |

**OTA 完整流程:**

```
PC                              App (USB CDC)
 │                                │
 │──── BEGIN (size+crc) ─────────▶│ 擦除 Download Slot
 │◀── "ota ack 1 0 0" ───────────│
 │                                │
 │──── DATA seq=1 offset=0 ──────▶│ 写入 ext flash
 │◀── "ota ack 2 1 0" ───────────│
 │──── DATA seq=2 offset=224 ────▶│
 │◀── "ota ack 2 2 0" ───────────│
 │    ...                         │
 │                                │
 │──── MANIFEST A ───────────────▶│ 写入 Manifest A
 │◀── "ota ack 3 N 0" ───────────│
 │──── MANIFEST B ───────────────▶│ 写入 Manifest B
 │◀── "ota ack 3 N+1 0" ─────────│
 │                                │
 │──── END ──────────────────────▶│ 校验完整性
 │◀── "ota ack 4 N+2 0" ─────────│
 │                                │
 │──── RESET ────────────────────▶│ NVIC_SystemReset()
 │    (USB断开)                    │
 │                                ▼
 │                          Boot 重新上电
 │                          读取 Manifest → PENDING_UPDATE
 │                          校验 ext flash CRC32
 │                          备份当前 App → Backup Slot
 │                          擦除 Internal Flash App 区
 │                          从 ext flash 拷贝到 Internal Flash
 │                          校验 Internal Flash CRC32
 │                          写 Manifest → TRIAL_BOOT
 │                          跳转 App
 │                                │
 │◀── "ota confirm: ok" ─────────│ App 写 Manifest → CONFIRMED
```

### 6.3 LDC 压测协议 (PC ↔ App, 二进制帧)

**请求帧 (PC → App):**

```
AA 55 [LEN_H LEN_L] [SEQ_3 SEQ_2 SEQ_1 SEQ_0] [TYPE=0x01] [PAYLOAD...] [CRC_L CRC_H]
```

- Header: `0xAA 0x55`
- LEN: body 长度 (从 SEQ 到 PAYLOAD 结尾)
- SEQ: 32 位递增序号 (大端)
- TYPE: `0x01` (echo/ack 测试)
- PAYLOAD: 支持递增/伪随机/固定 0x55 三种模式
- CRC16: Modbus CRC (小端序)

**ACK 帧 (App → PC):**

```
55 AA [00 05] [SEQ_3 SEQ_2 SEQ_1 SEQ_0] [STATUS] [CRC_L CRC_H]
```

- Header: `0x55 0xAA`
- LEN: 固定 5
- STATUS: `0x00` = OK, 非零 = 错误

---

## 7. 上位机 ↔ App 接口 (RS485 Modbus RTU)

物理通道: USB-RS485 转换器 → MCU UART (115200 8N1)

### Modbus RTU 协议

| 方向 | 功能码 | 说明 |
|------|--------|------|
| PC → App | `0x03` | 读保持寄存器 (从站号=1, 寄存器数=64) |
| App → PC | `0x03` | 响应: 寄存器数据 |
| App → PC | `0x83` | 异常响应 |

**帧格式:**

```
[UNIT] [FUNC] [START_H START_L] [COUNT_H COUNT_L] [CRC_L CRC_H]
```

- 默认从站号: 1
- 默认寄存器起始: 0x0000
- 默认寄存器数量: 64 (初始值 = 地址值, 即 reg[i] = i)

上位机支持单次请求和高压压测模式 (可设次数/间隔/超时)。

---

## 8. 上位机 ↔ W800 接口 (MQTT over WiFi)

物理通道: W800 WiFi 模组 ↔ 局域网 ↔ PC 上位机 MQTT Broker

### MQTT Topic 约定

| 方向 | Topic | Payload |
|------|-------|---------|
| W800 → PC | `leduo/w800/up` | 通用遥测 JSON |
| W800 → PC | `leduo/w800/status` | 设备状态 JSON |
| W800 → PC | `leduo/w800/log` | 日志 JSON |
| PC → W800 | `leduo/pc/down` | 命令 JSON |

### 状态 JSON (W800 → PC)

```json
{
  "deviceId": "leduo-h563-w800",
  "online": true,
  "ip": "192.168.1.88",
  "rssi": -47,
  "fw": "0.1.0",
  "uptime": 123456,
  "mode": "online"
}
```

### PC 下发命令 JSON

```json
{"cmd": "ping"}
{"cmd": "status"}
{"cmd": "log", "enable": true}
{"cmd": "log", "enable": false}
{"cmd": "reboot"}
```

### W800 联网状态机 (App 侧)

```
RESET → WIFI_JOIN → MQTT_SOCKET → MQTT_CONNECT → ONLINE
                                                  │
                                              heartbeat fail
                                                  ▼
                                            MQTT_RETRY → (重连)
```

---

## 9. 上位机 ↔ MCU 接口 (SWD 烧录)

上位机通过调用 `flash_ota_all.ps1` 脚本完成:

| 步骤 | 工具 | 操作 |
|------|------|------|
| 1 | Keil UV4.exe | 编译 Bootloader 工程 |
| 2 | Keil UV4.exe | 编译 App 工程 |
| 3 | STM32_Programmer_CLI | 烧录 Bootloader HEX 到 `0x08000000` |
| 4 | STM32_Programmer_CLI | 烧录 App HEX 到 `0x08020000` |
| 5 | STM32_Programmer_CLI | 软件复位 + Core Run |

---

## 10. 上位机 OTA 包制作流程

上位机调用 `make_ota_package.ps1` 脚本:

| 输入 | 处理 | 输出 |
|------|------|------|
| `.axf` / `.elf` | `fromelf --bin` | `app.bin` |
| `.hex` | Intel HEX → BIN 转换 | `app.bin` |
| `.bin` | 直接拷贝 | `app.bin` |

生成产物:

| 文件 | 说明 |
|------|------|
| `app.bin` | App 二进制镜像 |
| `manifest.bin` | 188 字节 OTA Manifest (boot_state=PENDING_UPDATE) |
| `manifest.json` | Manifest 可读 JSON 摘要 |

---

## 11. 上位机 IPC 接口 (Electron preload API)

`window.debugAssistant` 暴露的全部方法:

| 方法 | IPC Channel | 说明 |
|------|-------------|------|
| `listPorts()` | `serial:list` | 枚举系统串口 |
| `openPort(options)` | `serial:open` | 打开串口 (channel="log"/"modbus") |
| `closePort(options)` | `serial:close` | 关闭串口 |
| `writePort(payload)` | `serial:write` | 写串口 (支持 hex/ascii) |
| `getLogPaths()` | `logs:paths` | 获取日志文件路径 |
| `getMqttStatus()` | `mqtt:status` | MQTT Broker 状态 |
| `startMqtt(options)` | `mqtt:start` | 启动 MQTT Broker (aedes) |
| `stopMqtt()` | `mqtt:stop` | 停止 MQTT Broker |
| `publishMqtt(payload)` | `mqtt:publish` | 发布 MQTT 消息 |
| `simulateW800(options)` | `mqtt:simulate-w800` | 模拟 W800 设备上报 |
| `getOtaInfo()` | `ota:info` | 获取默认 OTA 包信息 |
| `listOtaPackages()` | `ota:list` | 列出所有 OTA 包 |
| `pickOtaInput()` | `ota:pick-input` | 文件选择对话框 |
| `convertOtaPackage(options)` | `ota:convert` | 调用 make_ota_package.ps1 |
| `startOta(options)` | `ota:start` | 执行 OTA 写入流程 |
| `stopOta()` | `ota:stop` | 取消 OTA |
| `runBoardFlash(options)` | `board:flash` | 编译+烧录+重启 |
| `stopBoardFlash()` | `board:flash-stop` | 停止烧录流程 |

**事件回调 (主进程 → 渲染进程):**

| 方法 | 事件 | 说明 |
|------|------|------|
| `onData(cb)` | `serial:data` | 串口接收数据 |
| `onError(cb)` | `serial:error` | 串口错误 |
| `onClosed(cb)` | `serial:closed` | 串口关闭 |
| `onMqttEvent(cb)` | `mqtt:event` | MQTT 事件 |
| `onOtaProgress(cb)` | `ota:progress` | OTA 进度更新 |
| `onBoardFlashOutput(cb)` | `board:flash-output` | 烧录流程输出 |
| `onBoardFlashDone(cb)` | `board:flash-done` | 烧录流程完成 |

---

## 12. 各层关键源文件索引

### 上位机 (desktop-debug-assistant)

| 文件 | 职责 |
|------|------|
| `electron/main.js` | Electron 主进程: 串口管理, OTA 帧构建/发送, MQTT Broker, 烧录脚本调用 |
| `electron/preload.cjs` | IPC 桥接: 暴露 `window.debugAssistant` API |
| `src/App.jsx` | React UI: 双通道调试, Modbus, LDC 压测, W800 MQTT, OTA 管理 |
| `docs/ldc-stress-protocol.md` | LDC 压测协议文档 |
| `docs/w800-mqtt-protocol.md` | W800 MQTT 协议文档 |

### Bootloader (STM32H563_Bootloader)

| 文件 | 职责 |
|------|------|
| `Core/Src/main.c` | Boot 入口: 时钟配置, SPI NOR 初始化, OTA 检查, App 跳转 |
| `user/ota_layout.h` | Flash 布局定义 + Manifest 结构体 (与 App 共享) |
| `user/ota_boot.c` | OTA 核心: Manifest 读写, 镜像校验, Flash 编程, 备份/回滚 |
| `user/ota_boot.h` | OTA 结果枚举 |
| `user/gd25lq128.c/h` | SPI NOR Flash 驱动 (读/写/擦除/校验) |

### App (STM32H563_Threadx_usbx_cdc_acm)

| 文件 | 职责 |
|------|------|
| `user/app/app_board_io.c/h` | 核心 I/O: USB CDC 收发, OTA 帧解析, RS485 Modbus, W800 AT |
| `user/app/app_ota.c/h` | OTA 确认: TRIAL_BOOT → CONFIRMED 状态转换 |
| `user/app/app_config.h` | 全局配置常量 (OTA magic, buffer 大小, MQTT 参数等) |
| `user/app/ota_layout.h` | Flash 布局定义 (与 Boot 相同) |
| `user/app/mqtt_packet.c/h` | MQTT CONNECT/PUBLISH 报文构建 |
| `user/ldc/ldc_core.c/h` | LDC 帧检测引擎 (环形缓冲 + 包队列) |
| `user/ldc/ldc_proto_dispatcher.c/h` | LDC 协议分发器 |
| `user/protocol/modbus/modbus_slave.c/h` | Modbus RTU 从站实现 |
| `user/protocol/modbus/modbus_rtu.c/h` | Modbus RTU CRC + 帧工具 |
| `user/at/core/at_session.c/h` | AT 指令会话引擎 |
| `user/at/modules/at_module_w800.c/h` | W800 WiFi 模组 AT 驱动 |
| `user/bsp/bsp.c/h` | BSP: GPIO, UART, SPI, LED 硬件抽象 |
