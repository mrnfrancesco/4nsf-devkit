# Build from the repo root:  make app APP=appcom     (runs this inside the dev container)
# Flags as in apps/hello/Makefile; -lws2 is the socket library.
CC      = arm-mingw32ce-gcc
CFLAGS  = -O2 -Wall -D_WIN32_WCE=0x600 -DUNICODE -D_UNICODE
LDFLAGS = -static -s -Wl,--image-base,0x10000,--major-subsystem-version,6,--minor-subsystem-version,0

appcom.exe: appcom.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -lws2
