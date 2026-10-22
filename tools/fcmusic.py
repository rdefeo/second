"""Second Reality's music: unscramble Future Crew's protected S3M files and
work out when each (order, row) position plays.

The modules in the source release (MAIN/MUSIC0.S3M, MUSIC1.S3M, and the
copies inside REALITY.FC) have their pattern data XOR-scrambled: byte i of
each packed pattern (i >= 2, after its length word) is XORed with
((i << 2) ^ i) & 0xff. Sample data is untouched.
"""
import struct

SKIP, END = 254, 255
END_KEY = (1 << 16, 0)  # sorts after every real (order, row)


def unscramble(data):
    d = bytearray(data)
    ordnum, insnum, patnum = struct.unpack_from("<3H", d, 0x20)
    pp = 0x60 + ordnum + insnum * 2
    for ptr in struct.unpack_from("<%dH" % patnum, d, pp):
        if ptr:
            off = ptr * 16
            (ln,) = struct.unpack_from("<H", d, off)
            for i in range(2, ln):
                d[off + i] ^= ((i << 2) ^ i) & 0xFF
    return bytes(d)


def parse(data):
    """Orders, speed, tempo and per-pattern row effects [(cmd, info)...]."""
    ordnum, insnum, patnum = struct.unpack_from("<3H", data, 0x20)
    orders = list(data[0x60:0x60 + ordnum])
    pp = 0x60 + ordnum + insnum * 2
    pats = []
    for ptr in struct.unpack_from("<%dH" % patnum, data, pp):
        rows = [[] for _ in range(64)]
        if ptr:
            off = ptr * 16
            (ln,) = struct.unpack_from("<H", data, off)
            q, r, end = off + 2, 0, off + ln
            while r < 64 and q < end:
                b = data[q]
                q += 1
                if not b:
                    r += 1
                    continue
                if b & 32:
                    q += 2
                if b & 64:
                    q += 1
                if b & 128:
                    rows[r].append((data[q], data[q + 1]))
                    q += 2
        pats.append(rows)
    return dict(orders=orders, pats=pats, speed=data[0x31], tempo=data[0x32])


def row_times(mod, start_order=0, tempo=None, limit_ms=15 * 60 * 1000):
    """Play the song from start_order and return {(order, row): ms} for the
    first time each position starts, honouring Axx (speed), Txx (tempo),
    Bxx (jump), Cxx (break), SBx (loop), SEx (row delay), skip/end markers."""
    speed, tempo = mod["speed"], tempo or mod["tempo"]
    orders, pats = mod["orders"], mod["pats"]
    t, times = 0.0, {}
    o, r = start_order, 0
    loop_row, loop_count = 0, 0
    while o < len(orders) and t < limit_ms:
        pat = orders[o]
        if pat == SKIP:
            o, r = o + 1, 0
            continue
        if pat == END or pat >= len(pats):
            break
        times.setdefault((o, r), t)
        jump = brk = None
        delay = 0
        for cmd, info in pats[pat][r]:
            c = chr(64 + cmd) if 1 <= cmd <= 26 else ""
            if c == "A" and info:
                speed = info
            elif c == "T" and info >= 32:
                tempo = info
            elif c == "B":
                jump = info
            elif c == "C":
                brk = (info >> 4) * 10 + (info & 15)
            elif c == "S":
                x, y = info >> 4, info & 15
                if x == 0xE:
                    delay = y
                elif x == 0xB:
                    if y == 0:
                        loop_row = r
                    elif loop_count == 0:
                        loop_count, jump = y, ("loop",)
                    else:
                        loop_count -= 1
                        if loop_count:
                            jump = ("loop",)
        t += speed * (1 + delay) * 2500.0 / tempo
        if jump == ("loop",):
            r = loop_row
        elif jump is not None:
            o, r = jump, brk or 0
        elif brk is not None:
            o, r = o + 1, brk
        else:
            r += 1
            if r >= 64:
                o, r = o + 1, 0
    times[END_KEY] = t  # when the song ran out (end marker or last order)
    return times
