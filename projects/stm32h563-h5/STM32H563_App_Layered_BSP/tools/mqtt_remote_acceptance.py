#!/usr/bin/env python3
"""Exercise the board's bounded MQTT remote-operations command channel."""

from __future__ import annotations

import argparse
import json
import socket
import struct
import time
from pathlib import Path


def encode_remaining_length(length: int) -> bytes:
    """Encode one MQTT variable-byte remaining-length field."""
    encoded = bytearray()
    while True:
        digit = length % 128
        length //= 128
        if length:
            digit |= 0x80
        encoded.append(digit)
        if not length:
            return bytes(encoded)


def encode_text(text: str) -> bytes:
    """Encode one MQTT two-byte-length UTF-8 field."""
    data = text.encode("utf-8")
    return struct.pack(">H", len(data)) + data


def send_packet(sock: socket.socket, header: int, payload: bytes) -> None:
    """Send one complete MQTT control packet."""
    sock.sendall(bytes((header,)) + encode_remaining_length(len(payload)) + payload)


def read_exact(sock: socket.socket, length: int) -> bytes:
    """Read exactly length bytes or raise when the peer closes."""
    output = bytearray()
    while len(output) < length:
        chunk = sock.recv(length - len(output))
        if not chunk:
            raise ConnectionError("MQTT peer closed the socket")
        output.extend(chunk)
    return bytes(output)


def receive_packet(sock: socket.socket) -> tuple[int, bytes]:
    """Receive and decode one complete MQTT control packet."""
    header = read_exact(sock, 1)[0]
    multiplier = 1
    remaining = 0
    while True:
        digit = read_exact(sock, 1)[0]
        remaining += (digit & 0x7F) * multiplier
        if not digit & 0x80:
            break
        multiplier *= 128
        if multiplier > 128**3:
            raise ValueError("invalid MQTT remaining length")
    return header, read_exact(sock, remaining)


def connect(sock: socket.socket, client_id: str) -> None:
    """Open one clean MQTT 3.1.1 session and validate CONNACK."""
    variable = encode_text("MQTT") + bytes((4, 2)) + struct.pack(">H", 30)
    send_packet(sock, 0x10, variable + encode_text(client_id))
    header, payload = receive_packet(sock)
    if header != 0x20 or payload != b"\x00\x00":
        raise ConnectionError(f"MQTT CONNACK rejected: {header:#x} {payload.hex()}")


def subscribe(sock: socket.socket, topic: str, packet_id: int = 1) -> None:
    """Subscribe at QoS0 and validate the matching SUBACK."""
    send_packet(sock, 0x82, struct.pack(">H", packet_id) + encode_text(topic) + b"\x00")
    header, payload = receive_packet(sock)
    if header != 0x90 or payload[:2] != struct.pack(">H", packet_id):
        raise ConnectionError("MQTT SUBACK missing or mismatched")


def publish_json(sock: socket.socket, topic: str, command: dict[str, object]) -> None:
    """Publish one compact QoS0 JSON command."""
    payload = encode_text(topic) + json.dumps(command, separators=(",", ":")).encode("utf-8")
    send_packet(sock, 0x30, payload)


def decode_publish(header: int, payload: bytes) -> tuple[str, str] | None:
    """Decode one QoS0 PUBLISH packet into topic and UTF-8 payload."""
    if header >> 4 != 3 or len(payload) < 2:
        return None
    topic_length = struct.unpack(">H", payload[:2])[0]
    if 2 + topic_length > len(payload):
        return None
    topic = payload[2 : 2 + topic_length].decode("utf-8", errors="replace")
    text = payload[2 + topic_length :].decode("utf-8", errors="replace")
    return topic, text


def receive_message(sock: socket.socket, messages: list[dict[str, object]]) -> dict[str, object] | None:
    """Receive one packet and append a decoded PUBLISH message when present."""
    header, payload = receive_packet(sock)
    decoded = decode_publish(header, payload)
    if decoded is None:
        return None
    topic, text = decoded
    item: dict[str, object] = {"received_at": time.time(), "topic": topic, "payload": text}
    try:
        item["json"] = json.loads(text)
    except json.JSONDecodeError:
        pass
    messages.append(item)
    return item


def wait_for_ops(
    sock: socket.socket,
    messages: list[dict[str, object]],
    request: int,
    deadline: float,
    minimum_generation: int | None = None,
) -> dict[str, object]:
    """Wait for the requested board operations reply while retaining all traffic."""
    while time.monotonic() < deadline:
        try:
            item = receive_message(sock, messages)
        except socket.timeout:
            send_packet(sock, 0xC0, b"")
            continue
        if item is None or item["topic"] != "leduo/w800/ops":
            continue
        body = item.get("json")
        if not isinstance(body, dict) or body.get("request") != request:
            continue
        self_test = body.get("self_test", {})
        if minimum_generation is not None and (
            not isinstance(self_test, dict)
            or self_test.get("generation", -1) < minimum_generation
            or self_test.get("state") != 3
            or self_test.get("fail") != 0
        ):
            continue
        return item
    raise TimeoutError(f"MQTT operations reply {request} did not arrive before the deadline")


def run_acceptance(host: str, port: int, timeout: float, output: Path) -> int:
    """Send the stage-five command set and retain all board replies as NDJSON."""
    commands = [
        ({"cmd": "diagnostics"}, 1),
        ({"cmd": "blackbox_mark", "text": "mqtt_stage5_acceptance"}, 2),
        ({"cmd": "blackbox_status"}, 2),
        ({"cmd": "self_test_run"}, 1),
        ({"cmd": "power_auto", "enabled": 0}, 1),
    ]
    messages: list[dict[str, object]] = []
    deadline = time.monotonic() + timeout
    baseline_generation = 0
    failure = ""

    try:
        with socket.create_connection((host, port), timeout=5.0) as sock:
            sock.settimeout(1.0)
            connect(sock, f"leduo-acceptance-{int(time.time())}")
            subscribe(sock, "leduo/w800/#")
            for command, request in commands:
                publish_json(sock, "leduo/w800/cmd", command)
                minimum_generation = baseline_generation + 1 if command["cmd"] == "self_test_run" else None
                item = wait_for_ops(sock, messages, request, deadline, minimum_generation)
                body = item.get("json", {})
                if command["cmd"] == "diagnostics" and isinstance(body, dict):
                    self_test = body.get("self_test", {})
                    if isinstance(self_test, dict):
                        baseline_generation = int(self_test.get("generation", 0))
    except (ConnectionError, OSError, TimeoutError, ValueError) as exc:
        failure = str(exc)

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(json.dumps(item, ensure_ascii=False) for item in messages) + "\n", encoding="utf-8")
    ops = [item for item in messages if item["topic"] == "leduo/w800/ops"]
    completed = any(
        isinstance(item.get("json"), dict)
        and item["json"].get("self_test", {}).get("state") == 3
        and item["json"].get("self_test", {}).get("fail") == 0
        for item in ops
    )
    print(json.dumps({"messages": len(messages), "ops": len(ops), "self_test_completed": completed,
                      "failure": failure, "output": str(output)}))
    return 0 if len(ops) >= len(commands) and completed and not failure else 2


def main() -> int:
    """Parse command-line arguments and execute the remote acceptance flow."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.1.4")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--timeout", type=float, default=25.0)
    parser.add_argument("--output", type=Path, default=Path("tools/artifacts/mqtt_stage5.ndjson"))
    args = parser.parse_args()
    return run_acceptance(args.host, args.port, args.timeout, args.output)


if __name__ == "__main__":
    raise SystemExit(main())
