# W800 MQTT Protocol

LeduO Debug Assistant includes a local MQTT broker for W800 bring-up.

## Broker

- Host: the PC LAN IP shown on the `W800联网` page
- Port: `1883` by default
- Auth: none for the first bring-up version
- Client ID: recommended `w800-<chip-id>` or `w800-<mac>`

The assistant also shows a copyable firmware config block:

```text
broker=mqtt://<PC_IP>:1883
clientId=w800-<chip-id>
topicUp=leduo/w800/up
topicStatus=leduo/w800/status
topicLog=leduo/w800/log
topicDown=leduo/pc/down
auth=none
```

## Topics

| Direction | Topic | Payload |
| --- | --- | --- |
| W800 -> PC | `leduo/w800/up` | General telemetry or test payload |
| W800 -> PC | `leduo/w800/status` | Device status JSON |
| W800 -> PC | `leduo/w800/log` | Text or JSON log lines |
| PC -> W800 | `leduo/pc/down` | Command JSON |

## Status JSON

The assistant parses JSON payloads published to `leduo/w800/up` or any topic under
`leduo/w800/` and updates the device card when these fields are present.

```json
{
  "deviceId": "w800-001",
  "online": true,
  "ip": "192.168.1.88",
  "rssi": -47,
  "fw": "0.1.0",
  "uptime": 123456,
  "mode": "mqtt"
}
```

Field aliases:

- `id` may be used instead of `deviceId`
- `version` may be used instead of `fw`

## PC Command JSON

The default downlink topic is `leduo/pc/down`.

```json
{
  "cmd": "ping"
}
```

Recommended first commands:

- `{"cmd":"ping"}`: W800 replies on `leduo/w800/up`
- `{"cmd":"status"}`: W800 replies on `leduo/w800/status`
- `{"cmd":"log","enable":true}`: W800 starts publishing logs to `leduo/w800/log`
- `{"cmd":"log","enable":false}`: W800 stops publishing logs
- `{"cmd":"reboot"}`: W800 reboots after acknowledging or logging the command

The `W800联网` page provides presets for these commands and publishes them to
`leduo/pc/down`.
