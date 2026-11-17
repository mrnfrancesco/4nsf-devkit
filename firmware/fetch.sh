#!/bin/bash
# Download Volkswagen's official "maps + more" update package and unpack it into firmware/.
# Runs inside the dev container (make firmware). Finished steps are skipped.
#
#   firmware/package/  the package as published (installer + archives)
#   firmware/flash/    \My Flash Disk: every application, DLL, config file and map of the unit
#   firmware/os/       boot loader files and the Windows CE 6 system image (NK.nb0)
#   firmware/rom/      NK.nb0 taken apart: modules/ (kernel, drivers, system DLLs) and files/
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FW="$ROOT/firmware"
REGION="${REGION:-Central_Europe}"            # also: South_Europe, North_Europe (only the maps differ)
URL="https://navigation-maps.volkswagen.com/vw-maps/$REGION.zip"
mkdir -p "$FW/package"

if [ ! -f "$FW/package/UpdateMeContent.content" ]; then
  echo ">> downloading $URL (3.5 GB)"
  curl -fL -A "Mozilla/5.0" -C - -o "$FW/package.zip" "$URL"
  unzip -q -o "$FW/package.zip" -d "$FW/package"
  rm "$FW/package.zip"
fi
if [ ! -f "$FW/package/.verified" ]; then
  echo ">> verifying the package against its own checksum list"
  (cd "$FW/package" && tr -d '\r' < ./*.md5 | sed 's/ \*/  /' | md5sum -c --quiet -)
  touch "$FW/package/.verified"
fi

if [ ! -f "$FW/os/NK.nb0" ]; then
  echo ">> unpacking the system image"
  python3 "$FW/sfx_extract.py" "$FW/package/Install_OS.exe" "$FW/os"
fi
if [ ! -f "$FW/flash/NAVIGON/navigon.exe" ]; then
  echo ">> unpacking the flash disk"
  python3 "$FW/sfx_extract.py" "$FW/package/UpdateMeContent.content" "$FW/flash"
  # read-only: the emulator build works on hard links to these files and must never change them
  find "$FW/flash" -type f -exec chmod a-w {} +
fi
if [ ! -d "$FW/rom/modules" ]; then
  echo ">> taking the system image apart"
  python3 "$FW/cerom.py" "$FW/os/NK.nb0" "$FW/rom"
fi
echo ">> firmware ready in $FW"
