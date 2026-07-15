# W800 BLE provisioning and USB rescue

## Current decision (2026-07-13)

The Mini Program source is retained in Git, but its product integration is
paused. The display project will take priority for Wi-Fi credential entry and
connection feedback. BLE remains an optional future path, USB CDC remains the
rescue path, and SoftAP Web provisioning stays removed. See
`companion/w800_ble_miniprogram/README.md` for usage and handoff constraints.

## Product paths

- Primary: W800 built-in BleWiFi service (`AT+BTEN=255,6`, then `AT+ONESHOT=4`).
- Rescue: USB CDC Shell command `wifi rescue`.
- Removed: SoftAP WebServer provisioning (`AT+ONESHOT=3`) is not used.

When the saved station profile is unavailable for 15 seconds, the STM32 starts
BLE provisioning. W800 advertises GATT service `0x1824` and exposes a single
write/indicate characteristic `0x2ABC`. A successful configuration is detected
through `AT+LKSTT`, after which the MQTT state machine resumes.

## USB CDC rescue

Run:

```text
leduo> wifi rescue
SSID: factory-network
Password (hidden):
credentials queued; use 'wifi status' for the result
```

The password is not echoed, is not added to Shell history, and AT logging is
suppressed while the key command is transmitted. The single-use credential
mailbox and command buffers are wiped after consumption. The rescue path accepts
a printable ASCII SSID of 1..32 bytes and a printable WPA/WPA2 password of 8..63
bytes. W800 persists the profile with `AT+PMTF`; STM32 never stores it in
internal flash.

`wifi status` reports `usb_rescue=pending|applying|saved|connected|failed`.

## WeChat Mini Program

The companion project is under `companion/w800_ble_miniprogram`. Import that
directory into WeChat DevTools, build it, enable Bluetooth, scan, connect to the
W800, enter an SSID/password, and select **Configure**.

The initial Mini Program uses the plaintext mode explicitly supported by the
WinnerMicro WIFIPROV specification. It provides CRC8, fragmentation, sequence
checks, and result indications, but it does not perform the optional RSA/AES
key negotiation. Use it only during local commissioning with physical access.
For deployment in an untrusted environment, add RSA/AES negotiation or wrap the
official Android/iOS BleWiFi SDK before treating the mobile channel as secure.

## References

- `WM_W800_BleWiFi蓝牙配网Android SDK_V1.0.pdf`
- `WM_W800_BleWiFi蓝牙配网Android App使用指导_V1.1.pdf`
- `WM_W800_SDK_AT指令用户手册_V1.1.pdf`
- WinnerMicro WIFIPROV protocol and `wifi_prov` example
