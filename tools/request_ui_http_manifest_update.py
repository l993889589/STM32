#!/usr/bin/env python3
"""Request a W800 UI asset update through MQTT using HTTP Range data.

Purpose:
  Publish the small MQTT control command that asks the board to fetch
  /ui/manifest.json from the PC HTTP server, then download /ui/ui_assets.bin
  with HTTP Range and commit the inactive external-Flash slot.

Usage:
  python tools/request_ui_http_manifest_update.py --http-host 192.168.1.4
  python tools/request_ui_http_manifest_update.py --http-host 192.168.1.4 --no-wait

Constraints:
  This script has no third-party dependency. It implements the small MQTT 3.1.1
  subset needed for QoS0 publish and status subscribe so it can run on a clean
  Windows host. MQTT remains command/status only; image bytes are never sent by
  this script.
"""

from __future__ import annotations

import argparse
import binascii
import json
import socket
import struct
import sys
import time
from pathlib import Path


TOPIC_CMD = "leduo/w800/cmd"
TOPIC_STATUS = "leduo/w800/status"
MQTT_PINGREQ = b"\xC0\x00"
MQTT_PINGRESP_HEADER = 0xD0
MQTT_WAIT_PING_INTERVAL_S = 20.0


def encode_remaining_length(value: int) -> bytes:
    out = bytearray()
    while True:
        encoded = value % 128
        value //= 128
        if value:
            encoded |= 0x80
        out.append(encoded)
        if not value:
            return bytes(out)


def mqtt_utf8(text: str) -> bytes:
    raw = text.encode("utf-8")
    return struct.pack(">H", len(raw)) + raw


def mqtt_connect(client_id: str) -> bytes:
    variable = mqtt_utf8("MQTT") + bytes([4, 2]) + struct.pack(">H", 60)
    payload = mqtt_utf8(client_id)
    remaining = variable + payload
    return bytes([0x10]) + encode_remaining_length(len(remaining)) + remaining


def mqtt_publish(topic: str, payload: bytes) -> bytes:
    body = mqtt_utf8(topic) + payload
    return bytes([0x30]) + encode_remaining_length(len(body)) + body


def mqtt_subscribe(topic: str, packet_id: int = 1) -> bytes:
    variable = struct.pack(">H", packet_id)
    payload = mqtt_utf8(topic) + b"\x00"
    remaining = variable + payload
    return bytes([0x82]) + encode_remaining_length(len(remaining)) + remaining


def read_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise RuntimeError("MQTT broker closed the connection")
        data.extend(chunk)
    return bytes(data)


def read_packet(sock: socket.socket) -> tuple[int, bytes]:
    header = read_exact(sock, 1)[0]
    multiplier = 1
    remaining = 0
    while True:
        encoded = read_exact(sock, 1)[0]
        remaining += (encoded & 0x7F) * multiplier
        if (encoded & 0x80) == 0:
            break
        multiplier *= 128
        if multiplier > 128 * 128 * 128:
            raise RuntimeError("Bad MQTT remaining length")
    return header, read_exact(sock, remaining)


def read_connack(sock: socket.socket) -> None:
    fixed = read_exact(sock, 4)
    if fixed != b"\x20\x02\x00\x00":
        raise RuntimeError(f"Unexpected CONNACK: {fixed.hex()}")


def read_suback(sock: socket.socket, packet_id: int = 1) -> None:
    header, body = read_packet(sock)
    if header != 0x90 or len(body) < 3:
        raise RuntimeError(f"Unexpected SUBACK: header=0x{header:02x} body={body.hex()}")
    got_id = struct.unpack_from(">H", body, 0)[0]
    if got_id != packet_id or body[2] == 0x80:
        raise RuntimeError(f"Subscribe failed: packet_id={got_id} rc=0x{body[2]:02x}")


def parse_publish(body: bytes) -> tuple[str, bytes] | None:
    if len(body) < 2:
        return None
    topic_len = struct.unpack_from(">H", body, 0)[0]
    if 2 + topic_len > len(body):
        return None
    topic = body[2 : 2 + topic_len].decode("utf-8", errors="replace")
    return topic, body[2 + topic_len :]


def parse_asset_header(path: Path) -> tuple[int, int, int]:
    data = path.read_bytes()
    if len(data) < 56:
        raise RuntimeError("Asset package is too small")
    magic, schema, header_size, entry_size, image_count, total_size, version = struct.unpack_from("<7I", data, 0)
    if magic != 0x50414955 or schema != 1 or header_size != 256 or entry_size != 32:
        raise RuntimeError("Asset package is not UIAP schema v1")
    if image_count == 0 or total_size == 0 or total_size > len(data):
        raise RuntimeError(f"Invalid asset total size: header={total_size}, file={len(data)}")
    crc32 = binascii.crc32(data[:total_size]) & 0xFFFFFFFF
    return total_size, version, crc32


def wait_update_result(sock: socket.socket, version: int, timeout_s: float) -> dict:
    deadline = time.monotonic() + timeout_s
    last_ping = time.monotonic()
    last_status: dict = {}
    while time.monotonic() < deadline:
        now = time.monotonic()
        if now - last_ping >= MQTT_WAIT_PING_INTERVAL_S:
            sock.sendall(MQTT_PINGREQ)
            last_ping = now

        sock.settimeout(max(0.2, min(1.0, deadline - time.monotonic())))
        try:
            header, body = read_packet(sock)
        except socket.timeout:
            continue

        if header == MQTT_PINGRESP_HEADER:
            continue
        if (header & 0xF0) != 0x30:
            continue
        parsed = parse_publish(body)
        if parsed is None:
            continue
        topic, payload = parsed
        if topic != TOPIC_STATUS:
            continue
        try:
            status = json.loads(payload.decode("utf-8", errors="replace"))
        except json.JSONDecodeError:
            continue

        last_status = status
        http = status.get("http") if isinstance(status.get("http"), dict) else {}
        asset = status.get("asset") if isinstance(status.get("asset"), dict) else {}
        http_error = str(http.get("error", "") or "")
        if http_error:
            raise RuntimeError(f"Board HTTP update failed: {http_error}; status={json.dumps(status, ensure_ascii=False)}")
        if int(asset.get("available", 0) or 0) == 1 and int(asset.get("version", 0) or 0) == version:
            return status

    raise TimeoutError(f"Timed out waiting for asset version {version}; last_status={json.dumps(last_status, ensure_ascii=False)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--broker-host", default="127.0.0.1")
    parser.add_argument("--broker-port", type=int, default=1883)
    parser.add_argument("--http-host", required=True)
    parser.add_argument("--http-port", type=int, default=8088)
    parser.add_argument("--manifest", default="/ui/manifest.json")
    parser.add_argument("--topic", default=TOPIC_CMD)
    parser.add_argument("--asset", default=r"D:\Embedded\H5\build\ui_assets\ui_assets.bin")
    parser.add_argument("--timeout", type=float, default=900.0)
    parser.add_argument("--no-wait", action="store_true")
    args = parser.parse_args()

    asset_path = Path(args.asset).resolve()
    total_size, version, crc32 = parse_asset_header(asset_path)
    message = {
        "cmd": "ui_http_manifest_update",
        "host": args.http_host,
        "port": args.http_port,
        "manifest": args.manifest,
    }

    print(f"asset={asset_path}")
    print(f"version={version} size={total_size} crc32=0x{crc32:08X}")
    print(f"mqtt={args.broker_host}:{args.broker_port} topic={args.topic}")
    print(f"http=http://{args.http_host}:{args.http_port}{args.manifest}")
    print(f"payload={json.dumps(message, separators=(',', ':'))}")

    sock = socket.create_connection((args.broker_host, args.broker_port), timeout=5)
    try:
        sock.sendall(mqtt_connect(f"leduo-http-update-{int(time.time())}"))
        read_connack(sock)

        if not args.no_wait:
            sock.sendall(mqtt_subscribe(TOPIC_STATUS))
            read_suback(sock)

        sock.sendall(mqtt_publish(args.topic, json.dumps(message, separators=(",", ":")).encode("utf-8")))

        if args.no_wait:
            return 0

        status = wait_update_result(sock, version, args.timeout)
        print("update=done")
        print(json.dumps(status, ensure_ascii=False, separators=(",", ":")))
        return 0
    finally:
        sock.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 - command line tool reports concise failure.
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
