# Build from the repo root:  make app APP=hello     (runs this inside the dev container)
CC      = arm-mingw32ce-gcc
CFLAGS  = -O2 -Wall -D_WIN32_WCE=0x600 -DUNICODE -D_UNICODE
# -static: the unit has no GCC runtime DLLs. -s: no debug symbols.
# Subsystem version 6.0 like the unit's own programs (the compiler's default is 3.0).
LDFLAGS = -static -s -Wl,--image-base,0x10000,--major-subsystem-version,6,--minor-subsystem-version,0

hello.exe: hello.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
