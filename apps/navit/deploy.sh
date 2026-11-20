#!/bin/sh
# Extra step of  make deploy APP=navit  (host side, runs before the launcher is copied):
# copies Navit (apps/navit/navit/) to \My Flash Disk\Navit\ and the maps named in MAPS
# (apps/navit/maps/NAME.bin) to \My Flash Disk\Navit\maps\.
#
#   make deploy APP=navit MAPS="italy"
#
# The program, its icons and the speech data go over only once (several minutes); the settings every time.
set -eu
cd "$(dirname "$0")/../.."
PY=.venv/bin/python
NAVIT='\My Flash Disk\Navit'
N=apps/navit/navit

# navit.exe last: once it is there, the copy is complete; again whenever its size changed
remote=$($PY emulator/deploy.py ls "$NAVIT" 2>/dev/null || true)
if ! echo "$remote" | grep -q ' navit\.exe$'; then
  $PY emulator/deploy.py push "$N"/icons/* "$NAVIT\\icons" >/dev/null
fi
if ! echo "$remote" | grep -q "^ *$(wc -c < "$N/navit.exe" | tr -d ' ')  navit\.exe$"; then
  $PY emulator/deploy.py push "$N/navit.exe" "$NAVIT"
fi
$PY emulator/deploy.py push "$N/navitsnd.dll" "$N/navit.xml" "$N/navit_layout_car.xml" "$N/navit_layout_car_dark.xml" "$NAVIT"

# eSpeak's speech data, once (~3 minutes): sub-folders first, phontab last (it marks a complete copy)
E="$NAVIT\\espeak-data"
if ! $PY emulator/deploy.py ls "$E" 2>/dev/null | grep -q ' phontab$'; then
  $PY emulator/deploy.py mkdir "$E" 2>/dev/null || true
  (cd "$N" && find espeak-data -mindepth 1 -type d) | sort | while read -r dir; do
    files=$(find "$N/$dir" -maxdepth 1 -type f)
    remote="$NAVIT\\$(echo "$dir" | tr / '\\')"
    if [ -n "$files" ]; then
      # shellcheck disable=SC2086  # file names in espeak-data have no spaces
      $PY emulator/deploy.py push $files "$remote" >/dev/null
    else
      $PY emulator/deploy.py mkdir "$remote" 2>/dev/null || true
    fi
  done
  $PY emulator/deploy.py push $(find "$N/espeak-data" -maxdepth 1 -type f ! -name phontab) "$E" >/dev/null
  $PY emulator/deploy.py push "$N/espeak-data/phontab" "$E"
fi

for map in ${MAPS:-}; do
  [ -f "apps/navit/maps/$map.bin" ] || { echo "no apps/navit/maps/$map.bin: make map MAP=$map REGION=..." >&2; exit 1; }
  $PY emulator/deploy.py push "apps/navit/maps/$map.bin" "$NAVIT\\maps"
done
