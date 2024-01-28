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
