#!/bin/sh
# Copy the rendered music (tools/render_music.py) to the Flipper's SD card,
# where the app's Play All streams it from: /ext/apps_data/second/.
#   tools/upload_music.sh [port]   (default /dev/ttyACM0)
set -e
PORT=${1:-/dev/ttyACM0}
DIR=$(dirname "$0")/../music
PY=$HOME/.ufbt/toolchain/x86_64-linux/bin/python3.11
STORAGE=$HOME/.ufbt/current/scripts/storage.py
"$PY" "$STORAGE" -p "$PORT" mkdir /ext/apps_data/second 2>/dev/null || true
for f in s0a s1 s0b; do
    "$PY" "$STORAGE" -p "$PORT" send "$DIR/$f.pcm" "/ext/apps_data/second/$f.pcm"
done
