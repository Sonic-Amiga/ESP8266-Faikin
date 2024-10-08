# ESP8266-Faikin

I am successfully running a great Daikin air conditioning unit in an OpenHAB-powered smart home. Recently i purchased a second one for my newly
remodeled bedroom; and was very "pleased" to see that original BRP online controller is not available for sale in my country any more due to
political reasons. There was, however, a locally developed replacement, which is, of course, not compatible with the original. Additionally it
forces me to use their proprietary cloud; not only potentially disclosing some of my privacy, but also tying my smart home to a working Internet
connection for no reason.

Fortunately i was able not only to find some information on successfully reverse engineered Daikin "S21" communication protocol, but also find
an awesome [ESP32-Faikin](https://github.com/revk/ESP32-Faikin/tree/main/main) project, which does the majority of the thing i need.

My newly purchased controller, named "Daichi" (which of course has nothing to do with the japanese company), is a small board, built around
an [AI Thinker ESP12](https://docs.ai-thinker.com/_media/esp8266/docs/esp-12f_product_specification_en.pdf) module. Therefore this project'sale
goals are:

- Backport ESP32-Faikin to ESP8266 architecture
- Support original HTTP+JSON-based BRP communication protocol, at least enough to support OpenHAB

Since various ESP8266-based hardware is widely accessible, i am making this project opensource. A (partly) reverse-engineered Daichi controller
schematic is available in "Hardware" folder for those who would be interested in implementing their own board based on this or similar ESP8266
module. Since violating someone else's possible design copyrights is not the goal of this project, the schematic is incomplete. It does not
contain exact values for parts (many of these are impossible to determine without complete disassembly), it's only enough for use as a reference,
to understand how the module is interfaced and what pins are in use. There's nothing special to that really; any electronics engineer is able to
put together such a board with minimum efforts.

# Functionality

* Compatible with original BRP series Daikin online controllers; drop-in replacement for use with home automation systems.
* Simple local web based control with live websocket status, easy to save as desktop icon on a mobile phone.
* MQTT reporting and controls
* Includes linux mysql/mariadb based logging and graphing tools
* Works with [EnvMon](https://github.com/revk/ESP32-EnvMon) Environmental Monitor for finer control and status display
* Supports wide variety of units with automatic detection of a protocol being used.
* Supported A/C interfaces: S21, S403, X50A, CN_WIRED (AKA CNW)

** !! WARNING !!! ** The `S403` port is a *non-isolated* `S21` port, i.e. it is at **MAINS POWER** levels, and dangerous.
Any connected device, such as a Faikin, needs to be inside the case. Touch or otherwise fiddling with running hardware,
connected to this port, ** IS POTENTIALLY LETHAL ** !!!

# Supported hardware

This port technically runs on any ESP8266 board. At the moment 4MB and 8 MB flash sizes are supported. Please see
Hardware folder of this repository for more info.

# Conditioner compatibility list

This project has been successfully tested on:

- FTXF20D "Sensira" - S21 protocol
- FTXB25 - S21 protocol
- FTXB35 - S21 protocol
- FTXB50C - CN_WIRED protocol

There's also a list of supported models on
[Original Faikin-ESP32 wiki](https://github.com/revk/ESP32-Faikin/wiki/List-of-confirmed-working-air-con-units);
those are also expected to work.

# Installing

It is possible to build the firmware from source, but for those who don't know how to do it,
prebuilt binaries are available in release/ folder of the repository.

Faikin is built using ESP8266_RTOS_SDK, and installation requires to replace bootloader and
partition table. This is done by flashing all the files using esptool:

esptool.py -p COM3 -b 460800 --after hard_reset write_flash --flash_mode dio --flash_freq 40m --flash_size 8MB 0x8000 partition-table.bin 0xd000 ota_data_initial.bin 0x0 bootloader.bin 0x10000 Faikin-8266.bin

It is enough to do this only once; after you have the proper bootloader, you only need to replace the application (Faikin-8266.bin)

If you built from source, the same result is achieved by simply running "make flash".

# Updates

The firmware defaults to http://ota.revk.uk/ . The author of original ESP32 firmware kindly agreed to
host a 8266 port on his server. Publishing happens with a delay, however, so there's one more update server
at the address http://faikin-ota.home-assistant.my/ . Beta releases for testing will only appear there.
It is possible to change the URL in advanced settings.

The second update server has been kindly provided by GB Network solutions, https://www.gbnetwork.my/ .
Huge thanks for hosting the project!

# Set-up

Appears as access point with simple web page to set up on local WiFI

![WiFi1](Manuals/WiFi1.png)

![WiFi2](Manuals/WiFi2.png)

# Operation

Local interactive web control page using *hostname*.local, no app required, no external internet required.

![WiFi3](Manuals/WiFi3.png)

- [Setup](Manuals/Setup.md) Manual
- [Advanced](Manuals/Advanced.md) Manual

# Building

The build requires [ESP8266 RTOS SDK v3.x](https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/)

Git clone this `--recursive` to get all the submodules, and it should build with just `make`. There are make targets
for other variables, but this hardware is the `make pico` version. The `make` actually runs the normal `idf.py` to build with
then uses cmake. `make menuconfig` can be used to fine tune the settings, but the defaults should be mostly sane.
`make flash` should work to program. Any basic USB-to-5V-TTL-serial adapter should be fine; i used 
[WaveShare PL2303](https://www.waveshare.com/product/pl2303-usb-uart-board-type-a.htm), and of course the full ESP IDF
environment.
