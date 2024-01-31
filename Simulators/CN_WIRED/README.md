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

At the moment the simulator works correctly with the controller; but there are problems sending data
according to this description to the real conditioner. So there are some details missing. This is a work in progress.

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
7. After the final space the line goes back to HIGH idle state

There are always a total of 64 data bits, or 8 bytes, in the packet. There is no defined explicit termination sequence.
Bytes are transmitted right-to-left, i. e. least significant bit first.

Since data bits of different values have different times, the signal has no defined bit rate. However it's possible to define
minimum and maximum possible packet durations:

- Minimum (all zeros): 2600 + 1000 + (400 + 300) * 64 = 48400 microseconds
- Maximum (all ones):  2600 + 1000 + (900 + 300) * 64 = 86800 microseconds

The data packet from the conditioner to controller is followed by an addition sequence of an unknown purpose:

1. Delay of 16 milliseconds, the line is in idle state (HIGH)
2. LOW pulse of 2 milliseconds.
3. The line returns to HIGH idle state.

Data packets from the controller to the conditioner do not have this amendment.

## Data packet format

TBD
