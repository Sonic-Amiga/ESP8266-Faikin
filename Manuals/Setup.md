# Faikin - set-up

The *Faikin-8266* is an alternative firmware for Daichi wi-fi controllers, designed to be a cloud-free
and fully compatible with original Daikin air-con BRP series control WiFi modules.

- Local web control over WiFi, no cloud/account needed, no Internet needed
- MQTT control and reporting
- Integration with Home Assistant using MQTT
- Integration with OpenHAB using original Daikin binding
- Should work with any existing home automation system, having original Daikin support

## Installation

Just connect your reflashed Daichi module to the conditioner and power up.

You may save up on a lead by building your own; Daichi charges substantially for them. You need a mini-USB connector
on one side; and a 5 pin connector to match the plug for the official Daikin WiFi modules on another.
<img src="../hardware/S21_Connector_FXF20D.png">
See included KiCAD schematic in hardware/ folder for details. Note that Daikin loves to play with pinouts, so you may
need to verify yours. You need to locate the GND, Power, Tx, and Rx pins on the S21 connector and connect appropriately.
Be careful with power pin as it typically provides 12-14V !

## LED

Target Daichi module only has a single LED, displaying module status by blinking. It is possible to disable it with an
MQTT setting, e.g. `setting/GuestAC {"blink":[0,0,0]}`.

|State|Meaning|
|----|-----|
|None|Power off|
|Steady|The module is starting up|
|Blinking with short period ~0.4 sec|No wifi config or connection; access point is available|
|Blinking with long period ~0.6 sec|wifi connected successfully|

## WiFi set up

One installed, the LED should light up and blink.

Look for a WiF Access Point called Daikin (or Faikin), e.g.

![WiFiAP](WiFi1.png)

Select this and it should connect, needing no password.

![WiFiAP](WiFi2.png)

On an iPhone this should automatically open a web page. On other devices you may need to check the IP settings and enter the *router IP* in to your browser. The page looks like this.

![WiFi](WiFi3.png)

Enter details and press **Set**. Setting a *Hostname* is a good idea so you can name your air-con. For HomeAssistant you will probably need an MQTT username and password. If not in the UK, please look up the *timezone* string needed in the link provided.

### Hostname

Pick a simple one work hostname to describe your air-con, e.g. GuestAC.

### SSID/Password

Enter the details for your own WiFi. You will note a list of SSIDs that have been seen are shown - you can click on one to set the SSID to save typing it. Make sure you enter the passphrase carefully. If the device is unable to connect the page should show an error and allow you to put in settings again. Only 2.4GHz WiFi is supported, and some special characters in SSID may not be supported.

### MQTT

If using MQTT, which could be Home Assistant running MQTT, enter the hostname or IP address of the MQTT server.

In addition you will usually see the option for an MQTT *username* and *password* - these are usually needed if using Home Assistant MQTT server. You can add a user on Home Assistant and then enter the details here.

## Accessing controls

One set up, the device connects to your WIFi. From the same WiFi you should be able to access from a web browser using the hostanme you have picked followed by `.local`, e.g. `GuestAC.local`.

The controls page shows teh controls for your air-con, and also has a link for *WiFi settings* allowing you to change the WiFi and MQTT settings if needed.

# Software Upgrade

We recommend you upgrade the software when you receive the device, as new features are often added.

Go to the web page, and select *WiFi settings*. You can click on *Upgrade*. This does need internet access.
