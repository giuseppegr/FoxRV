"""Minimal stdlib-only graphics helpers: indexed PNG writer and EGA planar decoding."""
import struct
import zlib

# Standard 16-colour EGA palette (used when the game's own palette is unknown).
EGA_PALETTE = [
    (0x00, 0x00, 0x00), (0x00, 0x00, 0xAA), (0x00, 0xAA, 0x00), (0x00, 0xAA, 0xAA),
    (0xAA, 0x00, 0x00), (0xAA, 0x00, 0xAA), (0xAA, 0x55, 0x00), (0xAA, 0xAA, 0xAA),
    (0x55, 0x55, 0x55), (0x55, 0x55, 0xFF), (0x55, 0xFF, 0x55), (0x55, 0xFF, 0xFF),
    (0xFF, 0x55, 0x55), (0xFF, 0x55, 0xFF), (0xFF, 0xFF, 0x55), (0xFF, 0xFF, 0xFF),
]


def vga_palette(raw):
    """Convert a 6-bit-per-channel VGA palette (n*3 bytes) to 8-bit RGB tuples."""
    return [tuple((raw[i + c] & 0x3F) * 255 // 63 for c in range(3))
            for i in range(0, len(raw), 3)]


def write_png(path, width, height, pixels, palette, transparent=None):
    """Write an 8-bit indexed PNG. `pixels` is width*height palette indices.
    `transparent` optionally names one palette index to make fully transparent."""
    def chunk(tag, body):
        return (struct.pack(">I", len(body)) + tag + body
                + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))

    rows = b"".join(b"\0" + bytes(pixels[y * width:(y + 1) * width]) for y in range(height))
    plte = b"".join(bytes(rgb) for rgb in palette)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)))
        f.write(chunk(b"PLTE", plte))
        if transparent is not None:
            f.write(chunk(b"tRNS", b"\xff" * transparent + b"\0"))
        f.write(chunk(b"IDAT", zlib.compress(rows, 9)))
        f.write(chunk(b"IEND", b""))


def planar_to_chunky(data, width, height):
    """Decode 4 consecutive bit-planes (plane 0 = bit 0) into one index per pixel."""
    plane_size = width * height // 8
    out = bytearray(width * height)
    for plane in range(4):
        base = plane * plane_size
        for i in range(plane_size):
            byte = data[base + i]
            if not byte:
                continue
            for bit in range(8):
                if byte & (0x80 >> bit):
                    out[i * 8 + bit] |= 1 << plane
    return out
