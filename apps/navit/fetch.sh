#!/bin/sh
# Download Navit for Windows CE (pinned release, SHA-256 checked) and prepare what goes to
# \My Flash Disk\Navit\ on the unit: apps/navit/navit/. Runs inside the dev container
# (make app APP=navit). Navit is GPL-2.0: https://github.com/navit-gps/navit (source: same release).
set -eu
VERSION=0.5.7
URL=https://github.com/navit-gps/navit/releases/download/v$VERSION/navit-wince-v$VERSION.zip
SHA256=2f5f38d4ff4d8e1d2f3a390b4444f1bc5af469dba7950756ce8b0d9e568d2653
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$HERE/navit
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo ">> $URL"
curl -fsSL -o "$TMP/navit.zip" "$URL"
echo "$SHA256  $TMP/navit.zip" | sha256sum -c --quiet
rm -rf "$OUT"
mkdir -p "$OUT"
# the program, its settings, two map styles (day / night) and the icons; no voices, no translations
unzip -q "$TMP/navit.zip" navit.exe navit.xml navit_layout_car.xml navit_layout_car_dark.xml 'icons/*' \
  -x 'icons/desktop_icons/*' icons/Makefile icons/cmake_install.cmake -d "$OUT"
patch -s "$OUT/navit.xml" "$HERE/navit.xml.diff"
echo ">> $OUT ready"
