#!/bin/sh
# Extra step of  make deploy APP=navit  (host side, runs before the launcher is copied):
# copies Navit (apps/navit/navit/) to \My Flash Disk\Navit\ and the maps named in MAPS
# (apps/navit/maps/NAME.bin) to \My Flash Disk\Navit\maps\.
#
#   make deploy APP=navit MAPS="italy"
#
# The program, its icons and the speech data go over only once (several minutes); the
# settings every time.
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

# copy_tree DIR MARKER: the folder tree DIR of Navit's (no spaces in its names) once, sub-folders
# first; the file MARKER (a path inside DIR) goes last and marks a complete copy
copy_tree() {
  marker_dir=$(dirname "$2"); marker=$(basename "$2")
  if $PY emulator/deploy.py ls "$NAVIT\\$(echo "$marker_dir" | tr / '\\')" 2>/dev/null | grep -q " $marker\$"; then
    return
  fi
  echo ">> $1 (once, a few minutes)"
  $PY emulator/deploy.py mkdir "$NAVIT\\$1" 2>/dev/null || true
  (cd "$N" && find "$1" -type d) | sort | while read -r dir; do
    remote="$NAVIT\\$(echo "$dir" | tr / '\\')"
    files=$(find "$N/$dir" -maxdepth 1 -type f ! -path "$N/$2")
    [ "$dir" = "$1" ] || $PY emulator/deploy.py mkdir "$remote" 2>/dev/null || true
    # shellcheck disable=SC2086  # no spaces in these names
    [ -z "$files" ] || $PY emulator/deploy.py push $files "$remote" >/dev/null
  done
  $PY emulator/deploy.py push "$N/$2" "$NAVIT\\$(echo "$marker_dir" | tr / '\\')" >/dev/null
}
copy_tree espeak-data espeak-data/phontab       # eSpeak's speech data

for map in ${MAPS:-}; do
  [ -f "apps/navit/maps/$map.bin" ] || { echo "no apps/navit/maps/$map.bin: make map MAP=$map REGION=..." >&2; exit 1; }
  $PY emulator/deploy.py push "apps/navit/maps/$map.bin" "$NAVIT\\maps"
done
