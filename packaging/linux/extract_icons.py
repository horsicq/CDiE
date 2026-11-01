"""Regenerate the Linux desktop icons from src/gui/images/main.ico.

The committed PNGs under packaging/linux/icons/ are this script's output, so
packaging needs no image tools. Entry 0 of main.ico (256x256) is stored as a
PNG and is copied unchanged; the 128x128 and 48x48 entries are 32-bit BI_RGB
DIBs and are re-encoded as RGBA PNGs with the standard library alone.

    python3 packaging/linux/extract_icons.py
"""
from pathlib import Path
import struct
import sys
import zlib

APP_ID = "io.github.horsicq.cdie"
SIZES = (256, 128, 48)
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def png_chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def dib_to_png(dib, size):
    header_size, width, height, planes, bits, compression = struct.unpack_from("<IiiHHI", dib, 0)
    if width != size or height != 2 * size or planes != 1 or bits != 32 or compression != 0:
        raise ValueError(f"unsupported {size}x{size} entry: {width}x{height} {bits} bpp, compression {compression}")
    stride = size * 4
    pixels = dib[header_size:header_size + stride * size]
    mask_stride = ((size + 31) // 32) * 4
    mask = dib[header_size + stride * size:header_size + stride * size + mask_stride * size]
    if len(pixels) != stride * size or len(mask) != mask_stride * size:
        raise ValueError(f"truncated {size}x{size} entry")
    # A 32-bit icon normally carries alpha; an all-zero alpha channel means
    # the transparency lives in the AND mask instead (1 = transparent).
    use_mask = not any(pixels[3::4])
    rows = []
    for y in range(size):
        source = size - 1 - y  # DIB rows are stored bottom-up
        row = bytearray(b"\x00")  # filter type None
        for x in range(size):
            b, g, r, a = pixels[source * stride + 4 * x:source * stride + 4 * x + 4]
            if use_mask:
                transparent = mask[source * mask_stride + x // 8] & (0x80 >> (x % 8))
                a = 0 if transparent else 255
            row += bytes((r, g, b, a))
        rows.append(bytes(row))
    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)  # 8-bit RGBA
    return (PNG_SIGNATURE + png_chunk(b"IHDR", header) +
            png_chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + png_chunk(b"IEND", b""))


def main():
    root = Path(__file__).resolve().parents[2]
    icon = (root / "src" / "gui" / "images" / "main.ico").read_bytes()
    reserved, kind, count = struct.unpack_from("<HHH", icon, 0)
    if reserved != 0 or kind != 1:
        raise ValueError("main.ico is not an icon file")
    entries = {}
    for index in range(count):
        width, height, _, _, _, _, length, offset = struct.unpack_from("<BBBBHHII", icon, 6 + 16 * index)
        width, height = width or 256, height or 256
        if width == height and width in SIZES and width not in entries:
            entries[width] = icon[offset:offset + length]
    for size in SIZES:
        if size not in entries:
            raise ValueError(f"main.ico has no {size}x{size} entry")
        data = entries[size]
        png = data if data.startswith(PNG_SIGNATURE) else dib_to_png(data, size)
        target = root / "packaging" / "linux" / "icons" / f"{size}x{size}" / f"{APP_ID}.png"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(png)
        print(f"{target.relative_to(root)}: {len(png)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
