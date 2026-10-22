"""Readers for the Future Crew data formats used by Second Reality
(source release: github.com/mtuomi/SecondReality, Unlicense).

Every reader returns an Indexed image: width, height, a 256-entry palette of
6-bit VGA (r, g, b) tuples, and one palette index per pixel.
"""
import struct
from dataclasses import dataclass


@dataclass
class Indexed:
    w: int
    h: int
    pal: list  # 256 x (r, g, b), 0..63
    px: bytes  # w*h indices

    def row(self, y):
        return self.px[y * self.w:(y + 1) * self.w]


def _pal6(raw):
    return [tuple(raw[i * 3:i * 3 + 3]) for i in range(256)]


def read_fc(path):
    """FC picture files.

    'FD FC' (.UP and some .UH): u16 w, h, colours, add; palette at 16;
    rows start at add*16, each a u16 byte count then RLE data where a byte
    with bit 7 set repeats the next byte (byte & 127) times.
    'FC FC' (.U, font .UH): same header, raw pixels at add*16.
    'Uh1' (.UH, LENS.EXB): 16-byte header with w, h; palette at 16; raw
    pixels at 784.
    """
    data = open(path, "rb").read()
    magic = data[:2]
    if magic in (b"\xfd\xfc", b"\xfc\xfc"):
        w, h, _cols, add = struct.unpack_from("<4H", data, 2)
        pal = _pal6(data[16:16 + 768])
        pos = add * 16
        if magic == b"\xfc\xfc":
            return Indexed(w, h, pal, data[pos:pos + w * h])
        out = bytearray()
        for _ in range(h):
            (n,) = struct.unpack_from("<H", data, pos)
            pos += 2
            row, end = bytearray(), pos + n
            while pos < end:
                b = data[pos]
                pos += 1
                if b & 0x80:
                    row += bytes([data[pos]]) * (b & 0x7F)
                    pos += 1
                else:
                    row.append(b)
            out += row[:w].ljust(w, b"\0")
        return Indexed(w, h, pal, bytes(out))
    if data[:3] == b"Uh1":
        w, h = struct.unpack_from("<2H", data, 4)
        return Indexed(w, h, _pal6(data[16:784]), data[784:784 + w * h])
    raise ValueError(f"{path}: unknown FC format {data[:4]!r}")


def read_ilbm(path):
    """Deluxe Paint IFF: FORM ILBM (planar) or FORM PBM (chunky), with
    optional ByteRun1 compression. CMAP is 8-bit; returned as 6-bit."""
    data = open(path, "rb").read()
    if data[:4] != b"FORM":
        raise ValueError(f"{path}: not IFF")
    kind = data[8:12]
    pos, end = 12, 8 + struct.unpack_from(">I", data, 4)[0]
    w = h = planes = comp = 0
    pal = [(0, 0, 0)] * 256
    body = None
    while pos + 8 <= min(end, len(data)):
        cid = data[pos:pos + 4]
        (clen,) = struct.unpack_from(">I", data, pos + 4)
        chunk = data[pos + 8:pos + 8 + clen]
        if cid == b"BMHD":
            w, h, _x, _y, planes, _mask, comp = struct.unpack_from(">HHhhBBB", chunk)
        elif cid == b"CMAP":
            n = clen // 3
            pal = [tuple(c >> 2 for c in chunk[i * 3:i * 3 + 3]) for i in range(n)]
            pal += [(0, 0, 0)] * (256 - n)
        elif cid == b"BODY":
            body = chunk
        pos += 8 + clen + (clen & 1)
    if body is None:
        raise ValueError(f"{path}: no BODY")

    def unpack(src, nbytes, at):
        out = bytearray()
        while len(out) < nbytes:
            n = src[at]
            at += 1
            if n < 128:
                out += src[at:at + n + 1]
                at += n + 1
            elif n > 128:
                out += bytes([src[at]]) * (257 - n)
                at += 1
        return bytes(out[:nbytes]), at

    px = bytearray(w * h)
    at = 0
    if kind == b"PBM ":
        rowbytes = w + (w & 1)
        for y in range(h):
            if comp:
                row, at = unpack(body, rowbytes, at)
            else:
                row, at = body[at:at + rowbytes], at + rowbytes
            px[y * w:(y + 1) * w] = row[:w]
    else:
        rowbytes = ((w + 15) // 16) * 2
        for y in range(h):
            for p in range(planes):
                if comp:
                    row, at = unpack(body, rowbytes, at)
                else:
                    row, at = body[at:at + rowbytes], at + rowbytes
                for x in range(w):
                    if row[x >> 3] & (0x80 >> (x & 7)):
                        px[y * w + x] |= 1 << p
    return Indexed(w, h, pal, bytes(px))


def lum(rgb):
    """Brightness of a 6-bit colour, 0..255. Must match vga_lum() in
    src/gfx/gfx.h so converted pictures match runtime palette effects."""
    r, g, b = (min(63, max(0, c)) for c in rgb)
    luma = (r * 77 + g * 150 + b * 29) >> 8
    return ((luma + max(r, g, b)) * 255) // 126


def read_rix(path):
    """'RIX3' pictures (.SCI, .CLX): u16 width, u16 height, 2 more bytes,
    palette at 10, pixels at 778."""
    data = open(path, "rb").read()
    if data[:4] != b"RIX3":
        raise ValueError(f"{path}: not RIX3")
    w, h = struct.unpack_from("<2H", data, 4)
    px = data[778:778 + w * h]
    h = len(px) // w
    return Indexed(w, h, _pal6(data[10:778]), px[:w * h])
