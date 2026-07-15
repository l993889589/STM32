"""One-shot MQTT 3.1.1 loopback broker used by the desktop self-test."""

from __future__ import annotations

import json
import socket
import sys
from pathlib import Path


def read_exact(connection: socket.socket, size: int) -> bytes:
    chunks: list[bytes] = []
    remaining = size
    while remaining:
        chunk = connection.recv(remaining)
        if not chunk:
            raise ConnectionError("client disconnected")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def read_packet(connection: socket.socket) -> tuple[int, bytes]:
    header = read_exact(connection, 1)[0]
    multiplier = 1
    remaining_length = 0
    for _ in range(4):
        encoded = read_exact(connection, 1)[0]
        remaining_length += (encoded & 0x7F) * multiplier
        if not encoded & 0x80:
            break
        multiplier *= 128
    else:
        raise ValueError("invalid MQTT remaining length")
    return header, read_exact(connection, remaining_length)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: mqtt_test_broker.py OUTPUT.json", file=sys.stderr)
        return 2

    output = Path(sys.argv[1])
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("127.0.0.1", 18883))
        listener.listen(1)
        listener.settimeout(10)
        connection, _ = listener.accept()
        with connection:
            connection.settimeout(10)
            connect_header, connect_body = read_packet(connection)
            if connect_header != 0x10 or b"MQTT" not in connect_body:
                raise ValueError("invalid MQTT CONNECT")
            connection.sendall(b"\x20\x02\x00\x00")

            publish_header, publish_body = read_packet(connection)
            if publish_header != 0x30 or len(publish_body) < 2:
                raise ValueError("invalid MQTT PUBLISH")
            topic_length = int.from_bytes(publish_body[:2], "big")
            topic = publish_body[2 : 2 + topic_length].decode("utf-8")
            payload = publish_body[2 + topic_length :].decode("utf-8")
            decoded = json.loads(payload)
            output.write_text(
                json.dumps({"topic": topic, "payload": decoded}, ensure_ascii=False, indent=2),
                encoding="utf-8",
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
