# 4NSF development kit. The dev container (sdk/) does all the building; QEMU runs on the host.
#
#   make setup              one time: container image, firmware download (3.5 GB), emulator disk
#   make run                start the emulator with the VW software     (HEADLESS=1: no window;
#                           RADIO=1: with a simulated car, GPS=1: with a GPS position)
#   make stop
#   make app    APP=hello   compile apps/hello          -> apps/hello/hello.exe
#   make deploy APP=hello   compile, copy into the running emulator as \My Flash Disk\Apps\hello, start it
#   make shot               screenshot of the emulator   -> emulator/work/shots/screen.png
#                           (taps: emulator/emu click X Y)
#   make shell              a shell inside the dev container
#
# Pieces of `make setup`, runnable on their own:  make venv | firmware | emulator
APP ?= hello
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

emulator:
	$(SDK) env $(if $(VIN),VIN=$(VIN)) emulator/build.sh

run:
	emulator/emu up $(if $(HEADLESS),,--window) $(if $(GPS),--gps) $(if $(RADIO),--radio)

stop:
	emulator/emu stop

app:
	$(SDK) make -C apps/$(APP)

# the program first (it is the one started), then what the VW menu needs to list it
deploy: app
	$(PY) emulator/deploy.py push apps/$(APP)/$(APP).exe $(wildcard apps/$(APP)/NgAppInfo.xml apps/$(APP)/*.png) \
		'\My Flash Disk\Apps\$(APP)' --run

shot:
	emulator/emu shot

shell:
	$(SDK) bash

# removes everything generated except the downloaded firmware
clean:
	rm -rf emulator/work .venv
	find apps -name '*.exe' -delete

.PHONY: setup venv firmware emulator run stop app deploy shot shell clean
