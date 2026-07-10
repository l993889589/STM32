"""Create P-256 OTA keys and sign H563 firmware manifests.

Private keys are supplied by path and are never written into the repository.
The firmware signature is fixed-width, big-endian r||s for the Boot P-256 verifier.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

SIGNED_FLAG = 0x00000002
MANIFEST_SCHEMA = 1
MANIFEST_SIZE = 188


def parse_u32(value: int | str) -> int:
    """Parse JSON decimal or 0x-prefixed values as unsigned 32-bit integers."""
    parsed = int(value, 0) if isinstance(value, str) else int(value)
    if not 0 <= parsed <= 0xFFFFFFFF:
        raise ValueError(f"value outside uint32: {value}")
    return parsed


def canonical_digest(manifest: dict, image_sha256: bytes, flags: int) -> bytes:
    """Build the exact canonical digest implemented by ota_security_policy.c."""
    message = struct.pack(
        "<4s7I32s",
        b"H5FW",
        MANIFEST_SCHEMA,
        parse_u32(manifest["image_version"]),
        parse_u32(manifest["image_size"]),
        parse_u32(manifest["image_crc32"]),
        flags,
        parse_u32(manifest["load_address"]),
        parse_u32(manifest["entry_address"]),
        image_sha256,
    )
    return hashlib.sha256(message).digest()


def load_private_key(path: Path) -> ec.EllipticCurvePrivateKey:
    key = serialization.load_pem_private_key(path.read_bytes(), password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(
        key.curve, ec.SECP256R1
    ):
        raise ValueError("OTA signing key must be ECDSA P-256")
    return key


def keygen(args: argparse.Namespace) -> None:
    """Generate a release key once and print its trust-anchor coordinates."""
    path = Path(args.private_key).expanduser().resolve()
    if path.exists() and not args.force:
        raise FileExistsError(f"refusing to overwrite existing key: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    key = ec.generate_private_key(ec.SECP256R1())
    path.write_bytes(
        key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        )
    )
    public = key.public_key().public_numbers()
    print(f"private_key={path}")
    print(f"public_x={public.x:064X}")
    print(f"public_y={public.y:064X}")


def update_v1_manifest_binary(path: Path, image_sha: bytes, signature: bytes, flags: int) -> int:
    """Keep the migration manifest consistent with the signed v2 descriptor."""
    data = bytearray(path.read_bytes())
    if len(data) != MANIFEST_SIZE:
        raise ValueError(f"unexpected manifest size {len(data)}: {path}")
    struct.pack_into("<I", data, 20, flags)
    data[56:88] = image_sha
    data[88:120] = image_sha
    data[120:184] = signature
    struct.pack_into("<I", data, 184, 0)
    crc = zlib.crc32(data) & 0xFFFFFFFF
    struct.pack_into("<I", data, 184, crc)
    path.write_bytes(data)
    return crc


def sign(args: argparse.Namespace) -> None:
    """Hash an image, sign canonical metadata and update JSON/binary manifests."""
    image_path = Path(args.image).resolve()
    manifest_path = Path(args.manifest).resolve()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    image = image_path.read_bytes()
    if len(image) != parse_u32(manifest["image_size"]):
        raise ValueError("image size differs from manifest")

    image_sha = hashlib.sha256(image).digest()
    if image_sha.hex().upper() != str(manifest["image_sha256"]).upper():
        raise ValueError("image SHA-256 differs from manifest")

    flags = parse_u32(manifest.get("image_flags", 0)) | SIGNED_FLAG
    digest = canonical_digest(manifest, image_sha, flags)
    key = load_private_key(Path(args.private_key).expanduser().resolve())
    der = key.sign(digest, ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    r, s = utils.decode_dss_signature(der)
    signature = r.to_bytes(32, "big") + s.to_bytes(32, "big")

    manifest_bin = Path(args.manifest_bin).resolve() if args.manifest_bin else Path(
        manifest["manifest_bin"]
    ).resolve()
    manifest_crc = update_v1_manifest_binary(
        manifest_bin, image_sha, signature, flags
    )
    public = key.public_key().public_numbers()
    key_id = hashlib.sha256(
        public.x.to_bytes(32, "big") + public.y.to_bytes(32, "big")
    ).hexdigest()[:16].upper()

    manifest["image_flags"] = f"0x{flags:08X}"
    manifest["image_sha256"] = image_sha.hex().upper()
    manifest["signature"] = signature.hex().upper()
    manifest["signature_algorithm"] = "ECDSA_P256_SHA256_RAW"
    manifest["signing_key_id"] = key_id
    manifest["manifest_crc32"] = f"0x{manifest_crc:08X}"
    manifest_path.write_text(
        json.dumps(manifest, indent=4, ensure_ascii=True) + "\n", encoding="utf-8"
    )
    print(f"signed_manifest={manifest_path}")
    print(f"signing_key_id={key_id}")
    print(f"canonical_digest={digest.hex().upper()}")


def verify(args: argparse.Namespace) -> None:
    """Verify a release package using only the public trust-anchor coordinates."""
    image_path = Path(args.image).resolve()
    manifest_path = Path(args.manifest).resolve()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    image = image_path.read_bytes()
    image_sha = hashlib.sha256(image).digest()
    if len(image) != parse_u32(manifest["image_size"]):
        raise ValueError("image size differs from manifest")
    if (zlib.crc32(image) & 0xFFFFFFFF) != parse_u32(manifest["image_crc32"]):
        raise ValueError("image CRC32 differs from manifest")
    if image_sha.hex().upper() != str(manifest["image_sha256"]).upper():
        raise ValueError("image SHA-256 differs from manifest")

    flags = parse_u32(manifest["image_flags"])
    signature = bytes.fromhex(manifest["signature"])
    if len(signature) != 64 or (flags & SIGNED_FLAG) == 0:
        raise ValueError("manifest is not a signed raw P-256 package")
    digest = canonical_digest(manifest, image_sha, flags)
    r = int.from_bytes(signature[:32], "big")
    s = int.from_bytes(signature[32:], "big")
    der = utils.encode_dss_signature(r, s)
    public = ec.EllipticCurvePublicNumbers(
        int(args.public_x, 16), int(args.public_y, 16), ec.SECP256R1()
    ).public_key()
    public.verify(der, digest, ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    print(f"verified_manifest={manifest_path}")
    print(f"canonical_digest={digest.hex().upper()}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    key_parser = subparsers.add_parser("keygen")
    key_parser.add_argument("--private-key", required=True)
    key_parser.add_argument("--force", action="store_true")
    key_parser.set_defaults(handler=keygen)

    sign_parser = subparsers.add_parser("sign")
    sign_parser.add_argument("--private-key", required=True)
    sign_parser.add_argument("--image", required=True)
    sign_parser.add_argument("--manifest", required=True)
    sign_parser.add_argument("--manifest-bin")
    sign_parser.set_defaults(handler=sign)

    verify_parser = subparsers.add_parser("verify")
    verify_parser.add_argument("--public-x", required=True)
    verify_parser.add_argument("--public-y", required=True)
    verify_parser.add_argument("--image", required=True)
    verify_parser.add_argument("--manifest", required=True)
    verify_parser.set_defaults(handler=verify)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    args.handler(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
