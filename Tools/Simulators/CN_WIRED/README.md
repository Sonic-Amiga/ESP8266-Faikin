# CN_WIRED conditioner simulator

This simulator is a development tool, intended for debugging Faikin code in the absense of a compatible
air conditioner. It also aids protocol reverse engineering.

Please note that at the moment CN_WIRED protocol is in the process of reverse engineering. Not all the
details are clear.

# Build requirements

In order to build this code, a Qt framework (https://www.qt.io/product/framework) is required. On Windows
MSYS2/MinGW64 (https://www.msys2.org) environment can be used, it easily provides all the necessary
components in a single environment. Once all the dependencies are installed, build process is very
straightforward. Just normal CMake as usual.

# Connecting

CN_WIRED is not a standard serial port. In order to connect it to a PC running the simulator, a special
bridge is required. The bridge source code is located in "bridge" folder, it can be compiled and installed
using Arduino IDE on any Arduino-compatible board. Default pin assignment is set for Arduino Nano:

GPIO2 - Rx (Tx on controller side)
GPIO4 - Tx (Rx on controller side)

It's possible to modify it for any other board, should that deem necessary. A board with 5V-tolerant I/O is
preferred, however, because the A/C interface uses 5V levels.

# Protocol description

## Notice

This protocol has been reverse engineered from FTXB35C2V1B conditioner model and a 3rd party
["Daichi DW-22"](https://daichi-aircon.com/product/DW22_B/) cloud-based controller (the web site is in russian).

Protocol implementation in the bridge is to be considered a reference; it has been verified to work with the conditioner,
as well as with the Daichi controller using a simulator.

## Physical characteristics.

The CN_WIRED interface uses +5V logic levels. The connector has 4 pins:

- +5V power
- Rx
- Tx
- Ground

TBD: Known A/C pinouts

The interface is proprietary serial, not compatible with RS-232.

The data packet is transmitted as:

1. Idle state of the line is HIGH
2. Sync pulse - LOW level 2600 microseconds
3. Start bit - HIGH level 1000 microseconds (this can be interpreted as data bit "1")
4. Space - LOW level 300 microseconds
5. Data bits are represented as HIGH level pulses. 900 microseconds for "1" and 400 usec for "0".
6. Each data bit is followed by 300 microseconds LOW (space)
7. Pause - HIGH level 16000 microseconds
8. Terminator pulse - LOW level 2000 microseconds
9. The line returns back to HIGH idle state

There are always a total of 64 data bits, or 8 bytes, in the packet. There is no defined explicit termination sequence.
Bytes are transmitted right-to-left, i. e. least significant bit first.

Since data bits of different values have different times, the signal has no defined bit rate. However it's possible to define
minimum and maximum possible packet durations:

- Minimum (all zeros): 2600 + 1000 + (400 + 300) * 64 + 16000 + 2000 = 64600 microseconds
- Maximum (all ones):  2600 + 1000 + (900 + 300) * 64 + 16000 + 2000 = 98400 microseconds

The final terminator pulse of 2000 microseconds appears to be ignored by the conditioner; at least some models are able to
receive and handle packets without it. Daichi controller does not send this pulse to the conditioner; however it rejects
incoming packets from the conditioner if they do not have it. Original Daikin equipment (wired wall panels) send this pulse
both ways, so we consider it mandatory.

Since the Arduino bridge is also an RnD tool, it separately detects these pulses and report them as "_" with the timestamp for
informational purposes.

## Data packet format

### From conditioner to controller 

The air conditioner sends sensor readout approximately every second, with the data having the following format:

 - byte[0] - Current indoors temperature, encoded as BCD
 - byte[1] - unknown
 - byte[2] - unknown
 - byte[3] ... byte[6] - 0 (unused)
 - byte[7] - checksum and packet type indicator:
             - High nibble - 4-bit sum of all other nibbles
			 - Low nibble - packet type = 0

When an original IR remote control is used, conditioner sends  out a "mode changed" packet of the following format:

 - byte[0] - Set point, BCD encoded
 - byte[1] - unknown
 - byte[2] - unknown
 - byte[3] - Mode:
             0x00 - dry
             0x01 - fan
             0x02 - cool
			 0x04 - heat
			 0x08 - auto
			 0x10 - power off flag, ORed with current mode.
 - byte[4] - Fan speed:
             0x08 - speed 1
             0x04 - speed 2
             0x02 - speed 4
             0x01 - speed "auto"
             0x03 - "powerful" aka "turbo" mode
             0x09 - "quiet" mode
 - byte[5] - Special modes. Logically ORed flags:
             bit 7 (mask 0x80) - "LED on" flag
             bit 4 (mask 0x10) - "vertical swing on" flag
			 Value of the remaining bits is unknown.
 - byte[6] - unknown
 - byte[7] - checksum and packet type indicator
             - High nibble - 4-bit sum of all other nibbles
			 - Low nibble - packet type = 1

Example of mode change packet:

 Rx1 80 18 18 00 01 08 0A 10 71 OK

This stands for:

 - byte[0] = 0x18 - Set point 18 C
 - byte[1] = 0x18 - AC seems to always set it equal to byte[1], exact meaning is unknown
 - byte[2] = 0x00 - not used ?
 - byte[3] = 0x01 - power on, "fan" mode
 - byte[4] = 0x08 - "quiet" mode active
 - byte[5] = 0x0A - vertical swing off (0x10 bitmask), the rest is unknown
 - byte[6] = 0x10 - unknown
 - byte[7] = 0x71 - 7 is a 4-bit sum of all nibbles; '1' in the low nibble signals "mode changed" packet.

The conditioner is known to send "mode changed" packets three times in a row, perhaps for better reliability.
There is no ACK/NAK signalling in this protocol.

### From controller to conditioner

Packets from the controller to the conditioner only specify operation modes. Their format is identical to
"mode change" packet above, but with type set to 0. If type is set to any other value, the conditioner ignores the packet!

Daichi controller is also known to send its control packets 4 times in a row when an action is issued by the user.

There is no way to query the conditioner about its current operation modes. The controller is supposed to track the
current state internally, based on own controls and "mode changed" nofitications from the AC.
