#!/bin/bash
# Build the emulator's kernel image and disk from firmware/. Runs inside the dev container
# (make emulator). Finished steps are skipped; after editing guest/, stubs/ or this file:
#   rm -rf emulator/work/stage && make emulator
# What each step is for: emulator/README.md.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FW="$ROOT/firmware"; EMU="$ROOT/emulator"; WORK="$EMU/work"
KERNEL="$WORK/kernel"; STAGE="$WORK/stage"
[ -f "$FW/flash/NAVIGON/navigon.exe" ] || { echo "firmware/ is empty: run 'make firmware' first" >&2; exit 1; }
mkdir -p "$KERNEL"

echo ">> 1. kernel: generic Windows Embedded Compact 7 for QEMU's vexpress-a15 board"
# Prebuilt by the Milek7/wince-qemu project; pinned by SHA-256.
URL="https://github.com/Milek7/wince-qemu/releases/download/2022-04-17/NK.bin.entry-0x800010c0.zip"
NAME="NK.bin.load-0x800010c0.raw"
if [ ! -f "$KERNEL/NK.raw" ]; then
  curl -fL -o "$KERNEL/image.zip" "$URL"
  echo "3c2f5a0bf904209b8f22e7d395b17fdd0a3b9e6a36f0cdfe93f3f89e03775485  $KERNEL/image.zip" | sha256sum -c --quiet -
  unzip -q -o "$KERNEL/image.zip" "$NAME" -d "$KERNEL"
  mv "$KERNEL/$NAME" "$KERNEL/NK.raw" && rm "$KERNEL/image.zip"
fi
echo "0fef23852d730add79eccf421e7de81336964377089228a49be86c4f754016b9  $KERNEL/NK.raw" | sha256sum -c --quiet -
[ -f "$KERNEL/NK.patched.raw" ] || python3 "$EMU/patches/patch_kernel.py" "$KERNEL/NK.raw" "$KERNEL/NK.patched.raw"
# its system DLLs, to see which functions this Windows offers (step 4)
[ -d "$KERNEL/rom/modules" ]    || python3 "$FW/cerom.py" "$KERNEL/NK.raw" "$KERNEL/rom"

if [ ! -f "$STAGE/.done" ]; then
  echo ">> 2. compile our programs for the guest (guest/) and the stand-in DLLs (stubs/)"
  CC="arm-mingw32ce-gcc -O2 -Wall -s -static -D_WIN32_WCE=0x600 -DUNICODE -D_UNICODE"
  mkdir -p "$WORK/guest" "$WORK/stubs"
  $CC -Wl,--image-base,0x10000 -o "$WORK/guest/launch.exe"  "$EMU/guest/launch.c" -lws2
  $CC -Wl,--image-base,0x10000 -o "$WORK/guest/ceagent.exe" "$EMU/guest/ceagent.c" -ltoolhelp
  $CC -shared -o "$WORK/guest/pl011poll.dll" "$EMU/guest/pl011poll.c"
  for source in "$EMU"/stubs/*.c; do       # VIN=<17 characters> replaces the placeholder in WolfSpecial.c
    $CC -shared ${VIN:+-DVIN="\"$VIN\""} -o "$WORK/stubs/$(basename "$source" .c).dll" "$source"
  done

  echo ">> 3. stage: a copy of firmware/flash to work on"
  # Hard links, so it costs no space. Files in it are only ever replaced, never written to.
  rm -rf "$STAGE"; cp -al "$FW/flash" "$STAGE"
  program_folders() { find "$STAGE" -iname '*.exe' -printf '%h\n' | sort -u; }

  echo ">> 4. redirect imports this Windows cannot satisfy to cestub.dll"
  find "$STAGE" -type f \( -iname '*.exe' -o -iname '*.dll' \) -print0 | xargs -0 \
    python3 "$EMU/patches/redirect_imports.py" --kernel "$KERNEL/rom/modules" --unit "$FW/rom/modules" \
      --stub "$WORK/stubs/cestub.dll" | sed "s|$STAGE/|   |"

  echo ">> 5. system DLLs next to every program"
  # Our stand-ins (emulator/stubs), then the DLLs and fonts from the unit's own system image.
  # On the unit those live in \Windows; here \Windows belongs to the generic kernel.
  # -n: never replace a file the flash disk already has (it can be newer than the ROM's).
  for folder in $(program_folders); do
    cp --remove-destination "$WORK"/stubs/*.dll "$folder/"
    cp -n "$FW"/rom/files/*.[dD][lL][lL] "$FW"/rom/files/*.ttf "$folder/"
  done

  echo ">> 6. settings for a unit whose GPS receiver and car are on the host, and with no graphics accelerator"
  edit() { sed "$2" "$FW/flash/$1" > "$STAGE/$1.new" && mv -f "$STAGE/$1.new" "$STAGE/$1"; }
  # GPS on COM1: (host TCP 5555); Navigon's software map renderer instead of OpenGL ES
  edit NAVIGON/Settings/UserSettings.default \
    's|<GPSPort>2</GPSPort>|<GPSPort>1</GPSPort>|; s|<OldMapDrawer>0</OldMapDrawer>|<OldMapDrawer>1</OldMapDrawer>|'
  # Navigon restarts the unit after 300 s without a GPS fix: make that a day
  edit NAVIGON/Settings/ApplicationSettings.xml \
    's|<NoGpsCheckTime>300</NoGpsCheckTime>|<NoGpsCheckTime>86400</NoGpsCheckTime>|'
  # the car link on COM3: (host TCP 5557, emulator/radio.py); the unit's COM9: does not exist here
  edit VwUserShell/VwCommunicationManager.ini 's|^ComPort=9|ComPort=3|'
  # the boot animation needs the unit's graphics accelerator: here it crashes inside sgalib.dll
  mv "$STAGE/SplashAnimation.exe" "$STAGE/SplashAnimation.exe.off"
  # Pairing needs no setting: the hidden flash (stubs/WolfSpecial.c) says "branded", and the flash
  # disk has no BrandDevice.reg (a file of that name would send the brand app back to waiting
  # for a car). So VwBrandDevice.exe does what it does at every normal boot, and exits.

  echo ">> 7. guest programs: \\My Flash Disk\\0start\\launch.exe and \\My Flash Disk\\dev"
  mkdir -p "$STAGE/0start" "$STAGE/dev"       # "0start" sorts first in Explorer; emulator/emu clicks it
  cp "$WORK/guest/launch.exe" "$STAGE/0start/"
  cp "$WORK/guest/ceagent.exe" "$WORK/guest/pl011poll.dll" "$STAGE/dev/"
  touch "$STAGE/.done"; rm -f "$WORK/disk.img"
fi

if [ ! -f "$WORK/disk.img" ]; then
  echo ">> 8. disk: 4 GB, one FAT32 partition, plus an overlay that takes the guest's writes"
  DISK="$WORK/disk.img.part"; FIRST_SECTOR=2048
  rm -f "$DISK"; truncate -s 4000M "$DISK"
  echo "start=$FIRST_SECTOR, type=c, bootable" | sfdisk --quiet "$DISK"
  mkfs.vfat -F 32 -n WINCEDISK --offset "$FIRST_SECTOR" "$DISK" > /dev/null
  mcopy -s -i "$DISK@@$((FIRST_SECTOR * 512))" "$STAGE"/* ::/
  mv "$DISK" "$WORK/disk.img"
  rm -f "$WORK/disk.qcow2"
  qemu-img create -q -f qcow2 -b "$WORK/disk.img" -F raw "$WORK/disk.qcow2"
fi
echo ">> emulator ready: make run"
