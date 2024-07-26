# Daichi DW22-B

Daichi is a russian domestically designed universal A/C controller. It supports wide variety of
conditioners of various vendors, including Daikin, but it has the following disadvantages:

- Only works with a proprietary phone app over a proprietary cloud using a proprietary protocol
- Does not support any sort of local connection
- Software is slow and of poor quality

This board is the original target for Faikin-8266 port.

<img src="Daichi_PCB.png"><BR>

Original vendor web page: http://aircon-wifi.ru/rac (russian language only)

# Installing Faikin

The board looks like it has a USB port, while in reality it's a serial port, just using a USB-Mini connector.
See the supplied schematics (reverse-engineered) for pinout. In order to flash the board, it needs to be
connected using a usb-to-serial adapter board. [WaveShare module](https://www.waveshare.com/pl2303-usb-uart-board-mini.htm)
works great for me, but anything ot this kind should do the job.
<img src="Serial_Connection.jpg"><BR>
An "UART" switch, located on Daichi board, enables firmware download mode. Move it up then press reset button before running esptool.
