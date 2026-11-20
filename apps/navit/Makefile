# Build from the repo root:  make app APP=navit     (runs this inside the dev container)
# navit.exe = the launcher (map picker); navit/ = Navit itself, downloaded and configured by fetch.sh
# -lws2 is the socket library (the launcher listens for the menu's AppCom message 31).
CC      = arm-mingw32ce-gcc
CFLAGS  = -O2 -Wall -D_WIN32_WCE=0x600 -DUNICODE -D_UNICODE
# -static: the unit has no GCC runtime DLLs. -s: no debug symbols.
# Subsystem version 6.0 like the unit's own programs (the compiler's default is 3.0).
LDFLAGS = -static -s -Wl,--image-base,0x10000,--major-subsystem-version,6,--minor-subsystem-version,0

all: navit.exe navit/navit.exe

navit.exe: navit.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -lws2

# Navit's own waveOutWrite: announces its speech to the VW menu (navitsnd.c); fetch.sh puts it in navit/
navitsnd.dll: navitsnd.c navitsnd.def
	$(CC) $(CFLAGS) -shared -static -s -o $@ navitsnd.c navitsnd.def -lws2

navit/navit.exe: fetch.sh navit.xml.diff navitsnd.dll
	./fetch.sh
