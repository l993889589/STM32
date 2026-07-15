#!/usr/bin/env python3
"""Build, verify, upload, and factory-provision signed ART-Pi H750 images."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import time
import urllib.request
import zlib
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils


MANIFEST_MAGIC = 0x4F54414D
MANIFEST_SCHEMA = 1
SIGNED_FLAG = 0x00000002
LOAD_ADDRESS = 0x90000000
STAGE_IMAGE_ADDRESS = 0x00401000
MANIFEST_FORMAT = "<14I32s32s64sI"
MANIFEST_SIZE = struct.calcsize(MANIFEST_FORMAT)

CONTROL_MAGIC = 0x43423748
CONTROL_SCHEMA = 2
CONTROL_ADDRESS = 0x90600000
SLOT_A_ADDRESS = 0x90200000
SLOT_CONFIRMED = 3
SLOT_NONE = 0xFFFFFFFF
DESCRIPTOR_FORMAT = "<7I32s64s"

PUBLIC_X = bytes.fromhex(
    "2ECF50B0BC6E2FB349FB632EF912637B47506B39A77708EE5C136B31718FF6B3"
)
PUBLIC_Y = bytes.fromhex(
    "9CCDA807259DD724B7ADFA1547A335E5CD21B05248032F6D0683E877DE8A3926"
)


def signature_message(fields: dict[str, int | bytes]) -> bytes:
    return b"".join(
        [
            b"H7FW",
            struct.pack(
                "<7I",
                MANIFEST_SCHEMA,
                int(fields["image_version"]),
                int(fields["image_size"]),
                int(fields["image_crc32"]),
                int(fields["image_flags"]),
                int(fields["load_address"]),
                int(fields["entry_address"]),
            ),
            bytes(fields["image_sha256"]),
        ]
    )


def public_key() -> ec.EllipticCurvePublicKey:
    numbers = ec.EllipticCurvePublicNumbers(
        int.from_bytes(PUBLIC_X, "big"),
        int.from_bytes(PUBLIC_Y, "big"),
        ec.SECP256R1(),
    )
    return numbers.public_key()


def sign_raw(private_key_path: Path, message: bytes) -> bytes:
    key = serialization.load_pem_private_key(
        private_key_path.read_bytes(), password=None
    )
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(
        key.curve, ec.SECP256R1
    ):
        raise ValueError("gateway OTA signing key must be ECDSA P-256")
    der = key.sign(message, ec.ECDSA(hashes.SHA256()))
    r, s = utils.decode_dss_signature(der)
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def verify_raw(message: bytes, signature: bytes) -> None:
    r = int.from_bytes(signature[:32], "big")
    s = int.from_bytes(signature[32:], "big")
    public_key().verify(
        utils.encode_dss_signature(r, s),
        message,
        ec.ECDSA(hashes.SHA256()),
    )


def make_manifest(image: bytes, version: int, key_path: Path) -> bytes:
    if not image or len(image) > 0x1FF000:
        raise ValueError("image must fit the 0x1FF000-byte gateway staging area")
    stack_pointer, entry_address = struct.unpack_from("<II", image, 0)
    if not (0x20000000 <= stack_pointer < 0x40000000):
        raise ValueError(f"unexpected initial stack pointer 0x{stack_pointer:08X}")
    if not (LOAD_ADDRESS <= (entry_address & ~1) < LOAD_ADDRESS + 0x200000):
        raise ValueError(f"unexpected reset vector 0x{entry_address:08X}")

    image_sha = hashlib.sha256(image).digest()
    fields: dict[str, int | bytes] = {
        "image_version": version,
        "image_size": len(image),
        "image_crc32": zlib.crc32(image) & 0xFFFFFFFF,
        "image_flags": SIGNED_FLAG,
        "load_address": LOAD_ADDRESS,
        "entry_address": entry_address,
        "image_sha256": image_sha,
    }
    signature = sign_raw(key_path, signature_message(fields))
    values = (
        MANIFEST_MAGIC,
        MANIFEST_SCHEMA,
        1,
        len(image),
        version,
        SIGNED_FLAG,
        fields["image_crc32"],
        STAGE_IMAGE_ADDRESS,
        len(image),
        0,
        0,
        0,
        LOAD_ADDRESS,
        entry_address,
        image_sha,
        image_sha,
        signature,
        0,
    )
    manifest = bytearray(struct.pack(MANIFEST_FORMAT, *values))
    struct.pack_into("<I", manifest, MANIFEST_SIZE - 4, zlib.crc32(manifest))
    return bytes(manifest)


def parse_manifest(data: bytes, image: bytes | None = None) -> dict[str, int | bytes]:
    if len(data) != MANIFEST_SIZE:
        raise ValueError(f"manifest must be {MANIFEST_SIZE} bytes")
    unpacked = struct.unpack(MANIFEST_FORMAT, data)
    names = [
        "magic",
        "version",
        "boot_state",
        "image_size",
        "image_version",
        "image_flags",
        "image_crc32",
        "package_address",
        "package_size",
        "rollback_address",
        "rollback_size",
        "rollback_crc32",
        "load_address",
        "entry_address",
        "image_sha256",
        "package_sha256",
        "signature",
        "manifest_crc32",
    ]
    fields = dict(zip(names, unpacked, strict=True))
    check = bytearray(data)
    struct.pack_into("<I", check, MANIFEST_SIZE - 4, 0)
    if fields["magic"] != MANIFEST_MAGIC or fields["version"] != MANIFEST_SCHEMA:
        raise ValueError("manifest magic/schema mismatch")
    if zlib.crc32(check) & 0xFFFFFFFF != fields["manifest_crc32"]:
        raise ValueError("manifest CRC mismatch")
    verify_raw(signature_message(fields), bytes(fields["signature"]))
    if image is not None:
        if len(image) != fields["image_size"]:
            raise ValueError("image size mismatch")
        if zlib.crc32(image) & 0xFFFFFFFF != fields["image_crc32"]:
            raise ValueError("image CRC mismatch")
        if hashlib.sha256(image).digest() != fields["image_sha256"]:
            raise ValueError("image SHA-256 mismatch")
    return fields


def write_hex(path: Path, segments: list[tuple[int, bytes]]) -> None:
    lines: list[str] = []
    current_upper = -1
    for address, data in sorted(segments):
        offset = 0
        while offset < len(data):
            absolute = address + offset
            upper = absolute >> 16
            if upper != current_upper:
                payload = struct.pack(">H", upper & 0xFFFF)
                lines.append(hex_record(0, 4, payload))
                current_upper = upper
            part = data[offset : offset + min(16, 0x10000 - (absolute & 0xFFFF))]
            lines.append(hex_record(absolute & 0xFFFF, 0, part))
            offset += len(part)
    lines.append(":00000001FF")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def hex_record(address: int, record_type: int, payload: bytes) -> str:
    header = bytes([len(payload)]) + struct.pack(">H", address) + bytes([record_type])
    checksum = (-sum(header + payload)) & 0xFF
    return ":" + (header + payload + bytes([checksum])).hex().upper()


def make_control_record(image: bytes, fields: dict[str, int | bytes]) -> bytes:
    descriptor = struct.pack(
        DESCRIPTOR_FORMAT,
        SLOT_CONFIRMED,
        fields["image_version"],
        fields["image_size"],
        fields["image_crc32"],
        fields["image_flags"],
        fields["load_address"],
        fields["entry_address"],
        fields["image_sha256"],
        fields["signature"],
    )
    empty_descriptor = bytes(struct.calcsize(DESCRIPTOR_FORMAT))
    prefix = struct.pack(
        "<8I",
        CONTROL_MAGIC,
        CONTROL_SCHEMA,
        1,
        0,
        SLOT_NONE,
        fields["image_version"],
        0,
        0,
    ) + descriptor + empty_descriptor
    return prefix + struct.pack("<II", zlib.crc32(prefix) & 0xFFFFFFFF, 0)


def http_post(url: str, data: bytes) -> dict:
    request = urllib.request.Request(
        url,
        data=data,
        method="POST",
        headers={"Content-Type": "application/octet-stream"},
    )
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.loads(response.read().decode("utf-8"))


def http_get(url: str) -> dict:
    request = urllib.request.Request(url, method="GET")
    with urllib.request.urlopen(request, timeout=10) as response:
        return json.loads(response.read().decode("utf-8"))


def command_package(args: argparse.Namespace) -> None:
    image_path = Path(args.image).resolve()
    image = image_path.read_bytes()
    manifest = make_manifest(image, args.version, Path(args.private_key).expanduser())
    fields = parse_manifest(manifest, image)
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    stem = f"art_pi_h750_gateway_{args.version}"
    image_output = output / f"{stem}.bin"
    manifest_output = output / f"{stem}.manifest.bin"
    json_output = output / f"{stem}.manifest.json"
    image_output.write_bytes(image)
    manifest_output.write_bytes(manifest)
    json_output.write_text(
        json.dumps(
            {
                "target": "ART-Pi STM32H750",
                "signature_domain": "H7FW",
                "version": fields["image_version"],
                "size": fields["image_size"],
                "crc32": f"{fields['image_crc32']:08X}",
                "sha256": bytes(fields["image_sha256"]).hex().upper(),
                "entry_address": f"0x{fields['entry_address']:08X}",
                "signature": bytes(fields["signature"]).hex().upper(),
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    print(f"image={image_output}")
    print(f"manifest={manifest_output}")
    print(f"metadata={json_output}")


def command_factory(args: argparse.Namespace) -> None:
    image = Path(args.image).read_bytes()
    manifest = Path(args.manifest).read_bytes()
    fields = parse_manifest(manifest, image)
    record = make_control_record(image, fields)
    if len(record) != 288:
        raise RuntimeError(f"unexpected control record size {len(record)}")
    output = Path(args.output).resolve()
    control_output = output.with_suffix(".control.bin")
    control_output.write_bytes(record)
    write_hex(
        output,
        [
            (LOAD_ADDRESS, image),
            (SLOT_A_ADDRESS, image),
            (CONTROL_ADDRESS, record),
        ],
    )
    print(f"factory_hex={output}")
    print(f"control_record={control_output}")
    print(f"control_record_size={len(record)}")


def command_verify(args: argparse.Namespace) -> None:
    image = Path(args.image).read_bytes()
    fields = parse_manifest(Path(args.manifest).read_bytes(), image)
    print(
        f"verified version={fields['image_version']} size={fields['image_size']} "
        f"crc32={fields['image_crc32']:08X} sha256={bytes(fields['image_sha256']).hex().upper()}"
    )


def command_upload(args: argparse.Namespace) -> None:
    host = args.host.rstrip("/")
    image = Path(args.image).read_bytes()
    manifest = Path(args.manifest).read_bytes()
    fields = parse_manifest(manifest, image)
    offset = 0
    ready = False

    if args.resume:
        status = http_get(f"{host}/api/ota/gateway/status")
        package_matches = (
            int(status.get("version", 0)) == int(fields["image_version"])
            and int(status.get("expected", 0)) == len(image)
            and int(status.get("crc32", 0)) == int(fields["image_crc32"])
        )
        if package_matches and status.get("state") == "receiving":
            offset = int(status.get("received", 0))
            if offset < 0 or offset > len(image):
                raise RuntimeError(f"invalid device resume offset {offset}")
            print(f"resuming={offset}/{len(image)}", flush=True)
        elif package_matches and status.get("state") == "ready":
            offset = len(image)
            ready = True
            print("device already holds a verified package", flush=True)

    if offset == 0 and not ready:
        print(http_post(f"{host}/api/ota/gateway/manifest", manifest), flush=True)
    started = time.monotonic()
    while offset < len(image):
        part = image[offset : offset + args.chunk_size]
        response = http_post(
            f"{host}/api/ota/gateway/chunk?offset={offset}", part
        )
        offset = int(response["next_offset"])
        if offset == len(image) or offset % (64 * 1024) < args.chunk_size:
            print(f"uploaded={offset}/{len(image)}", flush=True)
    if not ready:
        print(http_post(f"{host}/api/ota/gateway/finish", b""), flush=True)
    elapsed = time.monotonic() - started
    print(f"upload_seconds={elapsed:.3f}", flush=True)
    if args.start:
        print(http_post(f"{host}/api/ota/gateway/start", b""), flush=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    package = subparsers.add_parser("package")
    package.add_argument("--image", required=True)
    package.add_argument("--version", required=True, type=int)
    package.add_argument("--private-key", required=True)
    package.add_argument("--output", required=True)
    package.set_defaults(func=command_package)

    factory = subparsers.add_parser("factory")
    factory.add_argument("--image", required=True)
    factory.add_argument("--manifest", required=True)
    factory.add_argument("--output", required=True)
    factory.set_defaults(func=command_factory)

    verify = subparsers.add_parser("verify")
    verify.add_argument("--image", required=True)
    verify.add_argument("--manifest", required=True)
    verify.set_defaults(func=command_verify)

    upload = subparsers.add_parser("upload")
    upload.add_argument("--host", default="http://192.168.1.20")
    upload.add_argument("--image", required=True)
    upload.add_argument("--manifest", required=True)
    upload.add_argument("--chunk-size", type=int, default=7168)
    upload.add_argument("--resume", action="store_true")
    upload.add_argument("--start", action="store_true")
    upload.set_defaults(func=command_upload)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
