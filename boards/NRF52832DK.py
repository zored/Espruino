#!/bin/false
# This file is part of Espruino, a JavaScript interpreter for Microcontrollers
#
# Copyright (C) 2013 Gordon Williams <gw@pur3.co.uk>
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# ----------------------------------------------------------------------------------------
# This file contains information for a specific board - the available pins, and where LEDs,
# Buttons, and other in-built peripherals are. It is used to build documentation as well
# as various source and header files for Espruino.
# ----------------------------------------------------------------------------------------

import pinutils;

info = {
 'name' : "nRF52 Development Kit",
 'link' :  [ "https://www.nordicsemi.com/eng/Products/Bluetooth-low-energy/nRF52-DK" ],
 'espruino_page_link' : 'nRF52832DK',
  # This is the PCA10036
 'default_console' : "EV_SERIAL1",
 'default_console_tx' : "D6",
 'default_console_rx' : "D8",
 'default_console_baudrate' : "9600",
 'variables' : 2250, # How many variables are allocated for Espruino to use. RAM will be overflowed if this number is too high and code won't compile.
# 'bootloader' : 1,
 'binary_name' : 'espruino_%v_nrf52832.hex',
 'build' : {
   'optimizeflags' : '-Os',
   'libraries' : [
     'BLUETOOTH',
     'NET',
     'GRAPHICS',
     'NFC',
     'NEOPIXEL'
   ],
   'makefile' : [
     'DEFINES+=-DCONFIG_GPIO_AS_PINRESET', # Allow the reset pin to work
     'DEFINES += -DBOARD_PCA10040 -DPCA10040'
   ]
 }
};


chip = {
  'part' : "NRF52832",
  'family' : "NRF52",
  'package' : "QFN48",
  'ram' : 64,
  'flash' : 512,
  'speed' : 64,
  'usart' : 1,
  'spi' : 1,
  'i2c' : 1,
  'adc' : 1,
  'dac' : 0,
  'saved_code' : {
    'address' : ((118 - 10) * 4096), # Bootloader takes pages 120-127, FS takes 118-119
    'page_size' : 4096,
    'pages' : 10,
    'flash_available' : 512 - ((31 + 8 + 2 + 10)*4) # Softdevice uses 31 pages of flash, bootloader 8, FS 2, code 10. Each page is 4 kb.
  },
};

devices = {
  'BTN1' : { 'pin' : 'D13', 'pinstate' : 'IN_PULLDOWN' }, # Pin negated in software
  'BTN2' : { 'pin' : 'D14', 'pinstate' : 'IN_PULLDOWN' }, # Pin negated in software
  'BTN3' : { 'pin' : 'D15', 'pinstate' : 'IN_PULLDOWN' }, # Pin negated in software
  'BTN4' : { 'pin' : 'D16', 'pinstate' : 'IN_PULLDOWN' }, # Pin negated in software
  'LED1' : { 'pin' : 'D17' }, # Pin negated in software
  'LED2' : { 'pin' : 'D18' }, # Pin negated in software
  'LED3' : { 'pin' : 'D19' }, # Pin negated in software
  'LED4' : { 'pin' : 'D20' }, # Pin negated in software
  'RX_PIN_NUMBER' : { 'pin' : 'D8'},
  'TX_PIN_NUMBER' : { 'pin' : 'D6'},
  'CTS_PIN_NUMBER' : { 'pin' : 'D7'},
  'RTS_PIN_NUMBER' : { 'pin' : 'D5'},
  # Pin D22 is used for clock when driving neopixels - as not specifying a pin seems to break things
};

# left-right, or top-bottom order
board = {
  'left' : [ 'VDD', 'VDD', 'RESET', 'VDD','5V','GND','GND','','','D3','D4','D28','D29','D30','D31'],
  'right' : [
     'D27', 'D26', 'D2', 'GND', 'D25','D24','D23', 'D22','D20','D19','',
     'D18','D17','D16','D15','D14','D13','D12','D11','',
     'D10','D9','D8','D7','D6','D5','D21','D1','D0'],
  '_notes' : {
    'D6' : "Serial console RX",
    'D8' : "Serial console TX"
  }
};
board["_css"] = """
#board {
  width: 528px;
  height: 800px;
  top: 0px;
  left : 200px;
  background-image: url(img/NRF52832DK.jpg);
}
#boardcontainer {
  height: 900px;
}

#left {
    top: 219px;
    right: 466px;
}
#right {
    top: 150px;
    left: 466px;
}

.leftpin { height: 17px; }
.rightpin { height: 17px; }
""";

def get_pins():
  pins = pinutils.generate_pins(0,31) # 32 General Purpose I/O Pins.
  pinutils.findpin(pins, "PD0", True)["functions"]["XL1"]=0;
  pinutils.findpin(pins, "PD1", True)["functions"]["XL2"]=0;
  pinutils.findpin(pins, "PD5", True)["functions"]["RTS"]=0;
  pinutils.findpin(pins, "PD6", True)["functions"]["TXD"]=0;
  pinutils.findpin(pins, "PD7", True)["functions"]["CTS"]=0;
  pinutils.findpin(pins, "PD8", True)["functions"]["RXD"]=0;
  pinutils.findpin(pins, "PD9", True)["functions"]["NFC1"]=0;
  pinutils.findpin(pins, "PD10", True)["functions"]["NFC2"]=0;
  pinutils.findpin(pins, "PD2", True)["functions"]["ADC1_IN0"]=0;
  pinutils.findpin(pins, "PD3", True)["functions"]["ADC1_IN1"]=0;
  pinutils.findpin(pins, "PD4", True)["functions"]["ADC1_IN2"]=0;
  pinutils.findpin(pins, "PD5", True)["functions"]["ADC1_IN3"]=0;
  pinutils.findpin(pins, "PD28", True)["functions"]["ADC1_IN4"]=0;
  pinutils.findpin(pins, "PD29", True)["functions"]["ADC1_IN5"]=0;
  pinutils.findpin(pins, "PD30", True)["functions"]["ADC1_IN6"]=0;
  pinutils.findpin(pins, "PD31", True)["functions"]["ADC1_IN7"]=0;
  # Make buttons and LEDs negated
  pinutils.findpin(pins, "PD13", True)["functions"]["NEGATED"]=0;
  pinutils.findpin(pins, "PD14", True)["functions"]["NEGATED"]=0;
  pinutils.findpin(pins, "PD15", True)["functions"]["NEGATED"]=0;
  pinutils.findpin(pins, "PD16", True)["functions"]["NEGATED"]=0;
  pinutils.findpin(pins, "PD17", True)["functions"]["NEGATED"]=0;
  pinutils.findpin(pins, "PD18", True)["functions"]["NEGATED"]=0;
  pinutils.findpin(pins, "PD19", True)["functions"]["NEGATED"]=0;
  pinutils.findpin(pins, "PD20", True)["functions"]["NEGATED"]=0;

  # everything is non-5v tolerant
  for pin in pins:
    pin["functions"]["3.3"]=0;
  #The boot/reset button will function as a reset button in normal operation. Pin reset on PD21 needs to be enabled on the nRF52832 device for this to work.
  return pins
