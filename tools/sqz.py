"""Decompressor for Titus the Fox .SQZ files.

Header (4 bytes): out_len = b0<<16 | b3<<8 | b2, type = b1.
  type 0x10      -> LZW (MSB-first bits, 9..12-bit codes, 0x100 clear, 0x101 end)
  anything else  -> Huffman tree + RLE
Huffman leaf symbols: <0x100 literal byte; 0x100 = repeat last byte N times, N = next symbol;
0x101 = repeat last byte N times, N = next two symbols (high byte first).
"""
import struct
import sys


def lzw_decode(data, out_len):
    out = bytearray()
    pos = bitbuf = bits = 0
    width, table, next_code, prev = 9, {}, 0x102, None
    while len(out) < out_len:
        while bits < width:
            if pos >= len(data):
                return bytes(out)
            bitbuf = (bitbuf << 8) | data[pos]
            pos += 1
            bits += 8
        bits -= width
        code = (bitbuf >> bits) & ((1 << width) - 1)
        bitbuf &= (1 << bits) - 1
        if code == 0x101:
            break
        if code == 0x100:
            width, table, next_code, prev = 9, {}, 0x102, None
            continue
        if code < 0x100:
            entry = bytes([code])
        elif code in table:
            entry = table[code]
        elif code == next_code and prev is not None:
            entry = prev + prev[:1]
        else:
            raise ValueError(f"invalid LZW code {code:#x}")
        out += entry
        if prev is not None and next_code < 4096:
            table[next_code] = prev + entry[:1]
            next_code += 1
            if next_code == (1 << width) and width < 12:
                width += 1
        prev = entry
    return bytes(out[:out_len])


def huffman_decode(data, out_len):
    tree_bytes = data[0] | data[1] << 8
    tree = struct.unpack(f"<{tree_bytes // 2}H", data[2:2 + tree_bytes])
    out = bytearray()
    node = last = state = high = 0
    for byte in data[2 + tree_bytes:]:
        for bit in range(7, -1, -1):
            value = tree[node // 2 + ((byte >> bit) & 1)]
            if not value & 0x8000:
                node = value
                continue
            node = 0
            sym = value & 0x7FFF
            if state == 1:                      # 0x100: one-symbol count
                out += bytes([last]) * sym
                state = 0
            elif state == 2:                    # 0x101: two-symbol count, high byte first
                high, state = sym, 3
            elif state == 3:                    # 0x101: low byte
                out += bytes([last]) * (high << 8 | sym)
                state = 0
            elif sym < 0x100:
                out.append(sym)
                last = sym
            elif sym == 0x100:
                state = 1
            elif sym == 0x101:
                state = 2
            else:
                out += bytes([last]) * (sym - 0x100)
            if len(out) >= out_len:     # trailing bits are padding; overshoot means a bad decode
                return bytes(out)
    return bytes(out)


def unsqz(raw):
    out_len = raw[0] << 16 | raw[3] << 8 | raw[2]
    if raw[1] == 0x10:
        out = lzw_decode(raw[4:], out_len)
    else:
        out = huffman_decode(raw[4:], out_len)
    if len(out) != out_len:
        raise ValueError(f"decoded {len(out)} bytes, header says {out_len}")
    return out


def load(path):
    with open(path, "rb") as f:
        return unsqz(f.read())


if __name__ == "__main__":
    for path in sys.argv[1:]:
        data = load(path)
        out_path = path.rsplit(".", 1)[0] + ".bin"
        with open(out_path, "wb") as f:
            f.write(data)
        print(f"{path}: {len(data)} bytes -> {out_path}")
