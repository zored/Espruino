#!/usr/bin/python

# This file is part of Espruino, a JavaScript interpreter for Microcontrollers
#
# Copyright (C) 2013 Gordon Williams <gw@pur3.co.uk>
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# ----------------------------------------------------------------------------------------
# Reads board information from boards/BOARDNAME.py and uses it to generate a header file
# which describes the available peripherals on the board
# ----------------------------------------------------------------------------------------
import subprocess;
import re;
import json;
import sys;
import os;
import importlib;
import common;

scriptdir = os.path.dirname(os.path.realpath(__file__))
# added os.path.normpath to get a correct reckognition of the subsequent path
# by Ubuntu 14.04 LTS
basedir = os.path.normpath(scriptdir+"/../")
# added leading / as a consequence of use of os.path.normpath
sys.path.append(basedir+"/scripts");
sys.path.append(basedir+"/boards");

import pinutils;

# -----------------------------------------------------------------------------------------

# Now scan AF file
print("Script location "+scriptdir)

if len(sys.argv)!=3:
  print("ERROR, USAGE: build_platform_config.py BOARD_NAME HEADERFILENAME")
  exit(1)
boardname = sys.argv[1]
headerFilename = sys.argv[2]
print("HEADER_FILENAME "+headerFilename)
print("BOARD "+boardname)
# import the board def
board = importlib.import_module(boardname)
pins = board.get_pins()
# -----------------------------------------------------------------------------------------

LINUX = board.chip["family"]=="LINUX"
EMSCRIPTEN = board.chip["family"]=="EMSCRIPTEN"

if not "default_console" in board.info:
  board.info["default_console"] = "EV_SERIAL1"

has_bootloader = False
if "bootloader" in board.info and board.info["bootloader"]!=0:
  has_bootloader = True

variables=board.info["variables"]
# variables from board-file can bw overwritten. Be careful with this option.
# usually the definition in board file are already the maximum, and adding some more will corrupt firmware
if 'VARIABLES' in os.environ:
  variables=int(os.environ['VARIABLES'])
if variables==0: var_size = 16
elif variables<1023: var_size = 12   # the 'packed bits mean anything under 1023 vars gets into 12 byte JsVars
else: var_size = 16
var_cache_size = var_size*variables
flash_needed = var_cache_size + 4 # for magic number
print("Variables = "+str(variables))
print("JsVar size = "+str(var_size))
print("VarCache size = "+str(var_cache_size))

flash_page_size = 1024

if LINUX:
  flash_saved_code_pages = board.chip['flash']*1024 / flash_page_size
  total_flash = flash_page_size*flash_saved_code_pages  
else: # NOT LINUX
  # 100xB and 103xB are mid-density, so have 1k page sizes
  if board.chip["part"][:7]=="STM32F1" and board.chip["part"][10]=="B": board.chip["subfamily"]="MD";

  if board.chip["family"]=="STM32F1": 
    flash_page_size = 1024 if "subfamily" in board.chip and board.chip["subfamily"]=="MD" else 2048
  if board.chip["family"]=="STM32F2":
    flash_page_size = 128*1024
  if board.chip["family"]=="STM32F3": 
    flash_page_size = 2*1024
  if board.chip["family"]=="STM32F4":
    flash_page_size = 128*1024
  if board.chip["family"]=="NRF51":
    flash_page_size = 1024
  if board.chip["family"]=="NRF52":
    flash_page_size = 4*1024
  if board.chip["family"]=="EFM32GG":
    flash_page_size = 4*1024
  if board.chip["family"]=="STM32L4":
    flash_page_size = 128*1024
  flash_saved_code_pages = round((flash_needed+flash_page_size-1)/flash_page_size + 0.5) #Needs to be a full page, so we're rounding up
  # F4 has different page sizes in different places
  total_flash = board.chip["flash"]*1024

if "saved_code" in board.chip:
  flash_saved_code_start = board.chip["saved_code"]["address"]
  flash_page_size = board.chip["saved_code"]["page_size"]
  flash_saved_code_pages = board.chip["saved_code"]["pages"]
  flash_available_for_code = board.chip["saved_code"]["flash_available"]*1024
else:
  flash_saved_code_start = "(FLASH_START + FLASH_TOTAL - FLASH_SAVED_CODE_LENGTH)"
  flash_available_for_code = total_flash - (flash_saved_code_pages*flash_page_size)
  if has_bootloader: flash_available_for_code -= common.get_bootloader_size(board)



print("Flash page size = "+str(flash_page_size))
print("Flash pages = "+str(flash_saved_code_pages))
print("Total flash = "+str(total_flash))
print("Flash available for code = "+str(flash_available_for_code))


# -----------------------------------------------------------------------------------------
headerFile = open(headerFilename, 'w')
def codeOut(s): headerFile.write(s+"\n");
# -----------------------------------------------------------------------------------------
def die(err):
  print("ERROR: "+err)
  sys.exit(1)

def toPinDef(pin):
  for p in pins:
    if p["name"]=="P"+pin:
      return str(pins.index(p))+"/* "+pin+" */";
  die("Pin named '"+pin+"' not found");

def codeOutDevice(device):
  if device in board.devices:
    codeOut("#define "+device+"_PININDEX "+toPinDef(board.devices[device]["pin"]))
    if device[0:3]=="BTN":
      codeOut("#define "+device+"_ONSTATE "+("0" if "inverted" in board.devices[device] else "1"))
      if "pinstate" in board.devices[device]:
        codeOut("#define "+device+"_PINSTATE JSHPINSTATE_GPIO_"+board.devices[device]["pinstate"]);
    if device[0:3]=="LED":
      codeOut("#define "+device+"_ONSTATE "+("0" if "inverted" in board.devices[device] else "1"))
    if "no_bootloader" in board.devices[device]:
      codeOut("#define "+device+"_NO_BOOTLOADER 1 // don't use this in the bootloader");

def codeOutDevicePin(device, pin, definition_name):
  if device in board.devices:
    codeOut("#define "+definition_name+" "+toPinDef(board.devices[device][pin]))

def codeOutDevicePins(device, definition_name):
  for entry in board.devices[device]:
    if entry.startswith("pin_") or entry=="pin":
      codeOut("#define "+definition_name+"_"+entry.upper()+" "+toPinDef(board.devices[device][entry]))
# -----------------------------------------------------------------------------------------


codeOut("""
// Automatically generated header file for """+boardname+"""
// Generated by scripts/build_platform_config.py

#ifndef _PLATFORM_CONFIG_H
#define _PLATFORM_CONFIG_H

""");

codeOut("#define PC_BOARD_ID \""+boardname+"\"")
codeOut("#define PC_BOARD_CHIP \""+board.chip["part"]+"\"")
codeOut("#define PC_BOARD_CHIP_FAMILY \""+board.chip["family"]+"\"")

if "json_url" in board.info:
    codeOut('#define PC_JSON_URL "{0}"'.format(board.info['json_url']))

codeOut("")

# Linker vars used for:
linker_end_var = "_end";     # End of RAM (eg top of stack)
linker_etext_var = "_etext"; # End of text (function) section
# External interrupt count
exti_count = 16 

if board.chip["family"]=="LINUX":
  board.chip["class"]="LINUX"
elif board.chip["family"]=="EMSCRIPTEN":
  board.chip["class"]="EMSCRIPTEN"
elif board.chip["family"]=="STM32F1":
  board.chip["class"]="STM32"
  codeOut('#include "stm32f10x.h"')
elif board.chip["family"]=="STM32F2":
  board.chip["class"]="STM32"
  codeOut('#include "stm32f2xx.h"')
  codeOut("#define STM32API2 // hint to jshardware that the API is a lot different")
elif board.chip["family"]=="STM32F3":
  board.chip["class"]="STM32"
  codeOut('#include "stm32f30x.h"')
  codeOut("#define STM32API2 // hint to jshardware that the API is a lot different")
  codeOut("#define USB_INT_DEFAULT") # hack
elif board.chip["family"]=="STM32F4":
  board.chip["class"]="STM32"
  codeOut('#include "stm32f4xx.h"')
  codeOut('#include "stm32f4xx_conf.h"')
  codeOut("#define STM32API2 // hint to jshardware that the API is a lot different")
elif board.chip["family"]=="STM32L4":
  board.chip["class"]="STM32_LL"
  codeOut('#include "stm32l4xx_ll_bus.h"')
  codeOut('#include "stm32l4xx_ll_rcc.h"')
  codeOut('#include "stm32l4xx_ll_adc.h"')
elif board.chip["family"]=="NRF51":
  board.chip["class"]="NRF51"
  linker_etext_var = "__etext";
  linker_end_var = "end";
  exti_count = 4
  codeOut('#include "nrf.h"')
elif board.chip["family"]=="NRF52":
  board.chip["class"]="NRF52"
  linker_etext_var = "__etext";
  linker_end_var = "end";
  exti_count = 8
  codeOut('#include "nrf.h"')
elif board.chip["family"]=="EFM32GG":
  board.chip["class"]="EFM32"
  linker_etext_var = "__etext";
  codeOut('#include "em_device.h"')
elif board.chip["family"]=="LPC1768":
  board.chip["class"]="MBED"
elif board.chip["family"]=="AVR":
  board.chip["class"]="AVR"
elif board.chip["family"]=="ESP8266":
  board.chip["class"]="ESP8266"
elif board.chip["family"]=="ESP32":
  board.chip["class"]="ESP32"
  exti_count = 40
elif board.chip["family"]=="SAMD":
  board.chip["class"]="SAMD"
  codeOut('#include "targetlibs/samd/include/due_sam3x.init.h"')
else:
  die('Unknown chip family '+board.chip["family"])

codeOut("#define LINKER_END_VAR "+linker_end_var);
codeOut("#define LINKER_ETEXT_VAR "+linker_etext_var);

if board.chip["class"]=="MBED":
  codeOut("""
  #pragma diag_suppress 1295 // deprecated decl
  #pragma diag_suppress 188 // enumerated type mixed with another type
  #pragma diag_suppress 111 // statement is unreachable
  #pragma diag_suppress 68 // integer conversion resulted in a change of sign
  """);

codeOut("""

// SYSTICK is the counter that counts up and that we use as the real-time clock
// The smaller this is, the longer we spend in interrupts, but also the more we can sleep!
#define SYSTICK_RANGE 0x1000000 // the Maximum (it is a 24 bit counter) - on Olimexino this is about 0.6 sec
#define SYSTICKS_BEFORE_USB_DISCONNECT 2

// When to send the message that the IO buffer is getting full
#define IOBUFFER_XOFF ((TXBUFFERMASK)*6/8)
// When to send the message that we can start receiving again
#define IOBUFFER_XON ((TXBUFFERMASK)*3/8)
#define DEFAULT_BUSY_PIN_INDICATOR (Pin)-1 // no indicator
#define DEFAULT_SLEEP_PIN_INDICATOR (Pin)-1 // no indicator
""");

util_timer = pinutils.get_device_util_timer(board)
if util_timer!=False:
  codeOut(util_timer['defines']);

codeOut("");
# ------------------------------------------------------------------------------------- Chip Specifics
codeOut("#define RAM_TOTAL ("+str(board.chip['ram'])+"*1024)")
codeOut("#define FLASH_TOTAL ("+str(board.chip['flash'])+"*1024)")
codeOut("");

if variables==0:
  codeOut('#define RESIZABLE_JSVARS // Allocate variables in blocks using malloc - slow, and linux-only')
else:
  codeOut("#define JSVAR_CACHE_SIZE                "+str(variables)+" // Number of JavaScript variables in RAM")

if LINUX:  
  codeOut("#define FLASH_START                     "+hex(0x10000000))
  codeOut("#define FLASH_PAGE_SIZE                 "+str(flash_page_size))
else:  
  codeOut("#define FLASH_AVAILABLE_FOR_CODE        "+str(int(flash_available_for_code)))
  if board.chip["class"]=="EFM32":
    codeOut("// FLASH_PAGE_SIZE defined in em_device.h");
  else:
    codeOut("#define FLASH_PAGE_SIZE                 "+str(flash_page_size))
  if board.chip["family"]=="ESP8266":
    codeOut("#define FLASH_START                     "+hex(0x0))
  elif board.chip["family"]=="NRF52" or board.chip["family"]=="NRF51":
    codeOut("#define FLASH_START                     "+hex(0x0))
  elif board.chip["class"]=="EFM32":
    codeOut("#define FLASH_START                     FLASH_BASE // FLASH_BASE defined in em_device.h")
  else:
    codeOut("#define FLASH_START                     "+hex(0x08000000))
  if has_bootloader:
    codeOut("#define BOOTLOADER_SIZE                 "+str(common.get_bootloader_size(board)))
    codeOut("#define ESPRUINO_BINARY_ADDRESS         "+hex(common.get_espruino_binary_address(board)))
  codeOut("")


codeOut("#define FLASH_SAVED_CODE_START            "+str(flash_saved_code_start))
codeOut("#define FLASH_SAVED_CODE_LENGTH           "+str(int(flash_page_size*flash_saved_code_pages)))
codeOut("");

codeOut("#define CLOCK_SPEED_MHZ                      "+str(board.chip["speed"]))
codeOut("#define USART_COUNT                          "+str(board.chip["usart"]))
codeOut("#define SPI_COUNT                            "+str(board.chip["spi"]))
codeOut("#define I2C_COUNT                            "+str(board.chip["i2c"]))
codeOut("#define ADC_COUNT                            "+str(board.chip["adc"]))
codeOut("#define DAC_COUNT                            "+str(board.chip["dac"]))
codeOut("#define EXTI_COUNT                           "+str(exti_count))
codeOut("");
codeOut("#define DEFAULT_CONSOLE_DEVICE              "+board.info["default_console"]);
if "default_console_tx" in board.info:
  codeOut("#define DEFAULT_CONSOLE_TX_PIN "+toPinDef(board.info["default_console_tx"]))
if "default_console_rx" in board.info:
  codeOut("#define DEFAULT_CONSOLE_RX_PIN "+toPinDef(board.info["default_console_rx"]))
if "default_console_baudrate" in board.info:
  codeOut("#define DEFAULT_CONSOLE_BAUDRATE "+board.info["default_console_baudrate"])


codeOut("");
if LINUX:
  bufferSizeIO = 256
  bufferSizeTX = 256
  bufferSizeTimer = 16
elif EMSCRIPTEN:
  bufferSizeIO = 256
  bufferSizeTX = 256
  bufferSizeTimer = 16
else:
  # IO buffer - for received chars, setWatch, etc
  bufferSizeIO = 64
  if board.chip["ram"]>=20: bufferSizeIO = 128
  if board.chip["ram"]>=96: bufferSizeIO = 256
  # NRF52 needs this as Bluetooth traffic is funnelled through the buffer
  if board.chip["family"]=="NRF52": bufferSizeIO = 256
  # TX buffer - for print/write/etc
  bufferSizeTX = 32 
  if board.chip["ram"]>=20: bufferSizeTX = 128
  bufferSizeTimer = 4 if board.chip["ram"]<20 else 16

if 'util_timer_tasks' in board.info:
  bufferSizeTimer = board.info['util_timer_tasks']

codeOut("#define IOBUFFERMASK "+str(bufferSizeIO-1)+" // (max 255) amount of items in event buffer - events take 5 bytes each")
codeOut("#define TXBUFFERMASK "+str(bufferSizeTX-1)+" // (max 255) amount of items in the transmit buffer - 2 bytes each")
codeOut("#define UTILTIMERTASK_TASKS ("+str(bufferSizeTimer)+") // Must be power of 2 - and max 256")

codeOut("");

usedPinChecks = ["false"];
ledChecks = ["false"];
btnChecks = ["false"];
for device in pinutils.SIMPLE_DEVICES:
  if device in board.devices:
    codeOutDevice(device)
    check = "(PIN)==" + toPinDef(board.devices[device]["pin"])
    if device[:3]=="LED": ledChecks.append(check)
    if device[:3]=="BTN": btnChecks.append(check)
#   usedPinChecks.append(check)
# Actually we don't care about marking used pins for LEDs/Buttons

if "USB" in board.devices:
  if "pin_disc" in board.devices["USB"]: codeOutDevicePin("USB", "pin_disc", "USB_DISCONNECT_PIN")
  if "pin_vsense" in board.devices["USB"]: codeOutDevicePin("USB", "pin_vsense", "USB_VSENSE_PIN")

if "LCD" in board.devices:
  codeOut("#define LCD_CONTROLLER_"+board.devices["LCD"]["controller"].upper())
  if "width" in board.devices["LCD"]:
    codeOut("#define LCD_WIDTH "+str(board.devices["LCD"]["width"]))
  if "height" in board.devices["LCD"]:
    codeOut("#define LCD_HEIGHT "+str(board.devices["LCD"]["height"]))
  if "bpp" in board.devices["LCD"]:
    codeOut("#define LCD_BPP "+str(board.devices["LCD"]["bpp"]))
  if "pin_bl" in board.devices["LCD"]:
    codeOutDevicePin("LCD", "pin_bl", "LCD_BL")
  if board.devices["LCD"]["controller"]=="fsmc":
    for i in range(0,16):
      codeOutDevicePin("LCD", "pin_d"+str(i), "LCD_FSMC_D"+str(i))
    codeOutDevicePin("LCD", "pin_rd", "LCD_FSMC_RD")
    codeOutDevicePin("LCD", "pin_wr", "LCD_FSMC_WR")
    codeOutDevicePin("LCD", "pin_cs", "LCD_FSMC_CS")
    if "pin_rs" in board.devices["LCD"]:
      codeOutDevicePin("LCD", "pin_rs", "LCD_FSMC_RS")
    if "pin_reset" in board.devices["LCD"]:
      codeOutDevicePin("LCD", "pin_reset", "LCD_RESET")
  if board.devices["LCD"]["controller"]=="ssd1306" or board.devices["LCD"]["controller"]=="st7567" or board.devices["LCD"]["controller"]=="st7789v" or board.devices["LCD"]["controller"]=="st7735":
    codeOutDevicePin("LCD", "pin_mosi", "LCD_SPI_MOSI")
    codeOutDevicePin("LCD", "pin_sck", "LCD_SPI_SCK")
    codeOutDevicePin("LCD", "pin_cs", "LCD_SPI_CS")
    codeOutDevicePin("LCD", "pin_dc", "LCD_SPI_DC")
    codeOutDevicePin("LCD", "pin_rst", "LCD_SPI_RST")
  if "pin_bl" in board.devices["LCD"]:
    codeOutDevicePin("LCD", "pin_bl", "LCD_BL")
  if board.devices["LCD"]["controller"]=="st7789_8bit":
    codeOutDevicePins("LCD","LCD");

if "SD" in board.devices:
  if not "pin_d3" in board.devices["SD"]: # NOT SDIO - normal SD
    if "pin_cs" in board.devices["SD"]: codeOutDevicePin("SD", "pin_cs", "SD_CS_PIN")
    if "pin_di" in board.devices["SD"]: codeOutDevicePin("SD", "pin_di", "SD_DI_PIN")
    if "pin_do" in board.devices["SD"]: codeOutDevicePin("SD", "pin_do", "SD_DO_PIN")
    if "pin_clk" in board.devices["SD"]:
      codeOutDevicePin("SD", "pin_clk", "SD_CLK_PIN")
      sdClkPin = pinutils.findpin(pins, "P"+board.devices["SD"]["pin_clk"], False)
      spiNum = 0
      for func in sdClkPin["functions"]:
        if func[:3]=="SPI": spiNum = int(func[3])
      if spiNum==0: die("No SPI peripheral found for SD card's CLK pin")
      codeOut("#define SD_SPI EV_SPI"+str(spiNum))

if "IR" in board.devices:
  codeOutDevicePin("IR", "pin_anode", "IR_ANODE_PIN")
  codeOutDevicePin("IR", "pin_cathode", "IR_CATHODE_PIN")

if "CAPSENSE" in board.devices:
  codeOutDevicePin("CAPSENSE", "pin_rx", "CAPSENSE_RX_PIN")
  codeOutDevicePin("CAPSENSE", "pin_tx", "CAPSENSE_TX_PIN")

if "VIBRATE" in board.devices:
  codeOutDevicePins("VIBRATE", "VIBRATE")

if "SPEAKER" in board.devices:
  codeOutDevicePins("SPEAKER", "SPEAKER")

if "HEARTRATE" in board.devices:
  codeOutDevicePins("HEARTRATE", "HEARTRATE")

if "BAT" in board.devices:
  codeOutDevicePins("BAT", "BAT")

if "GPS" in board.devices:
  if "pin_en" in board.devices["GPS"]: codeOutDevicePin("GPS", "pin_en", "GPS_PIN_EN")
  codeOutDevicePins("GPS", "GPS")

if "ACCEL" in board.devices:
  codeOut("#define ACCEL_DEVICE \""+board.devices["ACCEL"]["device"].upper()+"\"")
  codeOut("#define ACCEL_ADDR "+str(board.devices["ACCEL"]["addr"]))
  codeOutDevicePins("ACCEL", "ACCEL")

if "MAG" in board.devices:
  codeOut("#define MAG_DEVICE \""+board.devices["MAG"]["device"].upper()+"\"")
  if "addr" in board.devices["MAG"]:
    codeOut("#define MAG_ADDR "+str(board.devices["MAG"]["addr"]))
  codeOutDevicePins("MAG", "MAG")

if "TEMP" in board.devices:
  if "addr" in board.devices["TEMP"]:
    codeOut("#define TEMP_ADDR "+str(board.devices["TEMP"]["addr"]))
  codeOutDevicePins("TEMP", "TEMP")

if "PRESSURE" in board.devices:
  codeOut("#define PRESSURE_DEVICE \""+board.devices["PRESSURE"]["device"].upper()+"\"")
  codeOut("#define PRESSURE_ADDR "+str(board.devices["PRESSURE"]["addr"]))
  codeOutDevicePins("PRESSURE", "PRESSURE")

if "SPIFLASH" in board.devices:
  codeOut("#define SPIFLASH_BASE 0x40000000UL")
  codeOut("#define SPIFLASH_PAGESIZE 4096")
  codeOut("#define SPIFLASH_LENGTH "+str(board.devices["SPIFLASH"]["size"]))
  codeOutDevicePins("SPIFLASH", "SPIFLASH")

#for device in ["USB","SD","LCD","JTAG","ESP8266","IR","GPS","ACCEL","MAG","TEMP","PRESSURE","SPIFLASH"]:
for device in ["USB","SD","LCD","JTAG","ESP8266","IR"]:
  if device in board.devices:
    for entry in board.devices[device]:
      if entry[:3]=="pin": usedPinChecks.append("(PIN)==" + toPinDef(board.devices[device][entry])+"/* "+device+" */")


# Dump pin definitions
for p in pins:
    rawName = p['name'].lstrip('P')
    codeOut('#define {0}_PININDEX {1}'.format(rawName, toPinDef(rawName)))

# Specific hacks for nucleo boards
if "NUCLEO_A" in board.devices:
  for n,pin in enumerate(board.devices["NUCLEO_A"]):
      codeOut("#define NUCLEO_A"+str(n)+" "+toPinDef(pin))
if "NUCLEO_D" in board.devices:
  for n,pin in enumerate(board.devices["NUCLEO_D"]):
      codeOut("#define NUCLEO_D"+str(n)+" "+toPinDef(pin))

if "ESP8266" in board.devices:
  for entry in board.devices["ESP8266"]:
    if entry[0:4]=="pin_":
      codeOut("#define ESP8266_"+str(entry[4:].upper())+" "+toPinDef(board.devices["ESP8266"][entry]))

codeOut("")

codeOut("// definition to avoid compilation when Pin/platform config is not defined")
codeOut("#define IS_PIN_USED_INTERNALLY(PIN) (("+")||(".join(usedPinChecks)+"))")
codeOut("#define IS_PIN_A_LED(PIN) (("+")||(".join(ledChecks)+"))")
codeOut("#define IS_PIN_A_BUTTON(PIN) (("+")||(".join(btnChecks)+"))")


codeOut("""
#endif // _PLATFORM_CONFIG_H
""");
