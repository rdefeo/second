#!/usr/bin/env python3
"""Render Second Reality's music for the Flipper's speaker.

    python3 tools/render_music.py /path/to/SecondReality [out_dir]

Uses libopenmpt (system library) on Future Crew's own modules, unscrambled
by fcmusic.py. Writes three raw streams (unsigned 8-bit mono, RATE Hz) to
out_dir (default: music/), one per music segment of the demo:

  s0a.pcm  music0 from the start (tempo 120, as STARTMUS.C sets it)
  s1.pcm   music1 from the start
  s0b.pcm  music0's second half (order 18 in Future Crew's file)

Copy them to /ext/apps_data/second/ on the SD card (tools/upload_music.sh).
The signal is shaped for a small piezo: bass removed, compressed, soft
limited, so it stays loud and clear at 8 bits.
"""
import ctypes
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fcmusic  # noqa: E402

RATE = 16000
MUSIC0_RESUME_ORDER = 18  # sync-table order 14, see timeline.py

lib = ctypes.CDLL("libopenmpt.so.0")
lib.openmpt_module_create_from_memory2.restype = ctypes.c_void_p
lib.openmpt_module_create_from_memory2.argtypes = [
    ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
    ctypes.c_void_p, ctypes.POINTER(ctypes.c_int), ctypes.c_void_p, ctypes.c_void_p]
lib.openmpt_module_set_repeat_count.argtypes = [ctypes.c_void_p, ctypes.c_int32]
lib.openmpt_module_set_position_order_row.restype = ctypes.c_double
lib.openmpt_module_set_position_order_row.argtypes = [ctypes.c_void_p, ctypes.c_int32, ctypes.c_int32]
lib.openmpt_module_set_render_param.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int32]
lib.openmpt_module_read_mono.restype = ctypes.c_size_t
lib.openmpt_module_read_mono.argtypes = [ctypes.c_void_p, ctypes.c_int32, ctypes.c_size_t, ctypes.c_void_p]
lib.openmpt_module_ctl_set_text.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]
lib.openmpt_module_destroy.argtypes = [ctypes.c_void_p]

RENDER_INTERPOLATIONFILTER_LENGTH = 3


def render(data, order=0, repeat=0):
    buf = ctypes.create_string_buffer(data, len(data))
    err = ctypes.c_int(0)
    mod = lib.openmpt_module_create_from_memory2(buf, len(data), None, None, None, None,
                                                 ctypes.byref(err), None, None)
    if not mod:
        raise RuntimeError("libopenmpt could not load module (error %d)" % err.value)
    lib.openmpt_module_set_repeat_count(mod, repeat)
    lib.openmpt_module_ctl_set_text(mod, b"play.at_end", b"stop")
    lib.openmpt_module_set_render_param(mod, RENDER_INTERPOLATIONFILTER_LENGTH, 8)
    if order:
        lib.openmpt_module_set_position_order_row(mod, order, 0)
    chunks, block = [], np.zeros(RATE, dtype=np.int16)
    while True:
        n = lib.openmpt_module_read_mono(mod, RATE, len(block), block.ctypes.data)
        if not n:
            break
        chunks.append(block[:n].astype(np.float32) / 32768)
    lib.openmpt_module_destroy(mod)
    return np.concatenate(chunks)


def loop_start(once, twice):
    """music0 ends with a jump back (to order 33) that libopenmpt treats as
    the end. `twice` has one repeat: find where in `once` the repeat picks
    up, the sample the player should go on from at the end (S0B_LOOP in
    src/runner.c). Patterns recur in the song, so match a long stretch,
    at every offset (squared error via FFT cross-correlation)."""
    again = twice[len(once):][:RATE * 30].astype(np.float64)
    x = once.astype(np.float64)
    n = len(x) + len(again)
    corr = np.fft.irfft(np.fft.rfft(x, n) * np.conj(np.fft.rfft(again, n)), n)[:len(x) - len(again) + 1]
    energy = np.concatenate(([0], np.cumsum(x * x)))
    err = energy[len(again):] - energy[:-len(again)] - 2 * corr + np.sum(again * again)
    return int(np.argmin(err))


def loop_splice(once, twice):
    """Where to end the looping file and where to go on from. Right after
    the jump the repeat still has the tails of the notes before it, so it
    differs from the first time through: run on into the repeat until the
    two agree (for longer than shape()'s filters reach), and cut there.
    Returns (file length, loop sample), both in `twice`."""
    start = loop_start(once, twice)
    n = len(once)
    d = np.abs(twice[n:n + RATE * 10] - once[start:start + RATE * 10])
    reach = RATE // 20 + RATE // 300  # envelope and high-pass windows
    for k in range(reach, len(d) - reach):
        if d[k - reach:k + reach].max() < 1e-3:
            return n + k, start + k
    raise RuntimeError("the repeat never settles into the first pass")


def shape(x):
    """Piezo-friendly: high-pass (~300 Hz), compress, soft limit, 8-bit."""
    # high-pass: subtract a moving-average low-pass (first null ~300 Hz)
    box = RATE // 300
    y = x - np.convolve(x, np.ones(box) / box, mode="same")
    # slow envelope follower -> gain that evens out loud and quiet parts
    env = np.abs(y)
    win = RATE // 20
    env = np.convolve(env, np.ones(win) / win, mode="same") + 1e-4
    gain = np.clip(0.25 / env, 0.5, 6.0) ** 0.6
    z = np.tanh(y * gain * 2.5)
    return np.clip(np.round(128 + z * 120), 0, 255).astype(np.uint8)


def main():
    fc = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "music")
    os.makedirs(out, exist_ok=True)
    m0 = bytearray(fcmusic.unscramble(open(os.path.join(fc, "MAIN/MUSIC0.S3M"), "rb").read()))
    m0[0x32] = 0x78  # tempo 120, as the demo's music loader sets it
    m1 = fcmusic.unscramble(open(os.path.join(fc, "MAIN/MUSIC1.S3M"), "rb").read())
    for name, data, order in (("s0a", bytes(m0), 0), ("s1", m1, 0), ("s0b", bytes(m0), MUSIC0_RESUME_ORDER)):
        raw = render(data, order)
        if name == "s0b":
            # it loops: end it a little into the repeat, where the repeat
            # matches the first time through, so the join doesn't click
            twice = render(data, order, repeat=1)
            end, loop = loop_splice(raw, twice)
            pcm = shape(twice)[:end]
        else:
            pcm = shape(raw)
        open(os.path.join(out, name + ".pcm"), "wb").write(pcm.tobytes())
        print("%s.pcm: %.1f s, %d bytes" % (name, len(pcm) / RATE, len(pcm)))
        if name == "s0b":
            print("s0b.pcm: loops back to sample %d (S0B_LOOP in src/runner.c)" % loop)


if __name__ == "__main__":
    main()
