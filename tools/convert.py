#!/usr/bin/env python3
"""Convert Second Reality data into Flipper assets.

    python3 tools/convert.py /path/to/SecondReality [out_dir] [--w32 /path/to/SecondRealityW32]

The first argument is a checkout of Future Crew's source release
(github.com/mtuomi/SecondReality, Unlicense). Output goes to assets/ by
default, which ufbt copies to /ext/apps_assets/second on install.

--w32 is a checkout of Conspiracy's reconstruction
(github.com/ConspiracyHu/SecondRealityW32, Unlicense). The source release
has a "PARTPIC MISSING" placeholder for the second credits picture (the U2A
ships) and a draft end scroller text; the reconstruction has the real ones.
Without it the placeholder and the draft are used.

Output format (.srg, "grey"): 12-byte header
    'SRG1', u16 width, u16 height, u16 frames, u16 reserved
followed by width*height*frames bytes of brightness (0 black..255 white),
row-major, frame after frame.
"""
import os
import re
import struct
import sys

from PIL import Image, ImageChops

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fcformats import lum, read_fc, read_ilbm, read_rix  # noqa: E402

# 4:3 pictures are shown in an 85x64 window (src/gfx/gfx.h VIEW_43)
VIEW43 = (85, 64)

# The opening (Alku, U2A ships, PAM explosion) is letterboxed in the
# original: picture in rows 25..174 of 320x200. That band (about 2.1:1) is
# shown full screen, like Plasma (src/gfx/gfx.h BAND_*).
BAND_Y0, BAND_H = 25, 150
FULL = (128, 64)


def to_grey(img, crop=None):
    """Indexed picture -> PIL 'L' image of palette brightness."""
    lut = bytes(lum(c) for c in img.pal)
    im = Image.frombytes("L", (img.w, img.h), img.px.translate(lut))
    return im.crop(crop) if crop else im


def write_srg(path, frames):
    w, h = frames[0].size
    with open(path, "wb") as f:
        f.write(b"SRG1" + struct.pack("<4H", w, h, len(frames), 0))
        for im in frames:
            assert im.size == (w, h)
            f.write(im.tobytes())
    print(f"{os.path.basename(path)}: {w}x{h}x{len(frames)}")


def picture(src, size, crop=None):
    return to_grey(read_fc(src), crop).resize(size, Image.BOX)


def mean(im):
    hist = im.histogram()
    return sum(v * n for v, n in enumerate(hist)) / sum(hist)


def stretch(frames, pct=99.5):
    """Scale brightness so the pct-th percentile becomes white (dark
    sources would otherwise be almost black on the LCD)."""
    hist = [0] * 256
    for im in frames:
        for v, n in enumerate(im.histogram()):
            hist[v] += n
    total, acc, top = sum(hist), 0, 255
    for v in range(256):
        acc += hist[v]
        if acc >= total * pct / 100:
            top = max(v, 1)
            break
    return [im.point(lambda v: min(255, v * 255 // top)) for im in frames]


# ---------------------------------------------------------------- fonts

def c_string(src, name):
    """Raw bytes of `char *name="...";` in an FC C file (CP437 kept)."""
    data = open(src, "rb").read()
    m = re.search(rb"\*" + name.encode() + rb'\s*=\s*"((?:[^"\\]|\\.)*)"', data)
    return m.group(1).replace(b'\\"', b'"')


def find_glyphs(ink, w, h, order):
    """Glyphs sit left to right in `order`, separated by blank columns."""
    glyphs, x = {}, 0
    for ch in order:
        while x < w and not any(ink(x, y) for y in range(h)):
            x += 1
        start = x
        while x < w and any(ink(x, y) for y in range(h)):
            x += 1
        if x > start:
            glyphs[ch] = (start, x - start)
    return glyphs


def fit_glyphs(strip, glyphs, height, x_scale, bands):
    """Glyph tiles {code: 'L' image, height rows}, each glyph resized on its
    own so it starts on a pixel boundary and gets whole-pixel stems.
    Vertically the source rows are mapped band by band, bands =
    [(src_y0, src_y1, rows)], so baseline and x-height land on pixel edges;
    rows the bands don't fill stay blank."""
    def fit(col):
        out = Image.new("L", (col.size[0], height))
        y = 0
        for y0, y1, n in bands:
            band = col.crop((0, y0, col.size[0], y1))
            if n == 1:
                # several rows into one: averaging dims the thin tops of C,
                # S, R next to the full bars of E, T, and they look cut off.
                # Take the brightest of the top half's rows (the stroke);
                # the curve's sides below show in the next band, and leaving
                # them out keeps O and C round instead of square.
                row = band.crop((0, 0, band.size[0], 1))
                for r in range(1, max(1, band.size[1] // 2)):
                    row = ImageChops.lighter(row, band.crop((0, r, band.size[0], r + 1)))
                band = row
            out.paste(band.resize((col.size[0], n), Image.BOX), (0, y))
            y += n
        return out

    def level(v):
        # box-filtered strokes go soft at 7-9 px: firm them up, then snap to
        # the display's 5 grey levels (multiples of 64 show as exact temporal
        # greys, src/display/dither.c, with no ordered-dither speckle)
        v = max(0, min(255, (v - 40) * 2))
        return min(255, (v + 32) // 64 * 64)

    tiles = {}
    for ch, (x, w) in glyphs.items():
        nw = max(1, round(w * x_scale))
        col = strip.crop((x, 0, x + w, strip.size[1])).resize((nw, strip.size[1]), Image.BOX)
        tiles[ch] = fit(col).point(level)
    return tiles


def write_font(path, tiles, height, space, x_scale):
    """Font (.srf): 'SRF1', u8 height, u8 gap, u16 strip width, then 256 x
    (u16 x, u8 w), then height rows of strip brightness (0..255). tiles
    from fit_glyphs; space is the space's source width."""
    table, sw = [(0, 0)] * 256, 0
    for ch, im in sorted(tiles.items()):
        table[ch] = (sw, im.size[0])
        sw += im.size[0]
    sw += 1  # blank column at the end for the space
    small = Image.new("L", (sw, height))
    for ch, im in tiles.items():
        small.paste(im, (table[ch][0], 0))
    table[32] = (sw - 1, max(2, round(space * x_scale)))
    gap = 1
    with open(path, "wb") as f:
        f.write(b"SRF1" + struct.pack("<BBH", height, gap, sw))
        for x, w in table:
            f.write(struct.pack("<HB", x, w))
        f.write(small.tobytes())
    print(f"{os.path.basename(path)}: {len(tiles)} glyphs, {sw}x{height}")


DOLBY_LETTERS = {  # 7 rows, pure black and white
    "D": ["###.", "#..#", "#..#", "#..#", "#..#", "#..#", "###."],
    "O": [".##.", "#..#", "#..#", "#..#", "#..#", "#..#", ".##."],
    "L": ["#...", "#...", "#...", "#...", "#...", "#...", "####"],
    "B": ["###.", "#..#", "#..#", "###.", "#..#", "#..#", "###."],
    "Y": ["#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."],
    "S": [".##.", "#..#", "#...", ".##.", "...#", "#..#", ".##."],
    "U": ["#..#", "#..#", "#..#", "#..#", "#..#", "#..#", ".##."],
    "R": ["###.", "#..#", "#..#", "###.", "#.#.", "#..#", "#..#"],
    "N": ["#..#", "##.#", "##.#", "#.##", "#.##", "#..#", "#..#"],
}


def dolby_logo():
    """Alku's "in [Dolby Surround]" logo. The original builds it from two
    215 x 30 font pieces drawn 19 rows apart (320x400 mode): the double-D
    and "DOLBY SURROUND" in a box, 40 rows tall, which is under 9 of our
    rows - too small to scale down legibly. Redrawn here as 1-bit pixel art,
    11 rows: the double-D as two blocks with a D cut into each, facing away,
    and the words in condensed capitals (Y is 5 wide so its stem is
    centred) in a box."""
    d = ["######",
         "#..###",
         "#...##",
         "#....#",
         "#....#",
         "#....#",
         "#....#",
         "#....#",
         "#...##",
         "#..###",
         "######"]
    words = "DOLBY SURROUND"
    text_w = sum(len(DOLBY_LETTERS[c][0]) + 1 if c != " " else 3 for c in words) - 1
    box_x = 2 * 6 + 1 + 2
    logo = Image.new("L", (box_x + text_w + 4, 11))
    px = logo.load()
    for y, row in enumerate(d):
        for x, ch in enumerate(row):
            if ch == "#":
                px[x, y] = px[2 * 6 + 1 - 1 - x, y] = 255  # left D, mirrored right D
    for x in range(box_x, logo.size[0]):
        px[x, 0] = px[x, 10] = 255
    for y in range(11):
        px[box_x, y] = px[logo.size[0] - 1, y] = 255
    x = box_x + 2
    for c in words:
        if c == " ":
            x += 3
            continue
        for y, row in enumerate(DOLBY_LETTERS[c]):
            for dx, ch in enumerate(row):
                if ch == "#":
                    px[x + dx, 2 + y] = 255
        x += len(DOLBY_LETTERS[c][0]) + 1
    return logo


# FC serif font (Alku, end scroller): capitals on rows 0..13, x-height
# 4..13, descenders down to 17, rest of the 30 rows empty. Pixel-aligned 8
# rows: 1 above the x-height, 5 for it, 2 for descenders.
FC_FONT_ROWS = [(0, 4, 1), (4, 14, 5), (14, 18, 2)]


def fc_font(src_pic, src_c, height, space):
    img = read_fc(src_pic)
    order = c_string(src_c, "fonaorder")
    glyphs = find_glyphs(lambda x, y: img.px[y * img.w + x] != 0, img.w, img.h, order)
    # brightness from the font's own palette, but full-scale (text is white)
    grey = to_grey(img)
    return stretch([grey], 100)[0], glyphs


def endscrl_w32(w32):
    """The final end scroller font and text, from the reconstruction. The
    font (ENDSCRL_FONT.DAT, 25 x 1550, values 0..3) adds Z, Y, a/o umlauts,
    / and & to the source release's. Its order string ends in " and ; but
    the strip has no glyphs for them (the " slot finds a stray fragment),
    while the two ticks after ':' make a double quote: use those for "."""
    w, h = 1550, 25
    px = open(os.path.join(w32, "Parts/ENDSCRL/ENDSCRL_FONT.DAT"), "rb").read()
    strip = Image.frombytes("L", (w, h), bytes(v * 85 for v in px))
    order = c_string(os.path.join(w32, "Parts/ENDSCRL/ENDSCRL_MAIN.C"), "fonaorder")
    order = order[:order.index(b"&") + 1].replace(b"\x8f\x8f", b'"\x01')
    glyphs = find_glyphs(lambda x, y: px[y * w + x] != 0, w, h, order)
    (x0, _), (x1, w1) = glyphs[ord('"')], glyphs.pop(1)
    glyphs[ord('"')] = (x0, x1 + w1 - x0)

    # '%' loops the original back to the top (the music ends it); stop there
    # and drop the trailing blank lines. Whitespace-only lines are blank,
    # "[NN" lines are NN-row gaps (rows of the 25-row line pitch).
    text = open(os.path.join(w32, "Data/ENDSCROL.TXT"), "rb").read().replace(b"\r", b"")
    lines = [ln if ln.strip() else b"" for ln in text.split(b"\n")]
    lines = lines[:lines.index(b"%")]
    while lines and not lines[-1]:
        lines.pop()
    return strip, glyphs, b"\n".join(lines) + b"\n"


def credits_font(src_inc, src_c, height):
    """CREDITS/FONA.INC: assembler 'db' rows, 32 x 1500, values 0..9."""
    vals = [int(v) for v in re.findall(rb"\d+", b" ".join(
        line.split(b"db", 1)[1] for line in open(src_inc, "rb") if b"db" in line))]
    w, h = 1500, len(vals) // 1500
    px = bytes(min(255, v * 255 // 9) for v in vals[:w * h])
    strip = Image.frombytes("L", (w, h), px)
    order = c_string(src_c, "fonaorder")
    glyphs = find_glyphs(lambda x, y: px[y * w + x] != 0, w, h, order)
    return strip, glyphs


# ---------------------------------------------------------------- text

def alku_cards(src_c, max_cards=7):
    """Text cards of ALKU/MAIN.C: prtc(160, y, "...") on the black screen
    and addtext/faddtext(160, y, "...") over the landscape (y relative to a
    text band ~100 rows down). A call whose y doesn't increase starts a new
    card; commented-out calls are skipped. Output lines are 'y text', cards
    separated by an empty line. The text "\\x8f" stands for the Dolby logo
    (alku_logo.srg, see dolby_logo)."""
    lines = [ln for ln in open(src_c, "rb").read().split(b"\n") if not ln.strip().startswith(b"//")]
    calls = re.findall(rb'(prtc|f?addtext)\(\s*160\s*,\s*(\d+)\s*,\s*"([^"]*)"\s*\)', b"\n".join(lines))
    cards, last = [[]], None
    for fn, y, txt in calls:
        y = int(y) + (0 if fn == b"prtc" else 100)
        if last is not None and y <= last:
            cards.append([])
        if txt != b"\x99":  # the logo's second piece: it's one picture here
            cards[-1].append(b"%d " % y + txt)
        last = y
    return b"\n".join(b"\n".join(c) + b"\n" for c in cards[:max_cards])


def credit_screens(src_c):
    """screenin(picN, "line\\0" "line\\0" ...) calls in CREDITS/MAIN.C."""
    data = open(src_c, "rb").read()
    screens = []
    for pic, body in re.findall(rb"screenin\(\s*(pic\w+)\s*,(.*?)\)\s*;", data, re.S):
        lines = [s.replace(b"\\0", b"") for s in re.findall(rb'"([^"]*)"', body)]
        screens.append((pic.decode(), [ln for ln in lines]))
    return screens


# ---------------------------------------------------------------- 3D

def c_array(src, name, defines):
    """Values of `type name[...]={...};` in an FC C file. Entries are
    integers, hex, or products with #define'd constants (e.g. -100*ZZZ)."""
    text = re.sub(rb"//[^\n]*", b"", open(src, "rb").read())
    m = re.search(rb"\b" + name.encode() + rb"\s*\[[^\]]*\]\s*=\s*\{(.*?)\}\s*;", text, re.S)
    vals = []
    for item in m.group(1).split(b","):
        item = item.strip().decode()
        if not item:
            continue
        v = 1
        for factor in item.split("*"):
            factor = factor.strip()
            neg = factor.startswith("-")
            factor = factor.lstrip("-")
            f = defines[factor] if factor in defines else int(factor, 0)
            v *= -f if neg else f
        vals.append(v)
    return vals


def glenz_objects(src_c):
    """glenz.bin: two objects, each u8 vertex count, int16 x,y,z per vertex,
    u8 polygon count, then per polygon u8 colour code (low byte of the
    original 0x40xx word), u8 vertex count, vertex indices."""
    text = open(src_c, "rb").read()
    defines = {k.decode(): int(v) for k, v in re.findall(rb"#define\s+(\w+)\s+(\d+)", text)}
    out = b""
    for pts, polys in (("points", "epolys"), ("pointsb", "epolysb")):
        p = c_array(src_c, pts, defines)
        n = p[0]
        out += struct.pack("<B", n) + struct.pack("<%dh" % (3 * n), *p[1:1 + 3 * n])
        q = c_array(src_c, polys, defines)
        faces, i = [], 0
        while q[i]:
            nv, colour = q[i], q[i + 1]
            faces.append(struct.pack("<BB", colour & 0xFF, nv) + bytes(q[i + 2:i + 2 + nv]))
            i += 2 + nv
        out += struct.pack("<B", len(faces)) + b"".join(faces)
    return out


def asm_words(src, defines):
    """'dw' values from an FC assembler include. Comments (;) are dropped,
    `name=value` defines are honoured, REPT n ... ENDM blocks expanded
    (capped at 16 copies: only the first few are ever reached)."""
    words, block, rept = [], None, 0
    for raw in open(src, "rb").read().decode("latin-1").splitlines():
        line = raw.split(";", 1)[0].strip()
        m = re.match(r"(\w+)\s*=\s*(-?\d+)$", line)
        if m:
            defines[m.group(1)] = int(m.group(2))
            continue
        m = re.match(r"rept\s+(\d+)$", line, re.I)
        if m:
            block, rept = [], min(int(m.group(1)), 16)
            continue
        if line.lower() == "endm" and block is not None:
            words += block * rept
            block = None
            continue
        if not line.lower().startswith("dw"):
            continue
        vals = []
        for item in line[2:].split(","):
            v = 1
            for factor in item.strip().split("*"):
                factor = factor.strip()
                neg = factor.startswith("-")
                factor = factor.lstrip("-")
                f = defines[factor] if factor in defines else int(factor, 0)
                v *= -f if neg else f
            vals.append(v)
        (block if block is not None else words).extend(vals)
    return words


def plz_cube(src_dir):
    """plzcube.bin: u16 coefficient count, int16 spline coefficients
    (4 x 256), u16 key count, keys of 8 int16 (dx, dy, dz, kx, ky, kz,
    light kx, light ky)."""
    coef = asm_words(os.path.join(src_dir, "SPLINE.INC"), {})
    keys = asm_words(os.path.join(src_dir, "RATA.INC"), {})
    n = len(keys) // 8
    return (struct.pack("<H", len(coef)) + struct.pack("<%dh" % len(coef), *coef) +
            struct.pack("<H", n) + struct.pack("<%dh" % (n * 8), *keys[:n * 8]))


def coman_maps(src_dir):
    """coman.bin: the two voxel height maps (256x128 int16 each in the
    original, indexed by a packed x | row << 8 position) as int8 with every
    row kept and x halved (they're smooth along x): 2 x 128x128 bytes."""
    out = b""
    for name in ("W1DTA.BIN", "W2DTA.BIN"):
        d = struct.unpack("<32768h", open(os.path.join(src_dir, name), "rb").read())
        out += bytes((max(-128, min(127, (d[r * 256 + 2 * c] + d[r * 256 + 2 * c + 1]) // 2))) & 255
                     for r in range(128) for c in range(128))
    return out


def u2_chunks(data):
    pos = 0
    while pos + 8 <= len(data):
        tag = data[pos:pos + 4]
        (ln,) = struct.unpack_from("<I", data, pos + 4)
        yield tag, data[pos + 8:pos + 8 + ln]
        if tag == b"END ":
            break
        pos += 8 + ln


U2_SCALE = 8  # vertex coordinates are stored divided by this


def u2_object(data):
    """One U2 object file -> compact record:
    u16 nverts, u16 centre vertex, nverts x (int16 x, y, z [/U2_SCALE],
    int16 normal or -1), u16 nnorm, u16 nnorm_flat, nnorm x int8 (x, y, z) [/128],
    u16 npolys, u8 index size (1 or 2 bytes), npolys x (u8 sides, u8 flags,
    u8 colour, u16 normal, sides x vertex index)."""
    verts, norms, polys, nflat, centre = [], [], [], 0, 0
    for tag, c in u2_chunks(data):
        if tag == b"VERT":
            (n,) = struct.unpack_from("<H", c, 0)
            for i in range(n):
                x, y, z, nn, _ = struct.unpack_from("<iiihh", c, 4 + i * 16)
                verts.append((x // U2_SCALE, y // U2_SCALE, z // U2_SCALE, nn))
        elif tag == b"NORM":
            n, nflat = struct.unpack_from("<HH", c, 0)
            for i in range(n):
                x, y, z, _ = struct.unpack_from("<hhhh", c, 4 + i * 8)
                norms.append(tuple(max(-127, min(127, v // 128)) for v in (x, y, z)))
        elif tag == b"POLY":
            q = 2
            while q < len(c) and c[q]:
                sides, flags, colour = c[q], c[q + 1], c[q + 2]
                (normal,) = struct.unpack_from("<H", c, q + 4)
                vs = struct.unpack_from("<%dH" % sides, c, q + 6)
                polys.append((sides, flags, colour, normal, vs))
                q += 6 + 2 * sides
        elif tag == b"ORD0":
            (centre,) = struct.unpack_from("<H", c, 2)
    out = struct.pack("<HH", len(verts), centre)
    for x, y, z, nn in verts:
        out += struct.pack("<hhhh", x, y, z, nn)  # normal -1 = none
    out += struct.pack("<HH", len(norms), nflat)
    for v in norms:
        out += struct.pack("<bbb", *v)
    wide = len(verts) > 256  # vertex indices as u16 only when needed
    out += struct.pack("<HB", len(polys), 2 if wide else 1)
    for sides, flags, colour, normal, vs in polys:
        out += struct.pack("<BBBH", sides, flags, colour, normal)
        out += struct.pack("<%d%s" % (sides, "H" if wide else "B"), *vs)
    return out


def u2_frames(anim):
    """Number of frames in a U2 animation stream (each ends with an
    0xff <fov> command; 0xff 0xff ends the stream)."""
    p = frames = 0
    while p < len(anim):
        b = anim[p]
        p += 1
        if b == 0xFF:
            c = anim[p]
            p += 1
            if c <= 0x7F:
                frames += 1
                continue
            if c == 0xFF:
                break
            b = c
        if (b & 0xC0) == 0xC0:
            b = anim[p]
            p += 1
        k = (b & 0x30) >> 4
        flag = int.from_bytes(anim[p:p + k], "little")
        p += k
        for sh in (0, 2, 4):
            p += (0, 1, 2, 4)[(flag >> sh) & 3]
        for i in range(9):
            if flag & (0x80 << i):
                p += 2 if flag & 0x40 else 1
    return frames


def u2_scene(data_dir, scene):
    """<scene>.bin: 'SRU2', u8 object files, u8 instances (incl. camera 0),
    u16 animation frames, u32 animation length, per instance 1.. its object
    file (1-based), 256 palette brightness values, then the objects. The
    animation stream (<scene>.0AB) is copied to <scene>.anm and decoded on
    the device."""
    m = open(os.path.join(data_dir, scene + ".00M"), "rb").read()
    (ip,) = struct.unpack_from("<I", m, 4)
    (conum,) = struct.unpack_from("<H", m, ip)
    inst = struct.unpack_from("<%dH" % (conum - 1), m, ip + 2)
    files = sorted(set(inst))
    assert files == list(range(1, len(files) + 1))
    lums = bytes(lum(m[16 + i * 3:19 + i * 3]) for i in range(256))
    anim = open(os.path.join(data_dir, scene + ".0AB"), "rb").read()
    out = (b"SRU2" + struct.pack("<BBHI", len(files), conum, u2_frames(anim), len(anim)) +
           bytes(inst) + lums)
    for f in files:
        out += u2_object(open(os.path.join(data_dir, "%s.%03d" % (scene, f)), "rb").read())
    return out


def lin2d(off):
    """Split a 320-stride linear offset into (dx, dy), dx in [-160, 160)."""
    dy = (off + 160) // 320
    return off - dy * 320, dy


def read_pos_tables(paths, count):
    """FOREST POS*.DAT / WATER WAT*.DAT: for each of `count` window pixels,
    u16 n then n u16 screen offsets (320x200) where that pixel appears.
    The three files cover interlaced screen rows; combined they give the
    full window pixel -> screen pixels mapping."""
    hits = {}  # screen offset -> list of window pixel indices
    for path in paths:
        d = open(path, "rb").read()
        p = 0
        for k in range(count):
            (n,) = struct.unpack_from("<H", d, p)
            p += 2
            for _ in range(n):
                (off,) = struct.unpack_from("<H", d, p)
                p += 2
                hits.setdefault(off, []).append(k)
    return hits


def reverse_map(hits, view=VIEW43):
    """For each pixel of the 4:3 view, the window pixel seen there most
    often (0xFFFF = none)."""
    vw, vh = view
    out = []
    for fy in range(vh):
        for fx in range(vw):
            votes = {}
            for y in range(fy * 200 // vh, (fy + 1) * 200 // vh):
                for x in range(fx * 320 // vw, (fx + 1) * 320 // vw):
                    for k in hits.get(y * 320 + x, ()):
                        votes[k] = votes.get(k, 0) + 1
            out.append(max(votes, key=votes.get) if votes else 0xFFFF)
    return out


def sparse_map(rmap, win_w, rows, view=VIEW43):
    """The screen pixels of a reverse map that show a strip pixel (window
    row < rows): (x, y, window row, window column) each, in screen order."""
    vw = view[0]
    return [(i % vw, i // vw, k // win_w, k % win_w) for i, k in enumerate(rmap)
            if k != 0xFFFF and k // win_w < rows]


def lum_view(index_px, pal, view=VIEW43, w=320, h=200):
    """Box-filtered brightness of an indexed 320x200 picture."""
    lut = bytes(lum(pal[i]) for i in range(256))
    return Image.frombytes("L", (w, h), bytes(index_px).translate(lut)).resize(view, Image.BOX)


def forest(src_dir, out):
    """forest.bin: 'SRF3', u16 entries n, u16 text w, h; n x (u8 x, y,
    window row, window column, text-over-background brightness with the
    full palette, the same with the leaves-only one) - only the screen
    pixels the text can reach; then text levels 0..7, two pixels per byte
    (even x in the low nibble), h rows of w/2 bytes.
    forest.srg: 2 frames 85x64 - background (full palette), background
    (leaves-only palette)."""
    hill = read_rix(os.path.join(src_dir, "HILLBACK.CLX"))
    text = read_rix(os.path.join(src_dir, "O2.SCI"))
    win_w, win_h = 237, 31
    hits = read_pos_tables([os.path.join(src_dir, "POS%d.DAT" % i) for i in (1, 2, 3)], win_w * win_h)
    full = hill.pal
    leaves = [(0, 0, 0) if (i < 32 or 128 <= i < 160) else c for i, c in enumerate(full)]
    on = bytes((p + 128 + 7) & 255 for p in hill.px)  # brightest text level
    write_srg(os.path.join(out, "forest.srg"), [lum_view(hill.px, full), lum_view(hill.px, leaves)])
    tw, th = text.w, 30
    px = text.px[:tw * th]
    nibbles = bytes((px[i] & 15) | (px[i + 1] & 15) << 4 for i in range(0, tw * th, 2))
    data = forest_bin(reverse_map(hits), win_w, lum_view(on, full).tobytes(), lum_view(on, leaves).tobytes(),
                      tw, th, nibbles)
    open(os.path.join(out, "forest.bin"), "wb").write(data)
    print("forest.bin: window %dx%d, text %dx%d" % (win_w, win_h, tw, th))


def forest_bin(rmap, win_w, on_full, on_leaves, tw, th, nibbles):
    entries = sparse_map(rmap, win_w, th)
    out = b"SRF3" + struct.pack("<4H", len(entries), tw, th, 0)
    for x, y, r, c in entries:
        i = y * VIEW43[0] + x
        out += bytes((x, y, r, c, on_full[i], on_leaves[i]))
    return out + nibbles


def water(src_dir, out):
    """water.bin: 'SRW2', u16 entries n, u16 sword w, h; n x (u8 x, y,
    window row, window column) - only the screen pixels the sword can
    reach; then sword brightness (h rows of w bytes, 0 = transparent).
    water.srg: background 85x64."""
    sword = read_rix(os.path.join(src_dir, "MIEKKA.SCI"))
    bkg = read_rix(os.path.join(src_dir, "BKG.CLX"))
    win_w, win_h = 158, 34
    hits = read_pos_tables([os.path.join(src_dir, "WAT%d.DAT" % i) for i in (1, 2, 3)], win_w * win_h)
    write_srg(os.path.join(out, "water.srg"), [lum_view(bkg.px, sword.pal)])
    sw, sh = sword.w, 34
    lut = bytes([0] + [max(1, lum(sword.pal[i])) for i in range(1, 256)])
    data = water_bin(reverse_map(hits), win_w, sw, sh, sword.px[:sw * sh].translate(lut))
    open(os.path.join(out, "water.bin"), "wb").write(data)
    print("water.bin: window %dx%d, sword %dx%d" % (win_w, win_h, sw, sh))


def water_bin(rmap, win_w, sw, sh, sword):
    entries = sparse_map(rmap, win_w, sh)
    out = b"SRW2" + struct.pack("<4H", len(entries), sw, sh, 0)
    for e in entries:
        out += bytes(e)
    return out + sword


def lens_maps(src_dir, out):
    """lens.lmap: 'SRLM', u8 w, u8 h (lens box in screen px), 3 tint
    brightness values, then w*h x (int8 dx, int8 dy, u8 class): class 1-3 =
    show the class-tinted background at (x+dx, y+dy), 0 = plain background.
    lens4.srg: background with class 0..3 tints (4 frames)."""
    ex = [open(os.path.join(src_dir, "LENS.EX%d" % i), "rb").read() for i in range(5)]
    wid, hig = struct.unpack_from("<HH", ex[0], 0)
    tints = [tuple(ex[0][4 + i * 3:7 + i * 3]) for i in range(3)]
    entry = {}  # (x, y) in lens -> (sx, sy, class)

    def row_data(table, r):
        off, n = struct.unpack_from("<HH", table, r * 4)
        return off, n

    for r in range(hig):
        off, n = row_data(ex[1], r)  # centre: consecutive destinations
        if n:
            (base,) = struct.unpack_from("<h", ex[1], off)
            for i in range(n):
                (s,) = struct.unpack_from("<h", ex[1], off + 2 + 2 * i)
                dx, dy = lin2d(base + i)
                sx, sy = lin2d(base + s)
                entry[(dx, r + dy)] = (sx, r + sy, 1)
        for cls, t in ((2, ex[2]), (3, ex[3])):  # rims: (dest, src) pairs
            off, n = row_data(t, r)
            if n:
                (base,) = struct.unpack_from("<h", t, off)
                for i in range(n):
                    d, s = struct.unpack_from("<hh", t, off + 2 + 4 * i)
                    dx, dy = lin2d(base + d)
                    sx, sy = lin2d(base + s)
                    entry[(dx, r + dy)] = (sx, r + sy, cls)
        off, n = row_data(ex[4], r)  # restored to plain background
        if n:
            (base,) = struct.unpack_from("<h", ex[4], off)
            for i in range(n):
                (s,) = struct.unpack_from("<h", ex[4], off + 2 + 2 * i)
                dx, dy = lin2d(base + s)
                entry.pop((dx, r + dy), None)

    kx, ky = VIEW43[0] / 320, VIEW43[1] / 200
    w, h = int(wid * kx) + 1, int(hig * ky) + 1
    cells = b""
    for y in range(h):
        for x in range(w):
            ox, oy = int((x + 0.5) / kx), int((y + 0.5) / ky)
            e = entry.get((ox, oy))
            if e:
                sx, sy, cls = e
                cells += struct.pack("<bbB", round((sx - ox) * kx), round((sy - oy) * ky), cls)
            else:
                cells += b"\0\0\0"
    exb = read_fc(os.path.join(src_dir, "LENS.EXB"))
    frames = []
    for c in range(4):
        t = tints[c - 1] if c else (0, 0, 0)
        pal = [tuple(min(63, a + b) for a, b in zip(col, t)) for col in exb.pal]
        frames.append(lum_view(exb.px, pal))
    write_srg(os.path.join(out, "lens4.srg"), frames)
    with open(os.path.join(out, "lens.lmap"), "wb") as f:
        f.write(b"SRLM" + struct.pack("<BB", w, h) + bytes(lum(t) for t in tints) + cells)
    print("lens.lmap: %dx%d cells, %d lens pixels" % (w, h, len(entry)))


def checker(src_uh):
    """Checkerboard for the Glenz parts as palette indices (the scenes blend
    through a live palette, so no brightness conversion): rows 0..110 kept,
    columns sampled to the screen width; plus the first 16 palette colours."""
    img = read_fc(src_uh)
    rows = 111
    px = bytes(img.px[y * img.w + (x * img.w) // 128] for y in range(rows) for x in range(128))
    pal = bytes(c for rgb in img.pal[:16] for c in rgb)
    return Image.frombytes("L", (128, rows), px), pal


# ---------------------------------------------------------------- PAM

def pam_frames(src_ani, src_pal, count):
    """OUT.ANI: frames start on 16-byte boundaries; signed bytes n > 0 =
    run of n pixels of the next byte's colour, n < 0 = skip -n pixels,
    0 = end of frame. Each frame is a delta on the previous one."""
    data = open(src_ani, "rb").read()
    pal = open(src_pal, "rb").read()
    lut = bytes(lum(pal[i * 3:i * 3 + 3]) for i in range(256))
    fb, pos, frames = bytearray(64000), 0, []
    while pos < len(data) - 1 and len(frames) < count:
        pos = (pos + 15) & ~15
        p = 0
        while True:
            n = data[pos] - 256 if data[pos] > 127 else data[pos]
            pos += 1
            if n > 0:
                fb[p:p + n] = bytes([data[pos]]) * n
                p += n
                pos += 1
            elif n < 0:
                p -= n
            else:
                break
        frames.append(Image.frombytes("L", (320, 200), bytes(fb).translate(lut)))
    return frames


def main():
    args = sys.argv[1:]
    w32 = None
    if "--w32" in args:
        i = args.index("--w32")
        if i + 1 >= len(args):
            sys.exit(__doc__)
        w32 = args[i + 1]
        del args[i:i + 2]
    if not args:
        sys.exit(__doc__)
    fc = args[0]
    out = args[1] if len(args) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "assets")
    os.makedirs(out, exist_ok=True)

    def src(rel):
        return os.path.join(fc, rel)

    def put(name, frames):
        write_srg(os.path.join(out, name), frames)

    put("title.srg", [picture(src("BEG/SRTITLE.UP"), VIEW43)])
    put("troll.srg", [picture(src("TECHNO/TROLL.UP"), VIEW43)])
    put("endlogo.srg", [picture(src("END/PIC.UH"), VIEW43)])
    put("lens.srg", [picture(src("LENS/LENS.EXB"), VIEW43)])
    # The knight is drawn as a 186-px-wide column (x 70..255) that the scene
    # stretches per line; keep extra vertical resolution for the squash.
    put("jplogo.srg", [picture(src("JPLOGO/ICEKNGDM.UP"), (50, 128), crop=(70, 0, 256, 400))])

    # PAM explosion: 41 frames, streamed from SD at runtime
    pam = [im.crop((0, BAND_Y0, 320, BAND_Y0 + BAND_H)).resize(FULL, Image.BOX)
           for im in pam_frames(src("PAM/OUT.ANI"), src("PAM/PAL.PAL"), 41)]
    put("pam.srg", stretch(pam))

    # Rotozoom texture: 256x256 cut from the lens picture (rows squeezed
    # by 10/11 and centred), halved to 128x128
    lens = to_grey(read_fc(src("LENS/LENS.EXB")))
    tex = Image.new("L", (256, 256))
    for y in range(256):
        a = y * 10 // 11 - 18
        if 0 <= a < 200:
            tex.paste(lens.crop((32, a, 288, a + 1)), (0, y))
    put("roto.srg", [tex.resize((128, 128), Image.BOX)])

    # Alku: landscape (640x200, scrolled) scaled to the screen height,
    # intro font and the text cards
    # landscape row y is drawn at screen row 25 + y: keep the band's rows
    # (0..149), 640 wide -> 256 (0.4 per pixel; the pan is 320 -> 128)
    put("alku_bg.srg", stretch([picture(src("ALKU/HOI.U"), (256, 64), crop=(0, 0, 640, BAND_H))]))
    strip, glyphs = fc_font(src("ALKU/FONA.UH"), src("ALKU/MAIN.C"), 9, 16)
    # 320x400 mode: keep the band's 0.4 across, but blow the letters up to
    # 8 rows for legibility (true scale would be under 4)
    # (the Dolby logo pieces, codes >= 0x80, are redrawn as a picture)
    glyphs = {c: g for c, g in glyphs.items() if c < 0x80}
    write_font(os.path.join(out, "alku.srf"), fit_glyphs(strip, glyphs, 8, FULL[0] / 320, FC_FONT_ROWS), 8, 16,
               FULL[0] / 320)
    open(os.path.join(out, "alku.txt"), "wb").write(alku_cards(src("ALKU/MAIN.C")))
    put("alku_logo.srg", [dolby_logo()])

    # Credits: 21 pictures (160x100, shown 4:3) + text + font
    screens = credit_screens(src("CREDITS/MAIN.C"))
    pics = []
    for pic, _ in screens:
        name = "PIC%02d%s.LBM" % (int(re.sub(r"\D", "", pic)), "B" if pic.endswith("b") else "")
        pics.append(to_grey(read_ilbm(src("CREDITS/PICS/" + name))).resize((40, 30), Image.BOX))
    if w32:
        pics[1] = to_grey(read_fc(os.path.join(w32, "Parts/CREDITS/PIC02.U"))).resize((40, 30), Image.BOX)
    else:
        print("credits.srg: no --w32, using the PARTPIC MISSING placeholder for picture 2")
    # each picture stretched on its own (one is nearly white, which held the
    # rest down when stretched together), mid-tones lifted on dark ones
    pics = [stretch([im])[0] for im in pics]
    pics = [im.point(lambda v: v * (510 - v) // 255) if mean(im) < 80 else im for im in pics]
    put("credits.srg", pics)
    open(os.path.join(out, "credits.txt"), "wb").write(
        b"\n".join(b"\n".join(lines) + b"\n" for _, lines in screens))
    strip, glyphs = credits_font(src("CREDITS/FONA.INC"), src("CREDITS/MAIN.C"), 8)
    # capitals fill rows 0..29 of the 40 (below is stray ink under 2..5),
    # 11 px wide: 7 rows tall plus a blank one, widened a little so M and W
    # keep their middle strokes
    write_font(os.path.join(out, "credits.srf"), fit_glyphs(strip, glyphs, 8, 0.36, [(0, 30, 7)]), 8, 8, 0.36)


    # Glenz: checkerboard (indices + palette) and the two polyhedra
    board, pal = checker(src("GLENZ/FC.UH"))
    put("checker.srg", [board])
    open(os.path.join(out, "checker.pal"), "wb").write(pal)
    open(os.path.join(out, "glenz.bin"), "wb").write(glenz_objects(src("GLENZ/MAIN.C")))

    # Plasma cube: spline coefficients and camera/light track
    open(os.path.join(out, "plzcube.bin"), "wb").write(plz_cube(src("PLZPART")))

    # Coman voxel landscape height maps
    open(os.path.join(out, "coman.bin"), "wb").write(coman_maps(src("COMAN")))

    # U2 engine scenes: objects + palette, and the raw animation streams
    for scene in ("U2A", "U2E"):
        name = scene.lower()
        open(os.path.join(out, name + ".bin"), "wb").write(u2_scene(src("MAIN/DATA"), scene))
        open(os.path.join(out, name + ".anm"), "wb").write(open(src("MAIN/DATA/%s.0AB" % scene), "rb").read())

    # Forest, Water and Lens: the original lookup tables, inverted to
    # per-screen-pixel maps at our resolution
    forest(src("FOREST"), out)
    water(src("WATER"), out)
    lens_maps(src("LENS"), out)

    # End scroller: font and text. The source release only has a work in
    # progress note (and a font without Y and Z); the final ones come from
    # the reconstruction.
    if w32:
        strip, glyphs, text = endscrl_w32(w32)
    else:
        print("endscrl: no --w32, using the source release's draft text")
        strip, glyphs = fc_font(src("ENDSCRL/FONA.UH"), src("ENDSCRL/MAIN.C"), 8, 16)
        text = open(src("ENDSCRL/ENDSCROL.TXT"), "rb").read().replace(b"\r", b"")
    write_font(os.path.join(out, "endscrl.srf"), fit_glyphs(strip, glyphs, 8, 0.36, FC_FONT_ROWS), 8, 16, 0.36)
    open(os.path.join(out, "endscrl.txt"), "wb").write(text)


if __name__ == "__main__":
    main()
