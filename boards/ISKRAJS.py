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
    pinmapping = { 
        'PIN0' :'PB10',
        'PIN1' :'PB11',
        'PIN2' :'PA6',
        'PIN3' :'PA7',
        'PIN4' :'PB1',
        'PIN5' :'PC3',
        'PIN6' :'PB0',
        'PIN7' :'PC2',
        'PIN8' :'PC1',
        'PIN9' :'PC0',
        'PIN10':'PC9',
        'PIN11':'PA8',
        'PIN12':'PC12',
        'PIN13':'PD2',
        'PINA0':'PA0',
        'PINA1':'PA1',
        'PINA2':'PA2',
        'PINA3':'PA3',
        'PINA4':'PA4',
        'PINA5':'PA5',
    }

    pins = pinutils.scan_pin_file([], 'stm32f40x.csv', 6, 9, 10)
    newpins = []

    for iskra_name, stm32_name in pinmapping.items():
        pin = pinutils.findpin(pins, stm32_name, True)
        pin["name"] = iskra_name
        pin["sortingname"] = iskra_name[3:].rjust(2, '0')
        newpins.append(pin) 

    newpins.sort(key=lambda p: p['sortingname'])

    reserved_names = []
    for dev in devices.values():
        for port in dev.values():
            reserved_names.append('P' + port)

    for name in reserved_names:
        pin = pinutils.findpin(pins, name, True)
        newpins.append(pin)

    return newpins

if __name__ == '__main__':
    from pprint import pprint
    pprint(get_pins())
