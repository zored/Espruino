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
 'name' : "Raspberry Pi",
 'default_console' : "EV_USBSERIAL",
 'variables' :  0, # 0 = resizable variables, rather than fixed
 'binary_name' : 'espruino_%v_raspberrypi',
 'build' : {
   'optimizeflags' : '-O3',
   'libraries' : [
     'NET',
     'GRAPHICS',
     'FILESYSTEM',
     'CRYPTO','SHA256','SHA512',
     'TLS',
     'TELNET',
   ],
   'makefile' : [
     'LINUX=1',
     'DEFINES += -DRASPBERRYPI',
   ]
 }
};
chip = {
  'part' : "RASPBERRYPI",
  'family' : "LINUX",
  'package' : "",
  'ram' : 0,
  'flash' : 256, # size of file used to fake flash memory (kb)
  'speed' : -1,
  'usart' : 1,
  'spi' : 1,
  'i2c' : 1,
  'adc' : 0,
  'dac' : 0,
};

devices = {
  'LED1' : { 'pin' : 'D16' }
};

def get_pins():
  pins = pinutils.generate_pins(0,31)
  pinutils.findpin(pins, "PD0", True)["functions"]["I2C1_SDA"]=0; # Rev 1
  pinutils.findpin(pins, "PD1", True)["functions"]["I2C1_SCL"]=0; # Rev 1
  pinutils.findpin(pins, "PD2", True)["functions"]["I2C1_SDA"]=0; # Rev 2
  pinutils.findpin(pins, "PD3", True)["functions"]["I2C1_SCL"]=0; # Rev 2
  pinutils.findpin(pins, "PD9", True)["functions"]["SPI1_MISO"]=0;
  pinutils.findpin(pins, "PD10", True)["functions"]["SPI1_MOSI"]=0;
  pinutils.findpin(pins, "PD11", True)["functions"]["SPI1_SCK"]=0;
  pinutils.findpin(pins, "PD14", True)["functions"]["UART1_TX"]=0;
  pinutils.findpin(pins, "PD15", True)["functions"]["UART1_RX"]=0;
  return pins
