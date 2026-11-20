# 4NSF development kit. The dev container (sdk/) does all the building; QEMU runs on the host.
#
#   make setup              one time: container image, firmware download (3.5 GB), emulator disk
#   make run                start the emulator with the VW software     (HEADLESS=1: no window;
#                           RADIO=1: with a simulated car, GPS=1: with a GPS position)
#   make stop
#   make app    APP=hello   compile apps/hello          -> apps/hello/hello.exe
#   make deploy APP=hello   compile, copy into the running emulator as \My Flash Disk\Apps\hello, start it
#   make map                build a Navit map from OpenStreetMap -> apps/navit/maps/italy.bin
#                           (MAP=name REGION=geofabrik/path [BBOX=left,bottom,right,top] [FILTER=addresses|full]; docs/apps/navit.md)
#   make shot               screenshot of the emulator   -> emulator/work/shots/screen.png
#                           (taps: emulator/emu click X Y)
#   make shell              a shell inside the dev container
#
# Pieces of `make setup`, runnable on their own:  make venv | firmware | emulator
APP ?= hello
MAP ?= italy
REGION ?= europe/italy
SDK := sdk/run
PY  := .venv/bin/python

setup: venv firmware emulator

# host-side Python tools live in a virtual environment, nothing is installed system-wide
venv: .venv/.ok
.venv/.ok: requirements.txt
	python3 -m venv .venv
	.venv/bin/pip install --quiet --disable-pip-version-check -r requirements.txt
	touch $@

firmware:
	$(SDK) firmware/fetch.sh

# MAPS= rewrites the disk: never under a running emulator (checked here, on the host, where QEMU runs)
emulator:
	@[ -z "$(MAPS)" ] || ! emulator/emu status | grep -q running || { echo "the emulator is running: make stop first" >&2; exit 1; }
	$(SDK) env $(if $(VIN),VIN=$(VIN)) $(if $(MAPS),MAPS="$(MAPS)") emulator/build.sh

run:
	emulator/emu up $(if $(HEADLESS),,--window) $(if $(GPS),--gps) $(if $(RADIO),--radio)

stop:
	emulator/emu stop

app:
	$(SDK) make -C apps/$(APP)

# the program first (it is the one started), then what the VW menu needs to list it
deploy: app
	@[ ! -x apps/$(APP)/deploy.sh ] || MAPS="$(MAPS)" apps/$(APP)/deploy.sh
	$(PY) emulator/deploy.py push apps/$(APP)/$(APP).exe $(wildcard apps/$(APP)/NgAppInfo.xml apps/$(APP)/*.png) \
		'\My Flash Disk\Apps\$(APP)' --run

# a whole country needs more than the container's default memory
map:
	MEMORY=$(or $(MEMORY),8g) $(SDK) env FILTER=$(or $(FILTER),roads) apps/navit/build_map.sh $(MAP) $(REGION) $(BBOX)

shot:
	emulator/emu shot

shell:
	$(SDK) bash

# removes everything generated except the downloaded firmware
clean:
	rm -rf emulator/work .venv
	find apps -name '*.exe' -delete

.PHONY: setup venv firmware emulator run stop app deploy map shot shell clean
