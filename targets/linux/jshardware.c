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
 * Platform Specific part of Hardware interface Layer
 * ----------------------------------------------------------------------------
 */
 #include <stdlib.h>
 #include <string.h>
 #include <stdio.h>
 #include <unistd.h>
 #include <sys/time.h>
#ifdef __MINGW32__
 #include <conio.h>
#else//!__MINGW32__
 #include <sys/select.h>
 #include <termios.h>
 #include <fcntl.h>
#endif//__MINGW32__
 #include <signal.h>
 #include <inttypes.h>

#include "platform_config.h"
#include "jshardware.h"
#include "jsutils.h"
#include "jsparse.h"
#include "jsinteractive.h"

#include <pthread.h>

#define FAKE_FLASH_FILENAME  "espruino.flash"
#define FAKE_FLASH_BLOCKSIZE FLASH_PAGE_SIZE
#define FAKE_FLASH_BLOCKS    (FLASH_TOTAL/FLASH_PAGE_SIZE)

#ifdef DEBUG
#define FAKE_FLASH_DBG(...) jsiConsolePrintf(__VA_ARGS__)
#else
#define FAKE_FLASH_DBG(...)
#endif

#ifndef FLASH_64BITS_ALIGNMENT
#define FLASH_UNITARY_WRITE_SIZE 4
#else
#define FLASH_UNITARY_WRITE_SIZE 8
#endif



#ifdef USE_WIRINGPI
// see http://wiringpi.com/download-and-install/
//   git clone git://git.drogon.net/wiringPi
//   cd wiringPi;./build
#include <wiringPi.h>

 #ifdef SYSFS_GPIO_DIR
  #error USE_WIRINGPI and SYSFS_GPIO_DIR can not coexist
 #endif
#endif

// ----------------------------------------------------------------------------
int ioDevices[EV_DEVICE_MAX+1]; // list of open IO devices (or 0)
JshPinState gpioState[JSH_PIN_COUNT]; // will be set to UNDEFINED if it isn't exported

#ifdef SYSFS_GPIO_DIR

#include <unistd.h>
#include <errno.h>

bool gpioShouldWatch[JSH_PIN_COUNT]; // whether we should watch this pin for changes
bool gpioLastState[JSH_PIN_COUNT]; // the last state of this pin


// functions for accessing the sysfs GPIO
void sysfs_write(const char *path, const char *data) {
/*  jsiConsolePrint(path);
  jsiConsolePrint(" = '");
  jsiConsolePrint(data);
  jsiConsolePrint("'\n");*/
  int f = open(path, O_WRONLY);
  if (f>=0) {
    write(f, data, strlen(data));
    close(f);
  } 
}

void sysfs_write_int(const char *path, JsVarInt val) {
  char buf[20];
  itostr(val, buf, 10);
  sysfs_write(path, buf);
}

void sysfs_read(const char *path, char *data, unsigned int len) {
  int amt = 0;
  int f = open(path, O_RDONLY);
  if (f>=0) {
    amt = read(f, data, len-1);
    close(f);
  } 
  if (amt<0) amt=0;
  data[amt]=0;
}

JsVarInt sysfs_read_int(const char *path) {
  char buf[20];
  sysfs_read(path, buf, sizeof(buf));
  return stringToIntWithRadix(buf, 10, NULL, NULL);
}
#endif

// ----------------------------------------------------------------------------
#ifdef USE_WIRINGPI
#if EXTI_COUNT < 16
#error EXTI_COUNT needs to be 16 or above for WiringPi
#endif

void irqEXTI0() { jshPushIOWatchEvent(EV_EXTI0); }
void irqEXTI1() { jshPushIOWatchEvent(EV_EXTI0+1); }
void irqEXTI2() { jshPushIOWatchEvent(EV_EXTI0+2); }
void irqEXTI3() { jshPushIOWatchEvent(EV_EXTI0+3); }
void irqEXTI4() { jshPushIOWatchEvent(EV_EXTI0+4); }
void irqEXTI5() { jshPushIOWatchEvent(EV_EXTI0+5); }
void irqEXTI6() { jshPushIOWatchEvent(EV_EXTI0+6); }
void irqEXTI7() { jshPushIOWatchEvent(EV_EXTI0+7); }
void irqEXTI8() { jshPushIOWatchEvent(EV_EXTI0+8); }
void irqEXTI9() { jshPushIOWatchEvent(EV_EXTI0+9); }
void irqEXTI10() { jshPushIOWatchEvent(EV_EXTI0+10); }
void irqEXTI11() { jshPushIOWatchEvent(EV_EXTI0+11); }
void irqEXTI12() { jshPushIOWatchEvent(EV_EXTI0+12); }
void irqEXTI13() { jshPushIOWatchEvent(EV_EXTI0+13); }
void irqEXTI14() { jshPushIOWatchEvent(EV_EXTI0+14); }
void irqEXTI15() { jshPushIOWatchEvent(EV_EXTI0+15); }
void irqEXTIDoNothing() { }

void (*irqEXTIs[16])(void) = {
    irqEXTI0,
    irqEXTI1,
    irqEXTI2,
    irqEXTI3,
    irqEXTI4,
    irqEXTI5,
    irqEXTI6,
    irqEXTI7,
    irqEXTI8,
    irqEXTI9,
    irqEXTI10,
    irqEXTI11,
    irqEXTI12,
    irqEXTI13,
    irqEXTI14,
    irqEXTI15,
};
#endif
// ----------------------------------------------------------------------------
IOEventFlags gpioEventFlags[JSH_PIN_COUNT];

IOEventFlags pinToEVEXTI(Pin pin) {
  return gpioEventFlags[pin];
}

IOEventFlags getNewEVEXTI() {
  int i;
  for (i=0;i<16;i++) {
    IOEventFlags evFlag = (IOEventFlags)(EV_EXTI0+i);
    Pin pin;
    bool found = false;
    for (pin=0;pin<JSH_PIN_COUNT;pin++)
      if (gpioEventFlags[pin] == evFlag)
        found = true;
    if (!found)
      return evFlag;
  }
  return 0;
}

// Get the path associated with a device. Returns false on failure.
bool jshGetDevicePath(IOEventFlags device, char *buf, size_t bufSize) {
  JsVar *obj = jshGetDeviceObject(device);
  if (!obj) return false;

  bool success = false;
  JsVar *str = jsvObjectGetChild(obj, "path", 0);
  if (jsvIsString(str)) {
    jsvGetString(str, buf, bufSize);
    success = true;
  }
  jsvUnLock2(str, obj);
  return success;
}



// ----------------------------------------------------------------------------
// for non-blocking IO
#ifdef __MINGW32__
void reset_terminal_mode() {}
void set_conio_terminal_mode() {}
int kbhit()
{
  return _kbhit();
}

int getch()
{
  return _getch();
}

#else//!__MINGW32__
struct termios orig_termios;
static int terminal_set = 0;

void reset_terminal_mode()
{

    tcsetattr(0, TCSANOW, &orig_termios);
}

void set_conio_terminal_mode()
{
    struct termios new_termios;

    /* take two copies - one for now, one for later */
    tcgetattr(0, &orig_termios);
    memcpy(&new_termios, &orig_termios, sizeof(new_termios));

    /* register cleanup handler, and set the new terminal mode */
    atexit(reset_terminal_mode);
    cfmakeraw(&new_termios);
    tcsetattr(0, TCSANOW, &new_termios);
}

int kbhit()
{
    struct timeval tv = { 0L, 0L };
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
}

int getch()
{
    int r;
    unsigned char c;
    if ((r = (int)read(STDIN_FILENO, &c, sizeof(c))) < 0) {
        return r;
    } else {
        return c;
    }
}
#endif//__MINGW32__

pthread_t inputThread;
bool isInitialised;

void jshInputThread() {
  while (isInitialised) {
    bool shortSleep = false;
    /* Handle the delayed Ctrl-C -> interrupt behaviour (see description by EXEC_CTRL_C's definition)  */
    if (execInfo.execute & EXEC_CTRL_C_WAIT)
      execInfo.execute = (execInfo.execute & ~EXEC_CTRL_C_WAIT) | EXEC_INTERRUPTED;
    if (execInfo.execute & EXEC_CTRL_C)
      execInfo.execute = (execInfo.execute & ~EXEC_CTRL_C) | EXEC_CTRL_C_WAIT;
    // Read from the console if we have space
    while (kbhit() && (jshGetEventsUsed()<IOBUFFERMASK/2)) {
      int ch = getch();
      if (ch<0) break;
      jshPushIOCharEvent(EV_USBSERIAL, (char)ch);
    }
    // Read from any open devices - if we have space
    if (jshGetEventsUsed() < IOBUFFERMASK/2) {
      int i;
      for (i=0;i<=EV_DEVICE_MAX;i++) {
        if (ioDevices[i]) {
          char buf[32];
          // read can return -1 (EAGAIN) because O_NONBLOCK is set
          int bytes = (int)read(ioDevices[i], buf, sizeof(buf));
          if (bytes>0) {
            //int j; for (j=0;j<bytes;j++) printf("]] '%c'\r\n", buf[j]);
            jshPushIOCharEvents(i, buf, (unsigned int)bytes);
            shortSleep = true;
          }
        }
      }
    }
    // Write any data we have
    IOEventFlags device = jshGetDeviceToTransmit();
    while (device != EV_NONE) {
      char ch = (char)jshGetCharToTransmit(device);
      //printf("[[ '%c'\r\n", ch);
      if (ioDevices[device]) {
        write(ioDevices[device], &ch, 1);
        shortSleep = true;
      }
      device = jshGetDeviceToTransmit();
    }


#ifdef SYSFS_GPIO_DIR
    Pin pin;
    for (pin=0;pin<JSH_PIN_COUNT;pin++)
      if (gpioShouldWatch[pin]) {
        shortSleep = true;
        bool state = jshPinGetValue(pin);
        if (state != gpioLastState[pin]) {
          jshPushIOEvent(pinToEVEXTI(pin) | (state?EV_EXTI_IS_HIGH:0), jshGetSystemTime());
          gpioLastState[pin] = state;
        }
      }
#endif

    jshDelayMicroseconds(shortSleep ? 1000 : 50000);
  }
}



void jshInit() {

#ifdef USE_WIRINGPI
  if (geteuid() == 0) {
    printf("RUNNING AS SUDO - awesome. You'll get proper IRQ support\n");
    wiringPiSetupGpio();
  } else {
    printf("NOT RUNNING AS SUDO - sorry, you'll get rubbish realtime IO. You also need to export the pins you want to use first.\n");
    wiringPiSetupSys() ;
  }
#endif

  int i;
  for (i=0;i<=EV_DEVICE_MAX;i++)
    ioDevices[i] = 0;

  jshInitDevices();
#ifndef __MINGW32__
  if (!terminal_set) {
    struct termios new_termios;

    /* take two copies - one for now, one for later */
    tcgetattr(0, &orig_termios);
    memcpy(&new_termios, &orig_termios, sizeof(new_termios));

    /* register cleanup handler, and set the new terminal mode */
    atexit(reset_terminal_mode);
    cfmakeraw(&new_termios);
    tcsetattr(0, TCSANOW, &new_termios);
    terminal_set = 1;
  }
#endif//!__MINGW32__
  for (i=0;i<JSH_PIN_COUNT;i++) {
    gpioState[i] = JSHPINSTATE_UNDEFINED;
    gpioEventFlags[i] = 0;
  }
#ifdef SYSFS_GPIO_DIR
  for (i=0;i<JSH_PIN_COUNT;i++) {
    gpioShouldWatch[i] = false;    
  }
#endif

  isInitialised = true;
  int err = pthread_create(&inputThread, NULL, &jshInputThread, NULL);
  if (err != 0)
      printf("Unable to create input thread, %s", strerror(err));
}

void jshReset() {
  jshResetDevices();
}

void jshKill() {
  int i;

  // Request that the input thread finishes
  isInitialised = false;
  // wait for thread to finish
  pthread_join(inputThread, NULL);

  for (i=0;i<=EV_DEVICE_MAX;i++)
    if (ioDevices[i]) {
      close(ioDevices[i]);
      ioDevices[i]=0;
    }

#ifdef SYSFS_GPIO_DIR

  // unexport any GPIO that we exported
  for (i=0;i<JSH_PIN_COUNT;i++)
    if (gpioState[i] != JSHPINSTATE_UNDEFINED)
      sysfs_write_int(SYSFS_GPIO_DIR"/unexport", i);
#endif
}

void jshIdle() {
  // all done in the thread now...
}

// ----------------------------------------------------------------------------

int jshGetSerialNumber(unsigned char *data, int maxChars) {
  long initialSerial = 0;
  long long serial = 0xDEADDEADDEADDEADL; 
  FILE *f = fopen("/proc/cpuinfo", "r");
  if (f) {
    char line[256]; 
    while (fgets(line, 256, f)) {
      if (strncmp(line, "Serial", 6) == 0) {
        char serial_string[16 + 1];
        strcpy(serial_string, strchr(line, ':') + 2);
        serial = stringToIntWithRadix(serial_string, 16, NULL, NULL);
      }
    }
    fclose(f);
  }
  memcpy(&data[0], &initialSerial, 4);
  memcpy(&data[4], &serial, 8);
  return 12;
}

unsigned int jshGetRegistrationCode() {
  unsigned int code = 0xFFFFFFFF;
  FILE *f = fopen("espruino.code","rb");
  if (f) {
    fread(&code,1,4,f);
    fclose(f);
  }
  return code;
}

void jshSetRegistrationCode(unsigned int code) {
  FILE *f = fopen("espruino.code","wb");
  if (f) {
    fwrite(&code,1,4,f);
    fclose(f);
  }
}

// ----------------------------------------------------------------------------

void jshInterruptOff() {
}

void jshInterruptOn() {
}

/// Are we currently in an interrupt?
bool jshIsInInterrupt() {
  return false; // or check if we're in the IO handling thread?
}

void jshDelayMicroseconds(int microsec) {
  struct timeval tv;
  tv.tv_sec  = microsec / 1000000;
  tv.tv_usec = (suseconds_t) microsec % 1000000;
  select( 0, NULL, NULL, NULL, &tv );
}

void jshPinSetState(Pin pin, JshPinState state) {
#ifdef SYSFS_GPIO_DIR
  if (gpioState[pin] != state) {
    if (gpioState[pin] == JSHPINSTATE_UNDEFINED)
      sysfs_write_int(SYSFS_GPIO_DIR"/export", pin);
    char path[64] = SYSFS_GPIO_DIR"/gpio";
    itostr(pin, &path[strlen(path)], 10);
    strcat(&path[strlen(path)], "/direction");
    sysfs_write(path, JSHPINSTATE_IS_OUTPUT(state)?"out":"in");
  }
#endif
#ifdef USE_WIRINGPI
  if (JSHPINSTATE_IS_OUTPUT(state)) {
    if (state==JSHPINSTATE_AF_OUT || state==JSHPINSTATE_AF_OUT_OPENDRAIN)
      pinMode(pin,PWM_OUTPUT);
    else
      pinMode(pin,OUTPUT);
  } else {
    pinMode(pin,INPUT);
    if (state==JSHPINSTATE_GPIO_IN_PULLUP)
      pullUpDnControl (pin, PUD_UP) ;
    else if (state==JSHPINSTATE_GPIO_IN_PULLDOWN)
      pullUpDnControl (pin, PUD_DOWN) ;
    else
      pullUpDnControl (pin, PUD_OFF) ;
  }
#endif
  gpioState[pin] = state;
}

JshPinState jshPinGetState(Pin pin) {
  return gpioState[pin];
}

void jshPinSetValue(Pin pin, bool value) {
#ifdef SYSFS_GPIO_DIR
  char path[64] = SYSFS_GPIO_DIR"/gpio";
  itostr(pin, &path[strlen(path)], 10);
  strcat(&path[strlen(path)], "/value");
  sysfs_write_int(path, value?1:0);
#endif
#ifdef USE_WIRINGPI
  digitalWrite(pin,value);
#endif
}

bool jshPinGetValue(Pin pin) {
#ifdef SYSFS_GPIO_DIR
  char path[64] = SYSFS_GPIO_DIR"/gpio";
  itostr(pin, &path[strlen(path)], 10);
  strcat(&path[strlen(path)], "/value");
  return sysfs_read_int(path);
#elif defined(USE_WIRINGPI)
  return digitalRead(pin);
#else
  return false;
#endif
}

bool jshIsDeviceInitialised(IOEventFlags device) { return true; }

bool jshIsUSBSERIALConnected() {
  return false;
}

JsSysTime jshGetTimeFromMilliseconds(JsVarFloat ms) {
  return (JsSysTime)(ms*1000);
}

JsVarFloat jshGetMillisecondsFromTime(JsSysTime time) {
  return ((JsVarFloat)time)/1000;
}

#ifdef USE_WIRINGPI
JsSysTime baseSystemTime = 0;
#endif

JsSysTime jshGetSystemTime() {
#ifdef USE_WIRINGPI
  /* use micros, and cope with wrapping...
   basically we're going to use getTime more often than once every 71 mins
   (the time it takes to wrap) so we can just cope with wraps be seeing if
   it has wrapped, and incrementing a bigger counter */
  unsigned int us = micros();
  static unsigned int lastUs = 0;
  if (us<lastUs) baseSystemTime += 0x100000000LL;
  lastUs = us;
  return baseSystemTime + (JsSysTime)us;
#else
  struct timeval tm;
  gettimeofday(&tm, 0);
  return (JsSysTime)(tm.tv_sec)*1000000L + tm.tv_usec;
#endif
}

void jshSetSystemTime(JsSysTime time) {
#ifdef USE_WIRINGPI
  baseSystemTime = time - micros();
#endif
}

// ----------------------------------------------------------------------------

JsVarFloat jshPinAnalog(Pin pin) {
  JsVarFloat value = 0;
  jsError("Analog is not supported on this device.");
  return value;
}

int jshPinAnalogFast(Pin pin) {
  return 0;
}

JshPinFunction jshPinAnalogOutput(Pin pin, JsVarFloat value, JsVarFloat freq, JshAnalogOutputFlags flags) { // if freq<=0, the default is used
#ifdef USE_WIRINGPI
  // todo pwmSetRange and pwmSetClock for freq?
  int v = (int)(value*1024);
  if (v<0) v=0;
  if (v>1023) v=1023;
  jshPinSetState(pin, JSHPINSTATE_AF_OUT);
  pwmWrite(pin, (int)(value*1024));
#endif
  return JSH_NOTHING;
}

void jshPinPulse(Pin pin, bool value, JsVarFloat time) {
  if (jshIsPinValid(pin)) {
    jshPinSetState(pin, JSHPINSTATE_GPIO_OUT);
    jshPinSetValue(pin, value);
    jshDelayMicroseconds(time*1000000);
    jshPinSetValue(pin, !value);
  } else jsError("Invalid pin!");
}

bool jshCanWatch(Pin pin) {
  if (jshIsPinValid(pin)) {
     IOEventFlags exti = getNewEVEXTI();
     if (exti) return true;
     return false;
  } else
    return false;
}

IOEventFlags jshPinWatch(Pin pin, bool shouldWatch) {
  if (jshIsPinValid(pin)) {
    IOEventFlags exti = getNewEVEXTI();
    if (shouldWatch) {
      if (exti) {
        gpioEventFlags[pin] = exti;
        jshPinSetState(pin, JSHPINSTATE_GPIO_IN);
#ifdef SYSFS_GPIO_DIR
        gpioShouldWatch[pin] = true;
        gpioLastState[pin] = jshPinGetValue(pin);
#endif
#ifdef USE_WIRINGPI
        wiringPiISR(pin, INT_EDGE_BOTH, irqEXTIs[exti-EV_EXTI0]);
#endif
      } else 
        jsError("You can only have a maximum of 16 watches!");
    }
    if (!shouldWatch || !exti) {
      gpioEventFlags[pin] = 0;
#ifdef SYSFS_GPIO_DIR
      gpioShouldWatch[pin] = false;
#endif
#ifdef USE_WIRINGPI
      wiringPiISR(pin, INT_EDGE_BOTH, irqEXTIDoNothing);
#endif

    }
    return shouldWatch ? exti : EV_NONE;
  } else jsError("Invalid pin!");
  return EV_NONE;
}

bool jshGetWatchedPinState(IOEventFlags device) {
  Pin i;
  for (i=0;i<JSH_PIN_COUNT;i++)
    if (gpioEventFlags[i]==device)
      return jshPinGetValue(i);
  return false;
}

bool jshIsEventForPin(IOEvent *event, Pin pin) {
  return IOEVENTFLAGS_GETTYPE(event->flags) == pinToEVEXTI(pin);
}

void jshUSARTSetup(IOEventFlags device, JshUSARTInfo *inf) {
  assert(DEVICE_IS_USART(device));
  if (ioDevices[device]) close(ioDevices[device]);
  ioDevices[device] = 0;
  
  if (inf->errorHandling) {
    jsExceptionHere(JSET_ERROR, "Linux Espruino builds can't handle framing/parity errors (errors:true)");
    return;
  } 
  
  char path[256];
  if (jshGetDevicePath(device, path, sizeof(path))) {
    ioDevices[device] = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (!ioDevices[device]) {
      jsError("Open of path %s failed", path);
    } else {
      struct termios settings;
      tcgetattr(ioDevices[device], &settings); // get current settings

      int baud = 0;
      switch (inf->baudRate) {
        case 50      : baud = B50     ;break;
        case 75      : baud = B75     ;break;
        case 110     : baud = B110    ;break;
        case 134     : baud = B134    ;break;
        case 150     : baud = B150    ;break;
        case 200     : baud = B200    ;break;
        case 300     : baud = B300    ;break;
        case 600     : baud = B600    ;break;
        case 1200    : baud = B1200   ;break;
        case 1800    : baud = B1800   ;break;
        case 2400    : baud = B2400   ;break;
        case 4800    : baud = B4800   ;break;
        case 9600    : baud = B9600   ;break;
        case 19200   : baud = B19200  ;break;
        case 38400   : baud = B38400  ;break;
        case 57600   : baud = B57600  ;break;
        case 115200  : baud = B115200 ;break;
        case 230400  : baud = B230400 ;break;
#ifndef __MACOSX__
        case 460800  : baud = B460800 ;break;
        case 500000  : baud = B500000 ;break;
        case 576000  : baud = B576000 ;break;
        case 921600  : baud = B921600 ;break;
        case 1000000 : baud = B1000000;break;
        case 1152000 : baud = B1152000;break;
        case 1500000 : baud = B1500000;break;
        case 2000000 : baud = B2000000;break;
        case 2500000 : baud = B2500000;break;
        case 3000000 : baud = B3000000;break;
        case 3500000 : baud = B3500000;break;
        case 4000000 : baud = B4000000;break;
#endif
      }

      if (baud) {
        cfsetispeed(&settings, baud); // set baud rates
        cfsetospeed(&settings, baud);

        // raw mode
        cfmakeraw(&settings);

        settings.c_cflag &= ~(PARENB|PARODD); // none
        
        if (inf->parity == 1) settings.c_cflag |= PARENB|PARODD; // odd
        if (inf->parity == 2) settings.c_cflag |= PARENB; // even
        
        settings.c_cflag &= ~CSTOPB;
        if (inf->stopbits==2) settings.c_cflag |= CSTOPB;

        settings.c_cflag &= ~CSIZE;

        switch (inf->bytesize) {
          case 5 : settings.c_cflag |= CS5; break;
          case 6 : settings.c_cflag |= CS6; break;
          case 7 : settings.c_cflag |= CS7; break;
          case 8 : settings.c_cflag |= CS8; break;
        }

        // finally set current settings
        tcsetattr(ioDevices[device], TCSANOW, &settings);
      } else {
        jsError("No baud rate defined for device");
      }
    }
  } else {
    jsError("No path defined for device");
  }
}

/** Kick a device into action (if required). For instance we may need
 * to set up interrupts */
void jshUSARTKick(IOEventFlags device) {
  assert(DEVICE_IS_USART(device) || DEVICE_IS_SPI(device));
  // all done by the idle loop
}

void jshSPISetup(IOEventFlags device, JshSPIInfo *inf) {
  assert(DEVICE_IS_SPI(device));
   if (ioDevices[device]) close(ioDevices[device]);
   ioDevices[device] = 0;
   char path[256];
   if (jshGetDevicePath(device, path, sizeof(path))) {
     ioDevices[device] = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
     if (!ioDevices[device]) {
       jsError("Open of path %s failed", path);
     } else {
     }
   } else {
     jsError("No path defined for device");
   }
}

/** Send data through the given SPI device (if data>=0), and return the result
 * of the previous send (or -1). If data<0, no data is sent and the function
 * waits for data to be returned */
int jshSPISend(IOEventFlags device, int data) {
  jshTransmit(device, (unsigned char)data);
  // FIXME
  // use jshPopIOEventOfType(device) but be aware that it may return >1 char!
  return -1;
}

/** Send 16 bit data through the given SPI device. */
void jshSPISend16(IOEventFlags device, int data) {
  jshSPISend(device, data>>8);
  jshSPISend(device, data&255);
}

/** Set whether to send 16 bits or 8 over SPI */
void jshSPISet16(IOEventFlags device, bool is16) {
}

/** Set whether to use the receive interrupt or not */
void jshSPISetReceive(IOEventFlags device, bool isReceive) {
}

/** Wait until SPI send is finished, */
void jshSPIWait(IOEventFlags device) {
}

void jshI2CSetup(IOEventFlags device, JshI2CInfo *inf) {
}

void jshI2CWrite(IOEventFlags device, unsigned char address, int nBytes, const unsigned char *data, bool sendStop) {
}

void jshI2CRead(IOEventFlags device, unsigned char address, int nBytes, unsigned char *data, bool sendStop) {
}

/// Enter simple sleep mode (can be woken up by interrupts). Returns true on success
bool jshSleep(JsSysTime timeUntilWake) {
  bool hasWatches = false;
#ifdef SYSFS_GPIO_DIR
  Pin pin;
  for (pin=0;pin<JSH_PIN_COUNT;pin++)
    if (gpioShouldWatch[pin]) hasWatches = true;
#endif
 
  JsVarFloat usecfloat = jshGetMillisecondsFromTime(timeUntilWake)*1000;
  unsigned int usecs = (usecfloat < 0xFFFFFFFF) ? (unsigned int)usecfloat : 0xFFFFFFFF;
  if (hasWatches && usecs>1000) 
    usecs=1000; // don't sleep much if we have watches - we need to keep polling them
  if (usecs > 50000)
    usecs = 50000; // don't want to sleep too much (user input/HTTP/etc)
  if (usecs >= 1000)  
    jshDelayMicroseconds(usecs);
  return true;
}

void jshUtilTimerDisable() {
}

void jshUtilTimerReschedule(JsSysTime period) {
}

void jshUtilTimerStart(JsSysTime period) {
}

JshPinFunction jshGetCurrentPinFunction(Pin pin) {
  return JSH_NOTHING;
}

void jshSetOutputValue(JshPinFunction func, int value) {
}

void jshEnableWatchDog(JsVarFloat timeout) {
}

void jshKickWatchDog() {
}

JsVarFloat jshReadTemperature() { return NAN; };
JsVarFloat jshReadVRef()  { return NAN; };
unsigned int jshGetRandomNumber() { return rand(); }

bool jshFlashGetPage(uint32_t addr, uint32_t *startAddr, uint32_t *pageSize) {
  if (addr < FLASH_START)
    return false;
  if (addr >= FLASH_START+(FAKE_FLASH_BLOCKSIZE * FAKE_FLASH_BLOCKS))
      return false;
  *startAddr = (uint32_t)(floor(addr / FAKE_FLASH_BLOCKSIZE) * FAKE_FLASH_BLOCKSIZE);
  *pageSize = FAKE_FLASH_BLOCKSIZE;
  return true;
}
JsVar *jshFlashGetFree() {
  JsVar *jsFreeFlash = jsvNewEmptyArray();
  if (!jsFreeFlash) return 0;
  uint32_t pAddr, pSize;
  JsVar *jsArea = jsvNewObject();
  if (!jsArea) return jsFreeFlash;
  jsvObjectSetChildAndUnLock(jsArea, "addr", jsvNewFromInteger(FLASH_START));
  jsvObjectSetChildAndUnLock(jsArea, "length", jsvNewFromInteger(FAKE_FLASH_BLOCKSIZE*FAKE_FLASH_BLOCKS));
  jsvArrayPushAndUnLock(jsFreeFlash, jsArea);
  return jsFreeFlash;
}

static FILE *jshFlashOpenFile(bool dontCreate) {
  FILE *f = fopen(FAKE_FLASH_FILENAME, "r+b");
  if (!f && dontCreate) return 0;
  if (!f) f = fopen(FAKE_FLASH_FILENAME, "wb");
  if (!f) return 0;
  int len = FAKE_FLASH_BLOCKSIZE*FAKE_FLASH_BLOCKS;
  fseek(f,0,SEEK_END);
  long filelen = ftell(f);
  if (filelen<len) {
    long pad = len-filelen;
    char *buf = malloc(pad);
    memset(buf,0xFF, pad);
    fwrite(buf, 1, pad, f);
    free(buf);
    fclose(f);
    f = fopen(FAKE_FLASH_FILENAME, "r+b");
  }
  return f;
}
void jshFlashErasePage(uint32_t addr) {
  FAKE_FLASH_DBG("FlashErasePage 0x%08x\n", addr);
  FILE *f = jshFlashOpenFile(true);
  if (!f) return; // if no file and we're erasing, we don't have to do anything
  uint32_t startAddr, pageSize;
  if (jshFlashGetPage(addr, &startAddr, &pageSize)) {
    startAddr -= FLASH_START;
    fseek(f, startAddr, SEEK_SET);
    char *buf = malloc(pageSize);
    memset(buf, 0xFF, pageSize);
    fwrite(buf, 1, pageSize, f);
    free(buf);
  }
  fclose(f);
}
void jshFlashRead(void *buf, uint32_t addr, uint32_t len) {
  FAKE_FLASH_DBG("FlashRead 0x%08x %d\n", addr,len);
  //assert(!(addr&(FLASH_UNITARY_WRITE_SIZE-1))); // sanity checks here to mirror real hardware
  //assert(!(len&(FLASH_UNITARY_WRITE_SIZE-1))); // sanity checks here to mirror real hardware
  if (addr<FLASH_START || addr>=FLASH_START+FLASH_TOTAL) {
    assert(0); // out of range
    return;
  }
  addr -= FLASH_START;

  FILE *f = jshFlashOpenFile(true);
  if (!f) { // no file, so it's all 0xFF
    memset(buf, 0xFF, len);
    return;
  }
  fseek(f, addr, SEEK_SET);
  size_t r = fread(buf, 1, len, f);
  assert(r==len);
  fclose(f);
}
void jshFlashWrite(void *buf, uint32_t addr, uint32_t len) {
  FAKE_FLASH_DBG("FlashWrite 0x%08x %d\n", addr,len);
  uint32_t i;
#ifndef SPIFLASH_BASE // for debug
  assert(!(addr&(FLASH_UNITARY_WRITE_SIZE-1))); // sanity checks here to mirror real hardware
  assert(!(len&(FLASH_UNITARY_WRITE_SIZE-1))); // sanity checks here to mirror real hardware
#endif

  /*jsiConsolePrintf("%08x ", addr);
  for (i=0;i<len;i++)
    jsiConsolePrintf("%02x ", (int)((unsigned char*)buf)[i]);
  jsiConsolePrintf("\n");*/

  if (addr<FLASH_START || addr>=FLASH_START+FLASH_TOTAL) {
    assert(0); // out of range
    return;
  }
  addr -= FLASH_START;

  FILE *f = jshFlashOpenFile(false);
  if (!f) return;

  char *wbuf = malloc(len);
  int err = fseek(f, addr, SEEK_SET);
  assert(err==0);
  size_t r = fread(wbuf, 1, len, f);
  assert(r==len);

  for (i=0;i<len;i++) {
    //jsiConsolePrintf("Write %d to 0x%08x\n", ((char*)buf)[i], FLASH_START+addr+i);
    wbuf[i] &= ((char*)buf)[i];
  }
  fseek(f, addr, SEEK_SET);
  fwrite(wbuf, 1, len, f);
  free(wbuf);
  //fsync(f);
  fclose(f);
}

// No - we can't memory-map the flash memory under Linux (well, we could but for testing it's handy not to)
size_t jshFlashGetMemMapAddress(size_t ptr) {
  return 0;
}

unsigned int jshSetSystemClock(JsVar *options) {
  return 0;
}

/// Perform a proper hard-reboot of the device
void jshReboot() {
  jsExceptionHere(JSET_ERROR, "Not implemented");
}
