/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2013 Gordon Williams <gw@pur3.co.uk>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * ----------------------------------------------------------------------------
 * This file is designed to be parsed during the build process
 *
 * Contains JavaScript interface for the hexagonal Espruino badge
 * ----------------------------------------------------------------------------
 */

/* DO_NOT_INCLUDE_IN_DOCS - this is a special token for common.py */

#include <jswrap_hexbadge.h>
#include "jsinteractive.h"
#include "jsdevices.h"
#include "jsnative.h"
#include "jshardware.h"
#include "jsdevices.h"
#include "jspin.h"
#include "jstimer.h"
#include "jswrap_bluetooth.h"
#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "nrf5x_utils.h"

#include "jswrap_graphics.h"
#include "lcd_arraybuffer.h"

/*

 var BTNA = D19;
var BTNB = D20;
var CORNER = [D25,D26,D27,D28,D29,D30];
var CAPSENSE = D22;
var BTNU = D31;
var BTND = D16;
var BTNL = D17;
var BTNR = D18;

 */

const Pin PIN_BTNA = 19;
const Pin PIN_BTNB = 20;
const Pin PIN_BTNU = 31;
const Pin PIN_BTND = 16;
const Pin PIN_BTNL = 17;
const Pin PIN_BTNR = 18;
const Pin PIN_CAPSENSE  = 22;
const Pin PIN_CORNERS[] = {25,26,27,28,29,30};

const Pin LCD_DC = 13;
const Pin LCD_CS = 12;
const Pin LCD_RST = 11;
const Pin LCD_SCK = 14;
const Pin LCD_MOSI = 15;


/*JSON{
  "type" : "variable",
  "name" : "BTNA",
  "generate_full" : "19",
  "return" : ["pin",""]
}
The pin connected to the 'A' button. Reads as `1` when pressed, `0` when not
*/
/*JSON{
  "type" : "variable",
  "name" : "BTNB",
  "generate_full" : "20",
  "return" : ["pin",""]
}
The pin connected to the 'B' button. Reads as `1` when pressed, `0` when not
*/
/*JSON{
  "type" : "variable",
  "name" : "BTNU",
  "generate_full" : "31",
  "return" : ["pin",""]
}
The pin connected to the up button. Reads as `1` when pressed, `0` when not
*/
/*JSON{
  "type" : "variable",
  "name" : "BTND",
  "generate_full" : "16",
  "return" : ["pin",""]
}
The pin connected to the down button. Reads as `1` when pressed, `0` when not
*/
/*JSON{
  "type" : "variable",
  "name" : "BTNL",
  "generate_full" : "17",
  "return" : ["pin",""]
}
The pin connected to the left button. Reads as `1` when pressed, `0` when not
*/
/*JSON{
  "type" : "variable",
  "name" : "BTNR",
  "generate_full" : "18",
  "return" : ["pin",""]
}
The pin connected to the right button. Reads as `1` when pressed, `0` when not
*/
/*JSON{
  "type" : "variable",
  "name" : "CORNER1",
  "generate_full" : "25",
  "return" : ["pin",""]
}
The pin connected to Corner #1
*/
/*JSON{
  "type" : "variable",
  "name" : "CORNER2",
  "generate_full" : "26",
  "return" : ["pin",""]
}
The pin connected to Corner #2
*/
/*JSON{
  "type" : "variable",
  "name" : "CORNER3",
  "generate_full" : "27",
  "return" : ["pin",""]
}
The pin connected to Corner #3
*/
/*JSON{
  "type" : "variable",
  "name" : "CORNER4",
  "generate_full" : "28",
  "return" : ["pin",""]
}
The pin connected to Corner #4
*/
/*JSON{
  "type" : "variable",
  "name" : "CORNER5",
  "generate_full" : "29",
  "return" : ["pin",""]
}
The pin connected to Corner #5
*/
/*JSON{
  "type" : "variable",
  "name" : "CORNER6",
  "generate_full" : "30",
  "return" : ["pin",""]
}
The pin connected to Corner #6
*/



/*JSON{
    "type": "class",
    "class" : "Badge"
}
Class containing utility functions for accessing IO on the hexagonal badge
*/
/*JSON{
    "type" : "staticmethod",
    "class" : "Badge",
    "name" : "capSense",
    "generate" : "jswrap_badge_capSense",
    "params" : [
      ["corner","int","The corner to use"]
    ],
    "return" : ["int", "Capacitive sense counter" ]
}
Capacitive sense - the higher the capacitance, the higher the number returned.

Supply a corner number between 1 and 6, and an integer value will be returned that is proportional to the capacitance
*/
int jswrap_badge_capSense(int corner) {
  if (corner>=1 && corner<=6) {
    return (int)nrf_utils_cap_sense(PIN_CAPSENSE, PIN_CORNERS[corner-1]);
  }
  return 0;
}

/*JSON{
    "type" : "staticmethod",
    "class" : "Badge",
    "name" : "getBatteryPercentage",
    "generate" : "jswrap_badge_getBatteryPercentage",
    "return" : ["int", "A percentage between 0 and 100" ]
}
Return an approximate battery percentage remaining based on
a normal CR2032 battery (2.8 - 2.2v)
*/
int jswrap_badge_getBatteryPercentage() {
  JsVarFloat v = jswrap_ble_getBattery();
  int pc = (v-2.2)*100/0.6;
  if (pc>100) pc=100;
  if (pc<0) pc=0;
  return pc;
}



void badge_lcd_wr(int data) {
  int bit;
  for (bit=7;bit>=0;bit--) {
    jshPinSetValue(LCD_MOSI, (data>>bit)&1 );
    jshPinSetValue(LCD_SCK, 1 );
    jshPinSetValue(LCD_SCK, 0 );
  }
}

void badge_lcd_flip(JsVar *g) {
  JsVar *buf = jsvObjectGetChild(g,"buffer",0);
  if (!buf) return;
  JSV_GET_AS_CHAR_ARRAY(bPtr, bLen, buf);
  if (!bPtr || bLen<128*8) return;

  jshPinSetValue(LCD_CS,0);
  for (int y=0;y<8;y++) {
    jshPinSetValue(LCD_DC,0);
    badge_lcd_wr(0xB0|y/* page */);
    badge_lcd_wr(0x00/* x lower*/);
    badge_lcd_wr(0x10/* x upper*/);
    jshPinSetValue(LCD_DC,1);
    for (int x=0;x<128;x++)
      badge_lcd_wr(*(bPtr++));
  }
  jshPinSetValue(LCD_CS,1);
  jsvUnLock(buf);
}


/*JSON{
    "type" : "staticmethod",
    "class" : "Badge",
    "name" : "setContrast",
    "generate" : "jswrap_badge_setContrast",
    "params" : [
      ["c","float","Contrast between 0 and 1"]
    ]
}
Set the LCD's contrast */
void jswrap_badge_setContrast(JsVarFloat c) {
  if (c<0) c=0;
  if (c>1) c=1;
  jshPinSetValue(LCD_CS,0);
  jshPinSetValue(LCD_DC,0);
  badge_lcd_wr(0x81);
  badge_lcd_wr((int)(63*c));
  //badge_lcd_wr(0x20|div); div = 0..7
  jshPinSetValue(LCD_CS,1);
}

/*JSON{
  "type" : "init",
  "generate" : "jswrap_badge_init"
}*/
void jswrap_badge_init() {
  // LCD Init 1
  jshPinOutput(LCD_CS,0);
  jshPinOutput(LCD_DC,0);
  jshPinOutput(LCD_SCK,0);
  jshPinOutput(LCD_MOSI,0);
  jshPinOutput(LCD_RST,0);
  // Create backing graphics for LCD
  JsVar *graphics = jspNewObject(0, "Graphics");
  if (!graphics) return; // low memory
  JsGraphics gfx;
  graphicsStructInit(&gfx,128,64,1);
  gfx.data.type = JSGRAPHICSTYPE_ARRAYBUFFER;
  gfx.data.flags = JSGRAPHICSFLAGS_ARRAYBUFFER_VERTICAL_BYTE | JSGRAPHICSFLAGS_INVERT_X;
  gfx.graphicsVar = graphics;
  lcdInit_ArrayBuffer(&gfx);
  graphicsSetVar(&gfx);
  jsvObjectSetChild(execInfo.root,"g",graphics);
  // Set initial image
  const unsigned int LCD_IMIT_IMG_OFFSET = 344;
  const unsigned char LCD_INIT_IMG[] = {
    128, 128, 128, 128, 128, 192, 96, 176, 88, 52, 30, 14, 6, 12, 12, 12, 12, 12, 12, 12, 8, 24, 24, 24, 24, 216, 56, 
    152, 240, 240, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 128, 128, 128, 
    0, 0, 0, 0, 0, 128, 128, 128, 0, 0, 128, 128, 0, 0, 176, 176, 0, 0, 128, 128, 128, 0, 0, 0, 128, 128, 0, 128, 128, 0, 
    128, 128, 0, 0, 0, 0, 128, 128, 128, 0, 128, 128, 0, 0, 0, 0, 128, 128, 128, 128, 0, 0, 0, 48, 48, 48, 48, 48, 48, 240, 
    240, 240, 0, 0, 0, 0, 255, 129, 56, 125, 199, 255, 2, 3, 6, 6, 6, 6, 7, 7, 15, 13, 13, 13, 13, 14, 14, 30, 30, 22, 22, 
    9, 6, 1, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 62, 127, 227, 193, 193, 
    227, 127, 62, 0, 0, 255, 255, 3, 1, 1, 255, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 127, 192, 192, 192, 255, 127, 0, 
    1, 1, 3, 255, 255, 0, 0, 62, 127, 227, 193, 193, 99, 255, 255, 0, 0, 114, 251, 217, 217, 205, 207, 103, 0, 0, 192, 198, 
    198, 198, 198, 198, 255, 255, 255, 0, 0, 0, 0, 0, 1, 1, 226, 255, 255, 0, 128, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 192, 224, 248, 255, 120, 56, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 7, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 7, 5, 7, 
    15, 11, 15, 15, 11, 15, 31, 22, 30, 30, 22, 30, 30, 8, 8, 12, 15, 11, 15, 3, 1
  };
  JsVar *buf = jsvObjectGetChild(graphics,"buffer",0);
  JSV_GET_AS_CHAR_ARRAY(bPtr, bLen, buf);
  if (bPtr) memcpy(&bPtr[LCD_IMIT_IMG_OFFSET], LCD_INIT_IMG, sizeof(LCD_INIT_IMG));
  jsvUnLock(buf);
  // Create 'flip' fn
  JsVar *fn = jsvNewNativeFunction((void (*)(void))badge_lcd_flip, JSWAT_VOID|JSWAT_THIS_ARG);
  jsvObjectSetChildAndUnLock(graphics,"flip",fn);
  // LCD init 2
  jshDelayMicroseconds(10000);
  jshPinSetValue(LCD_RST,1);
  jshDelayMicroseconds(10000);
  const unsigned char LCD_INIT_DATA[] = {
       // 0xE2,   // soft reset
       0xA3,   // bias 1/7
       0xC8,   // reverse scan dir
       0x25,   // regulation resistor ratio (0..7)
       0x81,   // contrast control
       0x17,
       0x2F,   // control power circuits - last 3 bits = VB/VR/VF
       0xA1,   // start at column 0
       0xAF    // disp on
  };
  // TODO: start at column 128 (0xA0) and don't 'flip' the LCD
  for (unsigned int i=0;i<sizeof(LCD_INIT_DATA);i++)
    badge_lcd_wr(LCD_INIT_DATA[i]);
  jshPinSetValue(LCD_CS,1);
  // actually flip the LCD contents
  badge_lcd_flip(graphics);
  jsvUnLock(graphics);
}

/*JSON{
  "type" : "kill",
  "generate" : "jswrap_badge_kill"
}*/
void jswrap_badge_kill() {

}

/*JSON{
  "type" : "idle",
  "generate" : "jswrap_badge_idle"
}*/
bool jswrap_badge_idle() {
  return false;
}
