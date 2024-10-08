#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := Faikin
SUFFIX := $(shell components/ESP32-RevK/buildsuffix)

ifeq ($(wildcard /bin/csh),)
$(error	Please install /bin/csh or equivalent)
endif

all:	settings.h
	@echo Make: $(PROJECT_NAME)$(SUFFIX).bin
	@idf.py build
	@cp build/$(PROJECT_NAME).bin $(PROJECT_NAME)$(SUFFIX).bin
	@cp build/bootloader/bootloader.bin $(PROJECT_NAME)$(SUFFIX)-bootloader.bin
	# ESP8266 uses image header version 1, which does not include app descriptor.
	# Here we're complementing this by hijacking data from object file, containing
	# esp_app_desc structure (the SDK still produces it as a part of binary in
	# order to support esp_app_get_description()). We store the data in raw binary
	# form in a separate file, which accompanies the binary.
	@xtensa-lx106-elf-objcopy -Obinary build/esp-idf/app_update/CMakeFiles/__idf_app_update.dir/esp_app_desc.c.obj $(PROJECT_NAME)$(SUFFIX).desc
	@echo Done: $(PROJECT_NAME)$(SUFFIX).bin

beta:
	./issue.sh betarelease

issue:
	./issue.sh release

settings.h:     components/ESP32-RevK/revk_settings settings.def components/ESP32-RevK/settings.def
	components/ESP32-RevK/revk_settings $^

components/ESP32-RevK/revk_settings: components/ESP32-RevK/revk_settings.c
	make -C components/ESP32-RevK revk_settings

flash:
	idf.py flash

monitor:
	idf.py monitor

clean:
	idf.py clean

menuconfig:
	idf.py menuconfig

pull:
	git pull
	git submodule update --recursive

update:
	-git pull
	git submodule update --init --remote
	git submodule update --recursive
	-git commit -a -m "Library update"
	-git push

# Program the FTDI
ftdi: ftdizap/ftdizap
	./ftdizap/ftdizap --serial="RevK" --description="Faikin" --cbus2-mode=17 --self-powered=1

ifeq ($(shell uname),Darwin)
INCLUDES=-I/usr/local/include/
LIBS=-L/usr/local/Cellar/popt/1.18/lib/
else
LIBS=
INCLUDES=
endif

SQLlib/sqllib.o: SQLlib/sqllib.c
	make -C SQLlib
AXL/axl.o: AXL/axl.c
	make -C AXL
AJL/ajl.o: AJL/ajl.c
	make -C AJL


CCOPTS=${SQLINC} -I. -I/usr/local/ssl/include -D_GNU_SOURCE -g -Wall -funsigned-char -lm
OPTS=-L/usr/local/ssl/lib ${SQLLIB} ${CCOPTS}

faikin: faikin.c
	gcc -O -o $@ $< -lpopt ${INCLUDES} ${LIBS}

faikin-s21: faikin-s21.c main/daikin_s21.h main/faikin_enums.h
	gcc -O0 -g -o $@ $< -lpopt ${INCLUDES} ${LIBS}

faikinlog: faikinlog.c SQLlib/sqllib.o AJL/ajl.o main/acextras.m main/acfields.m main/accontrols.m
	cc -O -o $@ $< -lpopt -lmosquitto -ISQLlib SQLlib/sqllib.o -IAJL AJL/ajl.o ${OPTS}

faikingraph: faikingraph.c SQLlib/sqllib.o AXL/axl.o
	cc -O -o $@ $< -lpopt -lmosquitto -ISQLlib SQLlib/sqllib.o -IAXL AXL/axl.o -lcurl ${OPTS}
