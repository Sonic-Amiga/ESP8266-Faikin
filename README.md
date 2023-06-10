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

Since various ESP8266-based hardware is widely accessible, am making this project opensource. A (partly) reverse-engineered Daichi controller
schematic is available in "Hardware" folder for those who would be interested in implementing their own board based on this or similar ESP8266
module. Since violating someone else's possible design copyrights is not the goal of this project, the schematic is incomplete. It does not
contain exact values for parts (many of these are impossible to determine without complete disassembly), it's only enough for use as a referense,
to understand how the module is interfaced and what pins are in use. There's nothing special to that really; any electronics engineer is able to
put together such a board with minimum efforts.

At the moment of writing this project doesn't work yet; it is a work in progress.

# Set-up

Appears as access point with simple web page to set up on local WiFI

![WiFi1](Manuals/WiFi1.png)

![WiFi2](Manuals/WiFi2.png)

# Operation

Local interactive web control page using *hostname*.local, no app required, no external internet required.

![WiFi3](Manuals/WiFi3.png)

- [Setup](Manuals/Setup.md) Manual
- [Advanced](Manuals/Advanced.md) Manual

# Design

* KiCad PCB designs included
* 3D printed case STL files
* Documentation of reverse engineered protocol included

Basically, Daikin have gone all cloudy with the latest WiFi controllers. This module is designed to provide an alternative.

* Simple local web based control with live websocket status, easy to save as desktop icon on a mobile phone.
* MQTT reporting and controls
* Includes linux mysql/mariadb based logging and graphing tools
* Works with [EnvMon](https://github.com/revk/ESP32-EnvMon) Environmental Monitor for finer control and status display
* Automatically works out if S21 or alternative protocol used on ducted units
* Backwards compatible `/aircon/get_control_info` and `/aircon/set_control_info` URLs (work in progress)

# Building

The build requires [ESP8266 RTOS SDK v3.x](https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/)

Git clone this `--recursive` to get all the submodules, and it should build with just `make`. There are make targets for other variables, but this hardware is the `make pico` version. The `make` actually runs the normal `idf.py` to build with then uses cmake. `make menuconfig` can be used to fine tune the settings, but the defaults should be mostly sane. `make flash` should work to program. You will need a programming lead, e.g. [Tazmotizer](https://github.com/revk/Shelly-Tasmotizer-PCB) or similar, and of course the full ESP IDF environment.

If you want to purchase an assembled PCB, see [A&A circuit boards](https://www.aa.net.uk/etc/circuit-boards/)

The wiring from the existing wifi modules fits directly (albeit only 4 pins used).

![272012](https://user-images.githubusercontent.com/996983/169694456-bd870348-f9bf-4c31-a2e3-00da13320ffc.jpg)
