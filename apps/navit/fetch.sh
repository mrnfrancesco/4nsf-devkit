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
# the program, its settings, two map styles (day / night), the icons and the speech data of the
# built-in eSpeak (espeak-data, 2.8 MB, all languages); no translations: this build's menus stay English
unzip -q "$TMP/navit.zip" navit.exe navit.xml navit_layout_car.xml navit_layout_car_dark.xml 'icons/*' 'espeak-data/*' \
  -x 'icons/desktop_icons/*' icons/Makefile icons/cmake_install.cmake -d "$OUT"
patch -s "$OUT/navit.xml" "$HERE/navit.xml.diff"
# the release build carries 7 MB of debug information: drop it (faster to copy and to load), which
# also frees the room in the header that the next step needs
arm-mingw32ce-strip --strip-debug "$OUT/navit.exe"
# speech on the car's speakers: Navit's waveOutWrite from navitsnd.dll (docs/apps/navit.md)
cp "$HERE/navitsnd.dll" "$OUT/"
python3 "$HERE/../../emulator/patches/redirect_imports.py" --only waveOutWrite --stub "$OUT/navitsnd.dll" "$OUT/navit.exe"
echo ">> $OUT ready"
