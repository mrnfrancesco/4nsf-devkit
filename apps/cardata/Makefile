# Build from the repo root:  make app APP=cardata     (runs this inside the dev container)
# Flags as in apps/hello/Makefile, plus the folder with the headers for the unit's services.
CC      = arm-mingw32ce-gcc
CFLAGS  = -O2 -Wall -D_WIN32_WCE=0x600 -DUNICODE -D_UNICODE -I../include
LDFLAGS = -static -s -Wl,--image-base,0x10000,--major-subsystem-version,6,--minor-subsystem-version,0

cardata.exe: cardata.c ../include/vwshm.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
