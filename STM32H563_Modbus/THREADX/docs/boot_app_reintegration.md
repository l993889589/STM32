# 从 standalone 回迁产品 Boot + App

## 已核对的产品契约

本文件只记录只读审计结果，没有修改 `STM32H563_App`、
`STM32H563_Bootloader` 或 `shared/ota`。

| 区域 | 起始地址 | 大小 | VTOR |
| --- | --- | --- | --- |
| 产品 Boot | `0x08000000` | `0x00020000` | `0x00000` |
| 产品 App | `0x08020000` | `0x001E0000` | `0x20000` |
| standalone Modbus 测试镜像 | `0x08000000` | `0x00200000` | `0x00000` |

证据来自产品 Keil 工程、两份 `system_stm32h5xx.c` 和
`shared/ota/ota_layout.h`。Boot 跳转时从 `OTA_APP_BASE` 读取 MSP/Reset，设置
`SCB->VTOR = 0x08020000`；因此当前 standalone HEX 不能放在产品 App 区后直接
由 Boot 启动。

## 签名结论

产品 OTA v2 描述符包含 SHA-256、64-byte ECDSA-P256 签名、镜像版本、load/entry
地址和防回滚最小版本。Boot 通过内置公钥验证待安装 slot。代码同时保留一个迁移兼容
分支：当镜像已是 `CONFIRMED`、未标记 signed 且 minimum version 为 0 时允许通过；
这不应当作为新版本发布流程。

也就是说：直接调试烧入一个地址正确的 App 可能通过基本向量检查，但正式 OTA 包仍需
走现有描述符、哈希、签名和版本策略。不能把 standalone 的通过等同于产品签名链通过。

## 安全回迁步骤

1. 将已验证的 `ld_modbus`、LDC adapter、BSP transport 和应用服务按产品 App
   的线程/资源所有权合入，而不是搬运 standalone `main`。
2. 使用产品 App 链接区 `0x08020000..0x081FFFFF`，恢复 `VECT_TAB_OFFSET=0x20000`。
3. 构建后检查向量表、Load Region、MSP 和 Reset_Handler 均位于产品契约范围。
4. 先重新烧录 Boot，再烧录产品 App；不要再执行 standalone 的整片擦除脚本。
5. 若走 OTA，使用现有制包/签名链生成 descriptor/manifest，验证 SHA-256、ECDSA 和
   version rollback policy，再做 trial boot、健康确认和回滚测试。
6. 最后复测 RS485-1 RTU、W800 TCP 与产品原有 USB/GUI/OTA 资源，确认没有 IRQ、DMA、
   UART 或线程优先级冲突。

本次没有替用户恢复 Boot/App，因为用户明确说测试完成后自行重新烧录；板上仍用于
standalone 验收。
