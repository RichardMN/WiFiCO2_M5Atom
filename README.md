# WiFiCO2_M5Atom
 
## Motivation

This is intended to allow you to connect an [MH-Z19B CO2 sensor](https://www.winsen-sensor.com/sensors/co2-sensor/mh-z19b.html) to an [M5 atom matrix](https://docs.m5stack.com/en/core/atom_matrix) to get a compact, simple network-enabled CO2 monitor.

 ![Overview photo of M5 Atom wired to MH-Z19B CO2 sensor](/M5%20Atom%20and%20MH-Z19b%20overview%20photo.jpeg)

 ## Hardware

You will need:

- M5 Atom Matrix
- MH-Z19B sensor (it may also work with the MH-Z19C, depending on the underlying software library)
- jumper wires
- soldering equipment, including heat-shrink tubing


### Instructions

The [MH-Z19B datasheet](https://www.winsen-sensor.com/d/files/infrared-gas-sensor/mh-z19b-co2-manual(ver1_7).pdf) shows the pins on the terminal wires which come with the sensor. I cut these wires then soldered them to jumper cables making it possible to plug the sensor into a breadboard - or the back of the M5 Atom Matrix.

Cut the wires from the cables which came with the sensor and connect them to jumper cables. In mine I wired red-red, black-black but had to change some colours so it goes green-blue and blue-yellow.

It should be possible to also connect these wires to a grove connector and so make it possible to plug into the grove connection in the base of the M5.

Plug red and black in to 5V and G on the back of the M5.

Plug blue and yellow into G22 and G19 on the back of the M5.

Connect the USB-C cable and upload the software.

Once the software is loaded onto the M5, you can then unplug the device and power it from a USB power source separate and away from the computer.

 ## Software

 This code depends on the following libraries:

 - [ErriezMHZ19B](https://erriez.github.io/ErriezMHZ19B/index.html)
 - [cppQueue](https://github.com/SMFSW/Queue)

The Arduino IDE should prompt you to help install these libraries, or others which are also required.

 The code is largely a mix of the [M5Atom example code](https://github.com/m5stack/M5Atom) and the [ErriezMHZ19B example code](https://github.com/Erriez/ErriezMHZ19B/tree/master/examples/ErriezMHZ19BGettingStarted). Both of these are released under the MIT license and this license is applied to this work.

 ## Operation

 Having assembled the hardware and uploaded the compiled software to the M5, you will initially need to connect to the M5 on its own wifi network (SSID: `M5STACK_SETUP`) to provide it with wifi credentials for your wifi network. Once this is done, the M5 will reset and provide a website at http://co2monitor.local/ 

 The website and the display will indicate "warming up" for the first three minutes of powered operation. After this they will display 

 ## Features

 - Web interface presents current CO2 concentration (averaged over readings for the past 5 minutes) and graphs of CO2 concentration.
 - LED display indicates either warm up (the MH-Z19 takes three minutes to be ready to give readings) or the current CO2 concentration as scrolling digits.
 - Raw CO2 readings can be downloaded in CSV format for analysis
 
## Known bugs

Issues will be generated for these in GitHub and the list should get shorter.

- The software does not handle cppQueue properly and does not cleanly re-use the data. Behaviour beyond 24 hours of operation is not well-defined.
- Graphs are badly labelled in the time axis.

## Desired features

- Graphs should scale better
- Wifi set-up should better defined
- M5 should use accelerometer to detect "up" and orient the LED display accordingly