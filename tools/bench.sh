#!/bin/sh
# usage: tools/bench.sh [label]   - speed benchmark + pixel-exact golden check
#
# tests/bench.txt times four frozen camera positions; tests/golden.txt renders
# six and they are compared against tests/golden.md5.  Both freeze the world
# (freezeLogic) and poke the camera, so they measure the renderer alone.
# Regenerate the reference only for an *intended* pixel change:
#     $HOME/.devkitsms/bin/smstest catacombs.sms tests/golden.txt
#     md5sum shots/g?_*.ppm > tests/golden.md5
cd "$(dirname "$0")/.."
SMSTEST=${SMSTEST:-$HOME/.devkitsms/bin/smstest}
echo "--- $1 ---"
$SMSTEST catacombs.sms tests/bench.txt | awk '/^== scene/{s=$0} /print: vblankCounter/{printf "%-34s %4d video frames / 10 renders\n", s, $4}'
$SMSTEST catacombs.sms tests/golden.txt >/dev/null || echo "GOLDEN SCRIPT FAILED"
if md5sum -c --quiet tests/golden.md5 2>/dev/null; then echo "golden: pixel-identical"; else echo "GOLDEN: PIXELS CHANGED"; md5sum -c tests/golden.md5 2>&1 | grep -v OK; fi
