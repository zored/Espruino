/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2019 Gordon Williams <gw@pur3.co.uk>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * ----------------------------------------------------------------------------
 * Simple jshardware replacement functions
 * ----------------------------------------------------------------------------
 */


#include "platform_config.h"
#include "jspininfo.h"
#include "nrf_gpio.h"
#include "nrf_delay.h"


// Using a macro means we hard-code values from pinInfo, and can ditch the pinInfo array
#define jshPinSetValue(PIN,value) \
  nrf_gpio_pin_write((uint32_t)pinInfo[PIN].pin, value ^ ((pinInfo[PIN].port&JSH_PIN_NEGATED)!=0))

#define jshPinGetValue(PIN) \
  (nrf_gpio_pin_read(pinInfo[PIN].pin) ^ ((pinInfo[PIN].port&JSH_PIN_NEGATED)!=0))

#define jshPinOutput(PIN,value) \
  if (pinInfo[PIN].port&JSH_PIN_NEGATED) \
    nrf_gpio_pin_write_output((uint32_t)pinInfo[PIN].pin, value==0); \
  else \
    nrf_gpio_pin_write_output((uint32_t)pinInfo[PIN].pin, value!=0);

#define jshDelayMicroseconds(US) \
  nrf_delay_us(US)

static void __attribute__((noinline)) nrf_gpio_pin_write_output(uint32_t pin, bool value)
{
  nrf_gpio_pin_write(pin, value);
  nrf_gpio_cfg_output(pin);
}

static void set_led_state(bool btn, bool progress)
{
#if defined(PIXLJS) || defined(BANGLEJS)
  // LED1 is backlight/HRM - don't use it!
#else
#if defined(LED2_PININDEX) && defined(LED3_PININDEX)
  jshPinOutput(LED3_PININDEX, progress);
  jshPinOutput(LED2_PININDEX, btn);
#elif defined(LED1_PININDEX)
  jshPinOutput(LED1_PININDEX, progress || btn);
#endif
#endif
}

static bool get_btn1_state() {
  return jshPinGetValue(BTN1_PININDEX)==BTN1_ONSTATE;
}
#ifdef BTN2_PININDEX
static bool get_btn2_state() {
  return jshPinGetValue(BTN2_PININDEX)==BTN2_ONSTATE;
}
#endif

static void hardware_init(void) {
#if defined(PIXLJS)
  // LED1 is backlight - don't use it, but ensure it's off
  jshPinOutput(LED1_PININDEX, 0);
#endif
  set_led_state(false, false);

  bool polarity;
  uint32_t pin;
  if (pinInfo[BTN1_PININDEX].port&JSH_PIN_NEGATED)
    polarity = BTN1_ONSTATE!=1;
  else
    polarity = BTN1_ONSTATE==1;
  pin = pinInfo[BTN1_PININDEX].pin;
  nrf_gpio_cfg_input(pin,
          polarity ? NRF_GPIO_PIN_PULLDOWN : NRF_GPIO_PIN_PULLUP);
#ifdef BTN2_PININDEX
  if (pinInfo[BTN2_PININDEX].port&JSH_PIN_NEGATED)
    polarity = BTN2_ONSTATE!=1;
  else
    polarity = BTN2_ONSTATE==1;
  pin = pinInfo[BTN2_PININDEX].pin;
  nrf_gpio_cfg_input(pin,
          polarity ? NRF_GPIO_PIN_PULLDOWN : NRF_GPIO_PIN_PULLUP);
#endif
#ifdef VIBRATE_PIN
  jshPinOutput(VIBRATE_PIN,0); // vibrate off
#endif
}
