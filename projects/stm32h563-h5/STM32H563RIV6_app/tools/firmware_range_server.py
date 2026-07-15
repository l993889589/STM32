#!/usr/bin/env python3
"""Serve one signed firmware image with bounded HTTP byte-range support."""

from __future__ import annotations

import argparse
import re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class FirmwareRangeHandler(BaseHTTPRequestHandler):
    """Return the configured image as a full response or one byte range."""

    image_path: Path

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        """Handle GET /firmware/app.bin with optional Range metadata."""
        if self.path != "/firmware/app.bin":
            self.send_error(404)
            return

        size = self.image_path.stat().st_size
        start = 0
        end = size - 1
        status = 200
        range_header = self.headers.get("Range")
        if range_header:
            match = re.fullmatch(r"bytes=(\d+)-(\d*)", range_header.strip())
            if match is None:
                self.send_error(416)
                return
            start = int(match.group(1))
            end = int(match.group(2)) if match.group(2) else end
            end = min(end, size - 1)
            if start < 0 or start > end or start >= size:
                self.send_error(416)
                return
            status = 206

        length = end - start + 1
        self.send_response(status)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(length))
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Cache-Control", "no-store")
        if status == 206:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.end_headers()

        with self.image_path.open("rb") as image:
            image.seek(start)
            remaining = length
            while remaining:
                chunk = image.read(min(4096, remaining))
                if not chunk:
                    break
                self.wfile.write(chunk)
                remaining -= len(chunk)

    def log_message(self, format: str, *args: object) -> None:
        """Emit compact request evidence without the default stderr prefix."""
        print(f"{self.client_address[0]} {format % args}", flush=True)


def main() -> int:
    """Validate arguments and run the threaded local range server."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8089)
    args = parser.parse_args()
    image = args.image.resolve()
    if not image.is_file():
        parser.error(f"firmware image does not exist: {image}")
    FirmwareRangeHandler.image_path = image
    server = ThreadingHTTPServer((args.host, args.port), FirmwareRangeHandler)
    print(f"serving {image} on {args.host}:{args.port}", flush=True)
    server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
