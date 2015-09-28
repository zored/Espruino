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
    'name' : "Iskra.js",
    'link' :  [ "http://amperka.ru/product/iskra-js" ],
    'default_console' : "EV_SERIAL2",
    'variables' : 5450,
    'binary_name' : 'espruino_%v_iskrajs.bin',
}

chip = {
    'part' : "STM32F405RGT6",
    'family' : "STM32F4",
    'package' : "LQFP64",
    'ram' : 192,
    'flash' : 1024,
    'speed' : 168,
    'usart' : 6,
    'spi' : 3,
    'i2c' : 3,
    'adc' : 3,
    'dac' : 2,
}

# TODO
board = {
}

devices = {
    'OSC' : { 'pin_1' : 'H0',
              'pin_2' : 'H1' },
    'LED1': { 'pin' : 'B6' },
    'BTN1': { 'pin' : 'C4' },
    'USB' : { 'pin_dm' : 'A11',
              'pin_dp' : 'A12',
              'pin_vbus' : 'A9',
              'pin_id' : 'A10', },
    'JTAG': { 'pin_MS' : 'A13',
              'pin_CK' : 'A14', 
              'pin_DI' : 'A15', },
}


# TODO:
board_css = """
""";

def get_pins():
    pins = pinutils.scan_pin_file([], 'stm32f40x.csv', 6, 9, 10)
    return pinutils.only_from_package(pinutils.fill_gaps_in_pin_list(pins), chip["package"])

if __name__ == '__main__':
    from pprint import pprint
    pprint(get_pins())
