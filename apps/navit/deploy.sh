#!/bin/sh
# Extra step of  make deploy APP=navit  (host side, runs before the launcher is copied):
# copies Navit (apps/navit/navit/) to \My Flash Disk\Navit\ and the maps named in MAPS
# (apps/navit/maps/NAME.bin) to \My Flash Disk\Navit\maps\.
#
#   make deploy APP=navit MAPS="italy"
#
# The program and its icons go over only once (the icons take ~2 minutes); the settings every time.
set -eu
cd "$(dirname "$0")/../.."
PY=.venv/bin/python
NAVIT='\My Flash Disk\Navit'
N=apps/navit/navit

# navit.exe last: once it is there, the copy is complete
if ! $PY emulator/deploy.py ls "$NAVIT" 2>/dev/null | grep -q ' navit\.exe$'; then
  $PY emulator/deploy.py push "$N"/icons/* "$NAVIT\\icons" >/dev/null
  $PY emulator/deploy.py push "$N/navit.exe" "$NAVIT"
fi
$PY emulator/deploy.py push "$N/navit.xml" "$N/navit_layout_car.xml" "$N/navit_layout_car_dark.xml" "$NAVIT"

for map in ${MAPS:-}; do
  [ -f "apps/navit/maps/$map.bin" ] || { echo "no apps/navit/maps/$map.bin: make map MAP=$map REGION=..." >&2; exit 1; }
  $PY emulator/deploy.py push "apps/navit/maps/$map.bin" "$NAVIT\\maps"
done
