# W800 BLE 微信小程序配网

这个目录是 W800 `WIFIPROV` BLE 配网的微信原生小程序客户端。源码已保留，便于以后为无屏设备或手机端批量配置继续开发。

## 当前状态

- 2026-07-13 起暂停继续完善小程序交互，产品配置优先由屏幕工程承接。
- W800 BLE 配网能力继续保留为备选路径。
- USB CDC Shell 的 `wifi rescue` 继续作为救援入口。
- SoftAP 网页配网已经移除，不计划恢复。
- 不允许把真实 Wi-Fi SSID、密码写进源码或 Git。

## 导入与真机使用

1. 在微信开发者工具中选择“小程序 -> 导入项目”。
2. 项目目录选择本目录。
3. `project.config.json` 默认使用 `touristappid`；BLE 真机调试建议换成自己的小程序 AppID。
4. 在板卡 USB 串口执行：

   ```text
   wifi ble
   wifi status
   ```

   正常状态包含 `provisioning=ble active=1`。

5. 使用“预览”或“真机调试”在手机微信中打开，允许蓝牙、附近设备及 Android 可能要求的定位权限。
6. 点击“扫描 W800”，选择距离最近、RSSI 最强的设备，连接后输入 SSID 和密码，再点击“写入并连接”。
7. 小程序显示成功 IP/MAC 后，再用 `wifi status` 确认设备已经 `wifi ready`。

微信开发者工具模拟器不能替代真实 BLE 链路验证。

## 协议接口

- GATT Service：`0x1824`
- Write/Indicate Characteristic：`0x2ABC`
- 数据按 20 字节 BLE 写入上限分片。
- `utils/wifiprov.js` 实现 WIFIPROV 帧封装、CRC8、分片及结果解析。
- 状态码 `2`：密码错误；`3`：DHCP 获取 IP 失败；`4`：Wi-Fi 扫描失败。

当前实现使用 WinnerMicro WIFIPROV 文档允许的明文模式，没有实现可选 RSA/AES 协商，只适合有物理接触的本地调试。若重新启用为正式产品入口，必须补齐安全评审和真机测试。

## 常见问题

- 扫描不到：确认 `wifi status` 显示 BLE 配网处于 active，重新开关手机蓝牙并检查权限。
- 设备无名称：当前页面会列出附近 BLE 设备，优先选择板卡旁信号最强的设备。
- 提示没有 `0x1824` 服务：点到了其他 BLE 设备，返回后重新选择。
- 保存后未联网：检查小程序状态提示和串口 `wifi status`；必要时执行 `wifi rescue`。

## 屏幕工程后续接管约束

屏幕侧应调用 STM32 应用层的统一配置接口，不直接拼接 W800 AT 命令；界面需要明确显示连接中、成功、密码错误、扫描失败、DHCP 失败、超时和离线状态。密码不得回显或写入日志，并应保留清除凭据、重新配网及 USB 串口救援入口。

## 主机协议测试

在 `STM32H563RIV6_app` 根目录执行：

```powershell
node .\tests\host\test_w800_wifiprov.js
```
