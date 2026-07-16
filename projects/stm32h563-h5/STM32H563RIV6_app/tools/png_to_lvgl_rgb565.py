"""Convert a 480x320 PNG into a deterministic LVGL v9 RGB565 C source.

The output uses native little-endian RGB565 bytes, matching the STM32H5 LVGL
display port.  This script is intended for reproducible boot-splash updates;
it does not edit project metadata.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


def rgb565_bytes(image: Image.Image) -> bytes:
    data = bytearray()
    for red, green, blue in image.convert("RGB").getdata():
        value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        data.append(value & 0xFF)
        data.append((value >> 8) & 0xFF)
    return bytes(data)


def emit_c(data: bytes, symbol: str) -> str:
    lines = []
    for offset in range(0, len(data), 20):
        chunk = data[offset : offset + 20]
        lines.append("    " + ", ".join(f"0x{value:02X}" for value in chunk) + ",")

    body = "\n".join(lines)
    return f'''#include "lvgl.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif
#ifndef LV_ATTRIBUTE_IMAGE_BOOT_EYE
#define LV_ATTRIBUTE_IMAGE_BOOT_EYE
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMAGE_BOOT_EYE uint8_t {symbol}_map[] = {{
{body}
}};

const lv_image_dsc_t {symbol} = {{
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.w = 480,
    .header.h = 320,
    .header.stride = 480 * 2,
    .data_size = sizeof({symbol}_map),
    .data = {symbol}_map,
}};
'''


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--symbol", default="boot_eye_img")
    args = parser.parse_args()

    image = Image.open(args.input).convert("RGB")
    if image.size != (480, 320):
        image = image.resize((480, 320), Image.Resampling.LANCZOS)

    args.output.write_text(emit_c(rgb565_bytes(image), args.symbol), encoding="utf-8")


if __name__ == "__main__":
    main()
