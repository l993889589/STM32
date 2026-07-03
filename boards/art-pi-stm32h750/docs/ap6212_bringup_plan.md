---
title: ART-Pi AP6212 SDIO + NetX Duo 联网调试记录
category: STM32 / Wi-Fi
tags:
  - STM32H750
  - ART-Pi
  - AP6212
  - SDIO
  - QSPI
  - NetX Duo
  - ThreadX
summary: 记录 ART-Pi 板载 AP6212 从 SDIO 探测、QSPI 固件加载、Wi-Fi 入网，到 NetX Duo DHCP 获取地址和 ping 网关的完整进度。
project: D:\Embedded\artpi
updated: 2026-06-30
---

# ART-Pi AP6212 SDIO + NetX Duo 联网调试记录

## 当前结论

AP6212 已经通过 SDIO 启动固件、连接 Wi-Fi，并接入 NetX Duo 完成 DHCP 和网关 ping 验证。

最终验证使用的是正常 UDP checksum 配置，没有保留 `NX_DISABLE_UDP_RX_CHECKSUM` 或 `NX_DISABLE_UDP_TX_CHECKSUM` 这类临时规避项。

UART4 只作为日志串口使用；AP6212 的数据面走 SDIO F2，不走 UART4。

## 最终验证

构建命令：

```powershell
& 'C:\Keil_v5\UV4\UV4.exe' -r 'D:\Embedded\artpi\MDK-ARM\ARTPI.uvprojx' -o 'D:\Embedded\artpi\MDK-ARM\build_ap6212_netx_checksum_on.log'
```

构建结果：

```text
Program Size: Code=123254 RO-data=6158 RW-data=180 ZI-data=79652
"ARTPI\ARTPI.axf" - 0 Error(s), 0 Warning(s).
Build Time Elapsed:  00:00:27
```

烧录工具：

```text
STM32CubeProgrammer v2.15.0
ST-LINK SN  : 066BFF393732484257163418
ST-LINK FW  : V2J40M27
Voltage     : 3.26V
Device name : STM32H7xx
```

UART4 验证日志文件：

```text
D:\Embedded\artpi\MDK-ARM\serial_ap6212_netx_checksum_on.log
```

关键日志：

```text
[qspi] JEDEC EF 40 17 mode=1 capacity=8388608 bytes
[ap6212-fw] bundle version=1 total=375547 header=64 crc=OK
[ap6212-fw] fw off=64 len=374608 crc=0x0D1EA341 read=0x0D1EA341
[ap6212-fw] nvram off=374672 len=875 crc=0xF0FDB414 read=0xF0FDB414
[wifi] fwver=wl0: Oct  8 2016 15:31:47 version 7.46.57.4.ap.r4 (A1 Station/P2P) FWID 01-3621395e es6.c5.n4.a3
[wifi] associated ssid=CU_eaJU len=7
[netx] ip=192.168.1.18
[netx] mask=255.255.255.0
[netx] gateway=192.168.1.1
[netx] dns=192.168.1.1
[netx] ping gateway OK
```

## 完成清单

- [x] UART4 日志输出保留，用于启动、Wi-Fi、NetX Duo 状态观测。
- [x] SDIO CMD5 / OCR / RCA / CCCR / CIS / F1 / F2 探测通过。
- [x] AP6212 F1 sleepcsr / chipclk / backplane 访问通过。
- [x] QSPI 识别 W25Q64，容量 8 MiB。
- [x] AP6212 固件和 nvram 以 bundle 形式放入 QSPI，并在启动时校验 CRC。
- [x] 固件下载到 AP6212 RAM，release CM3 后 SDPCM ready。
- [x] BCDC 控制通道可查询版本、MAC、固件版本。
- [x] Wi-Fi 参数下发、WPA2 入网、BSSID/SSID 校验通过。
- [x] NetX Duo packet pool、IP、ARP、ICMP、UDP、DHCP 初始化通过。
- [x] DHCP 获取 IP、mask、gateway、DNS。
- [x] ICMP ping gateway 成功。
- [x] UDP checksum 保持正常打开后重新验证通过。

## 硬件和接口边界

当前工程里的角色划分如下：

| 模块 | 作用 |
| --- | --- |
| UART4 | 只做日志输出，不承载 AP6212 网络数据 |
| ST-LINK VCP | PC 侧抓 UART4 日志，当前为 COM19 @ 115200 |
| SDIO / SDMMC2 | AP6212 Wi-Fi 主数据和控制接口 |
| QSPI | 存放 AP6212 固件 bundle 和 nvram |
| AP6212 | Broadcom/Cypress Wi-Fi 芯片，固件启动后提供 SDPCM/BCDC 数据通道 |
| NetX Duo | MCU 侧 TCP/IP 协议栈，当前验证 DHCP + ICMP |

这点很重要：UART4 不是 AP6212 通信接口。UART4 只是让我们看到初始化、联网和协议栈状态。

## 当前数据流

发送方向：

```text
NetX Duo DHCP / ICMP
  -> nx_ap6212_driver
  -> app_ap6212_wifi_send_ethernet()
  -> SDPCM data channel
  -> SDIO F2
  -> AP6212 firmware
  -> Wi-Fi AP
```

接收方向：

```text
Wi-Fi AP
  -> AP6212 firmware
  -> SDIO F2
  -> SDPCM data channel
  -> app_ap6212_wifi_receive_ethernet()
  -> nx_ap6212_driver RX thread
  -> NetX Duo IP / ARP / UDP / ICMP
```

控制方向：

```text
app_ap6212_sdio_probe_start()
  -> QSPI 读取 firmware/nvram
  -> SDIO backplane 下载固件
  -> SDPCM control channel
  -> BCDC ioctl 配置 Wi-Fi
  -> Wi-Fi join 完成后置 ready
  -> app_netxduo_init() / DHCP
```

## 关键文件

| 文件 | 职责 |
| --- | --- |
| `user/app/app_config.h` | 打开 `APP_ENABLE_AP6212_NETXDUO`，配置 NetX Duo 栈、包池、Wi-Fi SSID/密码别名 |
| `Core/Src/app_threadx.c` | ThreadX 启动入口，初始化 UART4、AP6212、NetX Duo |
| `user/app/app_ap6212_sdio_probe.c` | 当前 AP6212 bring-up 主体，包含 SDIO、QSPI 固件加载、SDPCM、BCDC、Wi-Fi join 和以太网帧收发 API |
| `user/app/app_ap6212_sdio_probe.h` | 暴露 NetX Duo 需要的 AP6212 Wi-Fi ready、MAC、send、receive API |
| `NetXDuo/App/app_netxduo.c` | NetX Duo 应用层初始化，创建 packet pool、IP、ARP、ICMP、UDP、DHCP，并打印 DHCP 结果 |
| `NetXDuo/App/nx_ap6212_driver.c` | NetX Duo 链路驱动，负责 Ethernet frame 与 AP6212 SDPCM data channel 之间转换 |
| `NetXDuo/Target/nx_user.h` | NetX Duo 编译配置，当前保留 UDP checksum |
| `MDK-ARM/ARTPI.uvprojx` | Keil 工程，已加入 NetX Duo 源码、include path 和 AP6212 driver 文件 |
| `firmware/ap6212/ap6212_qspi_bundle.bin` | 已验证的 AP6212 firmware + nvram bundle |

## 关键修复和原因

### 1. F2 发送分块

AP6212 的 SDIO F2 发送不能简单把大包一次性塞进去。当前实现增加了 `app_ap6212_write_f2_stream()`，把 F2 写入拆成不超过 512 字节的块。

这解决的是 SDPCM data/control 帧长度变大后偶发失败的问题，尤其是 DHCP 报文、固件阶段控制帧和后续大包场景。

### 2. RX 包 2 字节偏移对齐

NetX Duo 收到 IPv4 包后，对 IP header 的 32-bit 对齐比较敏感。AP6212 给上来的 Ethernet frame 是 14 字节以太网头，如果直接从 packet 起始地址放入，去掉 14 字节头以后 IPv4 header 会变成非 4 字节对齐。

当前在 `nx_ap6212_driver.c` 里分配 RX packet 后先加 2 字节偏移：

```c
packet_ptr->nx_packet_prepend_ptr += 2U;
packet_ptr->nx_packet_append_ptr += 2U;
```

然后再 append Ethernet frame。这样剥掉 14 字节 Ethernet header 后，IPv4 header 恰好重新 4 字节对齐。

这个是 DHCP Offer 之前进了驱动但 NetX Duo 不接收的根因。

### 3. DHCP 前 ARP 处理

DHCP 未拿到 IP 之前，ARP 包会出现，但此时 NetX Duo interface address 还是 0。当前驱动在 interface IP 未配置前丢弃 ARP，避免把早期 ARP 喂给还没完成地址配置的协议栈。

DHCP bound 之后，ARP 再交给 `_nx_arp_packet_deferred_receive()`。

### 4. Raw smoke 和 NetX Duo 互斥

以前 raw smoke 测试会手工发一些网络帧。接入 NetX Duo 后，raw smoke 默认关闭：

```c
#define APP_AP6212_ENABLE_RAW_NET_SMOKE (APP_ENABLE_AP6212_NETXDUO ? 0U : 1U)
```

原因是同一块 AP6212 数据通道不应该同时由 raw 测试代码和 NetX Duo 链路驱动抢收包、抢发包。现在网络帧所有权交给 NetX Duo。

### 5. DNS 结果打印

DHCP option 中拿到的 DNS 地址需要按当前 NetX Duo API 返回值正确解释。现在日志显示：

```text
[netx] dns=192.168.1.1
```

这个结果已经和网关一致，符合当前路由器配置。

### 6. UDP checksum 恢复正常

中间排查阶段曾怀疑 checksum 问题。最终确认根因是 RX 对齐，不是 checksum。因此最终版本移除了 UDP RX/TX checksum 禁用宏，并重新验证 DHCP/ping 通过。

## 和 LDC / endpoint / ldc_queue 的关系

AP6212 Wi-Fi 数据面不是字节流协议，也不是 UART framed protocol。它是 SDIO + SDPCM + Ethernet frame，再往上接 NetX Duo。

所以 AP6212 Wi-Fi 不适合直接按 LDC endpoint 或 ldc_queue 方式建模。LDC 更适合：

- UART、RS485、USB CDC 这类连续字节流。
- 外设协议需要按帧解析、排队、转发的场景。
- 需要一个安全的收包队列和任务通知模型，替代频繁轮询 pop。

AP6212 当前更合理的边界是：

```text
AP6212 SDIO driver
  -> Ethernet frame send/receive API
  -> NetX Duo link driver
  -> TCP/IP sockets / DHCP / ICMP / UDP / TCP
```

如果后续调 AP6212 蓝牙部分，蓝牙 HCI 通常会走 UART HCI 字节流，那时反而可以评估用 LDC 或 ldc_queue 包一层 HCI transport。但 Wi-Fi SDIO 数据面不建议这么做。

## 和之前 endpoint/channel 方式的对比

| 维度 | endpoint/channel | ldc_queue | 当前 AP6212 NetX Duo |
| --- | --- | --- | --- |
| 适用对象 | 应用层 endpoint 收发 | 字节流帧队列 | Ethernet frame + TCP/IP |
| 数据边界 | endpoint 自己定义 | LDC 帧边界 | Ethernet / ARP / IP / UDP / ICMP |
| 调度方式 | endpoint 分散初始化较多 | queue 可用信号量/事件唤醒 pop 任务 | NetX Duo 自己调度 IP/DHCP/ARP/ICMP |
| 效率 | 取决于 endpoint 封装和任务模型 | 比纯轮询好，可由 RX 完整帧发布事件 | 对 AP6212 最合适，避免重复解析网络协议 |
| 安全性 | 容易出现绑定分散、所有权不清 | 边界更清晰，可统一 overflow/drop 策略 | 由 NetX Duo 管协议，driver 只管链路帧 |
| 调试性 | endpoint 多时定位会分散 | 队列状态、drop 计数更好查 | UART4 打印协议栈状态，抓 Ethernet 级别日志 |

结论：串口类外设后续继续按 `ldc_queue` 方向做更顺手；AP6212 Wi-Fi 这条线应该交给 NetX Duo link driver，不要强行塞进 LDC。

## CubeMX 同步状态

当前已同步到 Keil 工程 `MDK-ARM/ARTPI.uvprojx`：

- 增加 `NX_INCLUDE_USER_DEFINE_FILE`。
- 增加 `NetXDuo/Target`、`NetXDuo/App`、`Middlewares/ST/netxduo/...` include path。
- 增加 NetX Duo common 源码、DHCP addon 源码和 AP6212 NetX driver 源码分组。

当前没有把这些自定义文件完整写回 `ARTPI.ioc`。原因是这次接入包含自定义 AP6212 link driver、QSPI firmware bundle 和手工加入的 NetX Duo source group，直接改 `.ioc` 容易让 CubeMX 生成出一套不等价的默认 NetX Duo 配置。

建议后续处理方式：

- CubeMX 只继续负责 pin/clock/peripheral 基础配置。
- NetX Duo 自定义 app/driver 文件继续由 Keil 工程维护。
- 如果必须让 CubeMX 知道 NetX Duo，需要单独开一个任务，只做 `.ioc` 级别同步并重新生成后比对 `uvprojx`，不能盲目覆盖。

## 当前遗留任务

1. 拆分 `app_ap6212_sdio_probe.c`。

   现在这个文件已经承担了 SDIO 探测、固件加载、SDPCM、BCDC、Wi-Fi join、以太网帧收发。它适合作为 bring-up 文件，但不适合长期维护。建议拆成：

   ```text
   bsp/drivers/ap6212/ap6212_sdio.c
   bsp/drivers/ap6212/ap6212_fw.c
   bsp/drivers/ap6212/ap6212_sdpcm.c
   bsp/drivers/ap6212/ap6212_bcdc.c
   bsp/drivers/ap6212/ap6212_wifi.c
   NetXDuo/App/nx_ap6212_driver.c
   ```

2. 增加 host wake / data ready 中断。

   当前 RX thread 以 timeout 方式收包，能工作，但不是最终效率最好的方式。后续应使用 AP6212 host wake 或 SDIO interrupt 通知 RX thread，减少轮询延迟和 CPU 空转。

3. 增加错误计数和恢复策略。

   至少记录：

   ```text
   SDIO CMD52/CMD53 error
   F2 short read/write
   SDPCM bad checksum
   unexpected channel
   NetX packet allocate failure
   RX overflow/drop
   Wi-Fi disconnect/rejoin count
   DHCP timeout count
   ```

4. 增加 socket 级验证。

   目前验证到 DHCP 和 ICMP。下一步可以验证：

   ```text
   UDP echo
   TCP connect
   DNS query
   HTTP GET
   长时间 ping / DHCP renew
   ```

5. 处理 Wi-Fi 配置来源。

   当前 SSID/password 使用 `APP_W800_WIFI_SSID` 和 `APP_W800_WIFI_PASSWORD` 的别名，适合快速验证。后续应改成 AP6212 独立配置，并避免把真实密码写进公开文档或仓库。

6. 决定正式日志等级。

   当前 `APP_AP6212_NET_TRACE` 默认为 0，但 SDPCM/BCDC bring-up 仍有较多日志。后续建议分级：

   ```text
   error
   state
   control
   frame
   dump
   ```

## 复现步骤

1. 确认 QSPI 中已有 firmware bundle：

   ```text
   D:\Embedded\artpi\firmware\ap6212\ap6212_qspi_bundle.bin
   ```

2. 编译：

   ```powershell
   & 'C:\Keil_v5\UV4\UV4.exe' -r 'D:\Embedded\artpi\MDK-ARM\ARTPI.uvprojx' -o 'D:\Embedded\artpi\MDK-ARM\build_ap6212_netx_checksum_on.log'
   ```

3. 通过 ST-LINK 烧录：

   ```powershell
   & 'C:\Program Files (x86)\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe' -c port=SWD mode=UR reset=HWrst -w 'D:\Embedded\artpi\MDK-ARM\ARTPI\ARTPI.hex' -v -rst
   ```

4. 打开 UART4 日志串口：

   ```text
   COM19, 115200, 8N1
   ```

5. 看到以下日志即可认为当前基础联网验证通过：

   ```text
   [wifi] associated ssid=...
   [netx] ip=...
   [netx] mask=...
   [netx] gateway=...
   [netx] dns=...
   [netx] ping gateway OK
   ```

## 下一步建议

优先顺序建议如下：

1. 保留当前可运行版本，不再在这个基础上大面积重构。
2. 单独开任务把 AP6212 bring-up 文件拆层，拆完每一步都编译和上板验证。
3. 增加 host wake / SDIO interrupt 唤醒 RX thread。
4. 加错误计数和恢复策略。
5. 做 UDP/TCP/DNS/HTTP 的 socket 级验证。
6. 再考虑 CubeMX `.ioc` 的可再生成策略。

当前最重要的里程碑已经完成：AP6212 不是只 probe OK，而是已经跑到 DHCP 获取 IP 和 ping gateway OK。
