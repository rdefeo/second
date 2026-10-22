# Second Reality for Flipper Zero

A recreation of Future Crew's 1993 demo *Second Reality* on the Flipper Zero's 128x64 LCD, with temporal grayscale and the original music streamed to the speaker. Recall that the demo starts with a dark screen as the music slowly fades in - be patient. Requires firmware/SDK 1.5.1 or newer (API 88.2+).

## Controls

- Menu: **Play Demo**, **Display Setup**, **About**, or any of the 25 scenes.
- In a scene: **Back** = menu, **Left/Right** = previous/next scene, **OK** = pause.
- Play Demo only: **Up/Down** = music volume (remembered).
- Display Setup: Up/Down tune the panel refresh rate (hold for 1 Hz steps),
  Left/Right pick 1-bit dither or 3–9 gray levels. Back saves.

Play Demo is silent in stealth mode (Lock menu on the desktop: press Up).

## Building

Everything is generated from Future Crew's source release
(github.com/mtuomi/SecondReality, Unlicense):

```sh
python3 tools/convert.py  /path/to/SecondReality --w32 /path/to/SecondRealityW32   # assets/  (pictures, fonts, 3D, tables)
python3 tools/timeline.py /path/to/SecondReality   # src/timeline.h (music sync points)
python3 tools/render_music.py /path/to/SecondReality  # music/*.pcm (needs libopenmpt, numpy)
ufbt launch                                        # build, install, run
tools/upload_music.sh                              # music -> /ext/apps_data/second/
```

`--w32` is a checkout of Conspiracy's reconstruction
(github.com/ConspiracyHu/SecondRealityW32, Unlicense). It supplies what the
source release lacks: the second credits picture (the U2A ships, a "PARTPIC
MISSING" placeholder there) and the final end scroller text and font (the
release has a draft note and a font without Y and Z).

The release's S3M files are scrambled; `tools/fcmusic.py` unscrambles them.
Music shaping for the piezo (high-pass, compression) is in `shape()` in
`tools/render_music.py`.

## Desktop preview

```sh
make -C host preview SCENES=7        # host/out/s07.gif
make -C host preview SCENES=all S=10
host/build/sr_host -t assets         # print the Play All timeline
```

## Layout

- `src/display/` – grayscale display (subframe PWM, Bayer-offset phases)
- `src/gfx/` – frame, fills, images, fonts, 3D helpers
- `src/scenes/` – one file per scene; `u2.c` is the U2 polygon engine
- `src/runner.c` – menu runner and the Play All clock; `src/audio.c` – PCM to speaker
- `tools/` – converters; `host/` – preview harness

Scene code is an independent implementation; the JS port
(covalichou/second-reality-js) was used only as a reference.

## Challenges and changes

This should be obvious, but this app is trying to cram 10kg of stuff into a 5kg bag. The original demo relies on a larger resolution screen and 256 colors

- The original was designed for a 4x3 screen while the Flipperzero is 2x1. Any scene that was letterboxed in the dmeo have been expanded to use the full LCD.
- Some scenes were brightened as the originals were too dark
- Rendered text is almost undecipherable
