/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2015 Gordon Williams <gw@pur3.co.uk>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * ----------------------------------------------------------------------------
 * This file is designed to be parsed during the build process
 *
 * Contains ESP8266 and Wifi library specific functions.
 *
 * FOR DESCRIPTIONS OF THE WIFI FUNCTIONS IN THIS FILE, SEE
 * libs/network/jswrap_wifi.c (or http://www.espruino.com/Reference#Wifi)
 *
 * IMPORTANT: the functions in this file have tests in ./tests/wifi-test-mode.js
 * please maintain these tests if you make functional changes!
 * ----------------------------------------------------------------------------
 */

// Set WIFI_DBG to 0 to disable debug printf's, to 1 for important printf's, to 2 for verbose
//#ifdef RELEASE
//#define WIFI_DBG 0
//#else
//#define WIFI_DBG 1
//#endif
#ifdef RELEASE
  #define DBG(format, ...) do { } while(0)
  #define DBGV(format, ...) do { } while(0)
#else
  // Normal debug
  #if WIFI_DBG > 0
    #define DBG(format, ...) os_printf(format, ## __VA_ARGS__)
  #else
    #define DBG(format, ...) do { } while(0)
  #endif
  // Verbose debug
  #if WIFI_DBG > 1
    #define DBGV(format, ...) os_printf(format, ## __VA_ARGS__)
  #else
    #define DBGV(format, ...) do { } while(0)
  #endif
#endif

// Because the ESP8266 JS wrapper is assured to be running on an ESP8266 we
// can assume that inclusion of ESP8266 headers will be acceptable.
#include <c_types.h>
#include <user_interface.h>
#include <mem.h>
#include "osapi_release.h"
#include <ping.h>
#include <espconn.h>
#include <sntp.h>
#include <espmissingincludes.h>
#include <uart.h>

#define ESP8266_ON_ACCESS_POINTS "#accessPoints"

#define _GCC_WRAP_STDINT_H
typedef long long int64_t;

#include "jswrap_esp8266_network.h"
#include "jswrap_esp8266.h"
#include "jswrap_modules.h"
#include "jswrap_interactive.h"
#include "jsinteractive.h"
#include "network.h"
#include "network_esp8266.h"
#include "jswrap_net.h"
#include "jswrap_storage.h"

//#define jsvUnLock(v) do { os_printf("Unlock %s @%d\n", __STRING(v), __LINE__); jsvUnLock(v); } while(0)

// Forward declaration of functions.
static void scanCB(void *arg, STATUS status);
static void wifiEventHandler(System_Event_t *event);
static void pingRecvCB();
static void startMDNS(char *hostname);
static void stopMDNS();

// Some common error handling

FLASH_STR(expect_cb, "Expecting callback function but got %v");
#define EXPECT_CB_EXCEPTION(jsCB) jsExceptionHere_flash(JSET_ERROR, expect_cb, jsCB)

FLASH_STR(expect_opt, "Expecting options object but got %t");
#define EXPECT_OPT_EXCEPTION(jsOPT) jsExceptionHere_flash(JSET_ERROR, expect_opt, jsOPT)

// #NOTE: For callback functions, be sure and unlock them in the `kill` handler.

// A callback function to be invoked when we find a new access point.
static JsVar *g_jsScanCallback;

// A callback function to be invoked when we have an IP address.
static JsVar *g_jsGotIpCallback;

// A callback function to be invoked on ping responses.
static JsVar *g_jsPingCallback;

// A callback function to be invoked on gethostbyname responses.
static JsVar *g_jsHostByNameCallback;

// A callback function to be invoked on a disconnect response.
static JsVar *g_jsDisconnectCallback;

// Flag to tell the wifi event handler that it should turn STA off on disconnect
static bool g_disconnecting;

// Flag to tell the wifi event handler to ignore the next disconnect event because
// we're provoking it in order to connect to something different
static bool g_skipDisconnect;

// Global data structure for ping request
static struct ping_option pingOpt;

// Global data structure for setIP  and setAPIP 
static struct ip_info info;

// Configuration save to flash
typedef struct {
  uint16_t    length, version;
  uint32_t    crc;
  uint8_t     mode, phyMode;
  uint8_t     sleepType, ssidLen;
  uint8_t     authMode, hidden;
  char        staSsid[32], staPass[64];
  char        apSsid[32], apPass[64];
  char        dhcpHostname[64];
} Esp8266_config;
static Esp8266_config esp8266Config;
static uint8  savedMode = 0;

//===== Mapping from enums to strings

// Reasons for which a connection failed
// (The code here is a bit of a nightmare in order to get the strings into FLASH so they don't
// eat up valuable RAM space. Sadly the FLASH_STR's __attribute__ stuff can't be applied to the
// wifiReasons array as a whole.)
FLASH_STR(__wr0,  "0 - <Not Used>");           // 0
FLASH_STR(__wr1,  "unspecified");              // 1 - REASON_UNSPECIFIED
FLASH_STR(__wr2,  "auth_expire");              // 2 - REASON_AUTH_EXPIRE
FLASH_STR(__wr3,  "auth_leave");               // 3 - REASON_AUTH_LEAVE
FLASH_STR(__wr4,  "assoc_expire");             // 4 - REASON_ASSOC_EXPIRE
FLASH_STR(__wr5,  "assoc_toomany");            // 5 - REASON_ASSOC_TOOMANY
FLASH_STR(__wr6,  "not_authed");               // 6 - REASON_NOT_AUTHED
FLASH_STR(__wr7,  "not_assoced");              // 7 - REASON_NOT_ASSOCED
FLASH_STR(__wr8,  "assoc_leave");              // 8 - REASON_ASSOC_LEAVE
FLASH_STR(__wr9,  "assoc_not_authed");         // 9 - REASON_ASSOC_NOT_AUTHED
FLASH_STR(__wr10, "disassoc_pwrcap_bad");      // 10 - REASON_DISASSOC_PWRCAP_BAD
FLASH_STR(__wr11, "disassoc_supchan_bad");     // 11 - REASON_DISASSOC_SUPCHAN_BAD
FLASH_STR(__wr12, "12 - <Not Used>");          // 12
FLASH_STR(__wr13, "ie_invalid");               // 13 - REASON_IE_INVALID
FLASH_STR(__wr14, "mic_failure");              // 14 - REASON_MIC_FAILURE
FLASH_STR(__wr15, "4way_handshake_timeout");   // 15 - REASON_4WAY_HANDSHAKE_TIMEOUT
FLASH_STR(__wr16, "group_key_update_timeout"); // 16 - REASON_GROUP_KEY_UPDATE_TIMEOUT
FLASH_STR(__wr17, "ie_in_4way_differs");       // 17 - REASON_IE_IN_4WAY_DIFFERS
FLASH_STR(__wr18, "group_cipher_invalid");     // 18 - REASON_GROUP_CIPHER_INVALID
FLASH_STR(__wr19, "pairwise_cipher_invalid");  // 19 - REASON_PAIRWISE_CIPHER_INVALID
FLASH_STR(__wr20, "akmp_invalid");             // 20 - REASON_AKMP_INVALID
FLASH_STR(__wr21, "unsupp_rsn_ie_version");    // 21 - REASON_UNSUPP_RSN_IE_VERSION
FLASH_STR(__wr22, "invalid_rsn_ie_cap");       // 22 - REASON_UNSUPP_RSN_IE_VERSION
FLASH_STR(__wr23, "802_1x_auth_failed");       // 23 - REASON_802_1X_AUTH_FAILED
FLASH_STR(__wr24, "cipher_suite_rejected");    // 24 - REASON_CIPHER_SUITE_REJECTED
FLASH_STR(__wr200, "beacon_timeout");          // 200 - REASON_BEACON_TIMEOUT
FLASH_STR(__wr201, "no_ap_found");             // 201 - REASON_NO_AP_FOUND
FLASH_STR(__wr202, "auth_failed");             // 202 - REASON_AUTH_FAIL
FLASH_STR(__wr203, "assoc_failed");            // 203 - REASON_ASSOC_FAIL
FLASH_STR(__wr204, "handshake_timeout");       // 204 - REASON_HANDSHAKE_TIMEOUT

static const char *wifiReasons[] = {
  __wr0, __wr1, __wr2, __wr3, __wr4, __wr5, __wr6, __wr7, __wr8, __wr9, __wr10,
  __wr11, __wr12, __wr13, __wr14, __wr15, __wr16, __wr17, __wr18, __wr19, __wr20,
  __wr21, __wr22, __wr23, __wr24, __wr200, __wr201, __wr202, __wr203, __wr204
};

static char wifiReasonBuff[sizeof("group_key_update_timeout")+1]; // length of longest string
static char *wifiGetReason(uint8 wifiReason) {
  const char *reason;
  if (wifiReason <= 24) reason = wifiReasons[wifiReason];
  else if (wifiReason >= 200 && wifiReason <= 204) reason = wifiReasons[wifiReason-200+24];
  else reason = wifiReasons[1];
  flash_strncpy(wifiReasonBuff, reason, sizeof(wifiReasonBuff));
  wifiReasonBuff[sizeof(wifiReasonBuff)-1] = 0; // force null termination
  return wifiReasonBuff;
}

// Wifi events
FLASH_STR(__ev0, "#onassociated");
FLASH_STR(__ev1, "#ondisconnected");
FLASH_STR(__ev2, "#onauth_change");
FLASH_STR(__ev3, "#onconnected");
FLASH_STR(__ev4, "#ondhcp_timeout");
FLASH_STR(__ev5, "#onsta_joined");
FLASH_STR(__ev6, "#onsta_left");
FLASH_STR(__ev7, "#onprobe_recv");
static const char *wifi_events[] = { __ev0, __ev1, __ev2, __ev3, __ev4, __ev5, __ev6, __ev7 };
static char wifiEventBuff[sizeof("#ondisconnected")+1]; // length of longest string
static char *wifiGetEvent(uint32 event) {
  flash_strncpy(wifiEventBuff, wifi_events[event], sizeof(wifiEventBuff));
  wifiEventBuff[sizeof(wifiEventBuff)-1] = 0;
  return wifiEventBuff;
}

static char *wifiAuth[] = { "open", "wep", "wpa", "wpa2", "wpa_wpa2" };
static char *wifiMode[] = { "off", "sta", "ap", "sta+ap" };
static char *wifiPhy[]  = { "?", "11b", "11g", "11n" };
static char *wifiConn[] = {
  "off", "connecting", "bad_password", "no_ap_found", "connect_failed", "connected"
};

static char macFmt[] = "%02x:%02x:%02x:%02x:%02x:%02x";

//===== This file contains definitions for two classes: ESP8266 and wifi

/*JSON{
   "type": "library",
   "class": "ESP8266"
}
The ESP8266 library is specific to the ESP8266 version of Espruino, i.e., running Espruino on an ESP8266 module (not to be confused with using the ESP8266 as Wifi add-on to an Espruino board).  This library contains functions to handle ESP8266-specific actions.
For example: `var esp8266 = require('ESP8266'); esp8266.reboot();` performs a hardware reset of the module.
*/

/** Get the global object for the Wifi library/module, this is used in order to send the
 * "on event" callbacks to the handlers.
 */
static JsVar *getWifiModule() {
  JsVar *moduleName = jsvNewFromString("Wifi");
  JsVar *m = jswrap_require(moduleName);
  jsvUnLock(moduleName);
  return m;
}

void jswrap_wifi_disconnect(JsVar *jsCallback) {
  DBGV("> Wifi.disconnect\n");

  // Free any existing callback, then register new callback
  if (g_jsDisconnectCallback != NULL) jsvUnLock(g_jsDisconnectCallback);
  g_jsDisconnectCallback = NULL;
  if (jsCallback != NULL && !jsvIsUndefined(jsCallback) && !jsvIsFunction(jsCallback)) {
    EXPECT_CB_EXCEPTION(jsCallback);
    return;
  }
  g_jsDisconnectCallback = jsvLockAgainSafe(jsCallback);

  int8 conn = wifi_station_get_connect_status();

  // Do the disconnect, we ignore errors 'cause we don't care if we're not currently connected
  wifi_station_disconnect();

  if (conn == STATION_GOT_IP) {
    // If we're connected we let the event handler turn off wifi so we can cleanly disconnect
    // The event handler will also make the callback
    g_disconnecting = true;
  } else {
    // We're not really connected, so we might as well make the callback right here
    DBGV("  Wifi.disconnect turning STA off\n");
    wifi_set_opmode(wifi_get_opmode() & SOFTAP_MODE);
    g_disconnecting = false;
    if (jsvIsFunction(jsCallback)) {
      jsiQueueEvents(NULL, jsCallback, NULL, 0);
    }
  }

  DBG("Wifi.disconnect: opmode=%s\n", wifiMode[wifi_get_opmode()]);
  DBGV("< Wifi.disconnect\n");
}

void jswrap_wifi_stopAP(JsVar *jsCallback) {
  DBGV("> Wifi.stopAP\n");

  // handle the callback parameter
  if (jsCallback != NULL && !jsvIsUndefined(jsCallback) && !jsvIsFunction(jsCallback)) {
    EXPECT_CB_EXCEPTION(jsCallback);
    return;
  }

  // Change operating mode
  bool ok = wifi_set_opmode(wifi_get_opmode() & STATION_MODE); // keep station mode intact

  if (jsvIsFunction(jsCallback)) {
    jsiQueueEvents(NULL, jsCallback, NULL, 0);
  }

  DBG("Wifi.stopAP: opmode=%s\n", wifiMode[wifi_get_opmode()]);
  DBGV("< Wifi.stopAP\n");
}

void jswrap_wifi_connect(
    JsVar *jsSsid,
    JsVar *jsOptions,
    JsVar *jsCallback
  ) {

  DBGV("> Wifi.connect\n");

  struct station_config stationConfig;
  memset(&stationConfig, 0, sizeof(stationConfig));

  // Handle parameter ssid
  char ssid[33];
  if (!jsvIsString(jsSsid)) {
    jsExceptionHere(JSET_ERROR, "No SSID provided");
    return;
  } else {
    int len = jsvGetString(jsSsid, ssid, sizeof(ssid)-1);
    ssid[len]='\0';
    os_strncpy((char *)stationConfig.ssid, ssid, 32);
  }

  // Check callback
  if (g_jsGotIpCallback != NULL) {
    jsvUnLock(g_jsGotIpCallback);
  }
  g_jsGotIpCallback = NULL;
  if (jsCallback != NULL && !jsvIsUndefined(jsCallback) && !jsvIsFunction(jsCallback)) {
    EXPECT_CB_EXCEPTION(jsCallback);
    return;
  }

  // Clear disconnect callback to prevent disconnection from disabling station mode
  if (g_jsDisconnectCallback != NULL) jsvUnLock(g_jsDisconnectCallback);
  g_jsDisconnectCallback = NULL;
  g_disconnecting = false; // we're gonna be connecting...

  // Check for jsOptions
  if (jsOptions != NULL) {
    if (!jsvIsObject(jsOptions)) {
      jsExceptionHere(JSET_ERROR, "Expecting an Object");
      return;
    } else {

      // Handle password
      char password[65];
      os_memset(password, 0, sizeof(password));

      JsVar *jsPassword = jsvObjectGetChild(jsOptions, "password", 0);
      if (jsPassword != NULL && !jsvIsString(jsPassword)) {
        jsExceptionHere(JSET_ERROR, "Expecting options.password to be a string but got %t", jsPassword);
        jsvUnLock(jsPassword);
        return;
      }
      if (jsPassword != NULL) {
        int len = jsvGetString(jsPassword, password, sizeof(password)-1);
        password[len]='\0';
      } else {
        password[0] = '\0';
      }
      os_strncpy((char *)stationConfig.password, password, 64);
      DBGV(" - ssid:%s passwordLen:%d\n", ssid, strlen(password));
      jsvUnLock(jsPassword);

      // Handle bssid
      JsVar *jsBssid= jsvObjectGetChild(jsOptions, "bssid", 0);
      if (jsBssid != NULL && jsvIsString(jsBssid)) {
        char macAddrString[6 * 3 + 1 ];
        int len = jsvGetString(jsBssid, macAddrString, sizeof(macAddrString)-1);
        macAddrString[len] ='\0';
        DBGV("bssid %s, len: %d\n",macAddrString,len);
        bool isMAC = networkParseMACAddress((unsigned char*) stationConfig.bssid, (char *) macAddrString);
        if ( isMAC ) {
           stationConfig.bssid_set = 1;
           DBGV("stationConfig.bssid_set = 1, %d %d %d %d %d %d\n", 
             stationConfig.bssid[0], stationConfig.bssid[1], stationConfig.bssid[2],
             stationConfig.bssid[3], stationConfig.bssid[4], stationConfig.bssid[5]
            );
        } else { 
          jsExceptionHere(JSET_ERROR, "Expecting bssid as \"aa:bb:cc:dd:cc:ff\"");
          jsvUnLock(jsBssid);
          return;
        }
      } 
      jsvUnLock(jsBssid);

      //Handle channel
      JsVar *jsChannel = jsvObjectGetChild(jsOptions, "channel", 0);
      if(jsChannel != NULL && jsvIsInt(jsChannel)){
        uint8 channel = jsvGetInteger(jsChannel);
        if ( channel >= 0 && channel <= 14) {
          DBGV("channel %d\n", channel);
          wifi_set_channel(channel);
        } else  {
          jsExceptionHere(JSET_ERROR, "Expecting options.channel to be a integer between 0 and  14, but got %t", jsChannel);
          jsvUnLock(jsChannel); 
          return;   
        }
      }
      jsvUnLock(jsChannel);

      // Handle  dnsServers
      JsVar *jsDNSServers = jsvObjectGetChild(jsOptions, "dnsServers", 0);
      if (jsvIsArray(jsDNSServers) != false) {
        int count = 0;
        DBGV(" - We have DNS servers!!\n");
        JsVarInt numDNSServers = jsvGetArrayLength(jsDNSServers);
        ip_addr_t dnsAddresses[2];
        if (numDNSServers == 0) {
          DBGV("No servers!!");
          count = 0;
        }
        if (numDNSServers > 0) {
          // One server
          count = 1;
          JsVar *jsCurrentDNSServer = jsvGetArrayItem(jsDNSServers, 0);
          char buffer[50];
          size_t size = jsvGetString(jsCurrentDNSServer, buffer, sizeof(buffer)-1);
          buffer[size] = '\0';
          jsvUnLock(jsCurrentDNSServer);
          dnsAddresses[0].addr = networkParseIPAddress(buffer);
        }
        if (numDNSServers > 1) {
          // Two servers
          count = 2;
          JsVar *jsCurrentDNSServer = jsvGetArrayItem(jsDNSServers, 1);
          char buffer[50];
          size_t size = jsvGetString(jsCurrentDNSServer, buffer, sizeof(buffer)-1);
          buffer[size] = '\0';
          jsvUnLock(jsCurrentDNSServer);
          dnsAddresses[1].addr = networkParseIPAddress(buffer);
        }
        if (numDNSServers > 2) {
          DBG("Ignoring DNS servers after first 2.");
        }
        if (count > 0) {
          espconn_dns_setserver((char)count, dnsAddresses);
        }
      }
      jsvUnLock(jsDNSServers);
    }
  }

  int8 wifiConnectStatus = wifi_station_get_connect_status();
  if (wifiConnectStatus < 0) wifiConnectStatus = 0;
  DBGV(" - Current connect status: %s\n", wifiConn[wifiConnectStatus]);

  struct station_config existingConfig;
  wifi_station_get_config(&existingConfig);

  if (wifiConnectStatus == STATION_GOT_IP &&
      os_strncmp((char *)existingConfig.ssid, (char *)stationConfig.ssid, 32) == 0 &&
      os_strncmp((char *)existingConfig.password, (char *)stationConfig.password, 64) == 0) {
    // we're already happily connected to the target AP, thus we don't need to do anything
    if (jsvIsFunction(jsCallback)) {
      JsVar *params[1];
      params[0] = jsvNewNull();
      jsiQueueEvents(NULL, jsCallback, params, 1);  // TODO: fix callback params and unlock...
      jsvUnLock(params[0]);
    }
    DBGV("< Wifi.connect - no action\n");
    return;
  } else {
    // we're not happily connected to the right AP, so disconnect to start over
    wifi_station_disconnect();
    // we skip the disconnect event unless we're connected (then it's legit) and unless
    // we're idle/off (then there is no disconnect event to start with)
    g_skipDisconnect = wifiConnectStatus != STATION_GOT_IP && wifiConnectStatus != STATION_IDLE;
    wifi_set_opmode(wifi_get_opmode() | STATION_MODE);
  }

  // set callback
  if (jsvIsFunction(jsCallback)) g_jsGotIpCallback = jsvLockAgainSafe(jsCallback);

  // Set the station configuration
  int8 ok = wifi_station_set_config_current(&stationConfig);

  // ensure we have a default DHCP hostname
  char *old_hostname = wifi_station_get_hostname();
  if (old_hostname == NULL || old_hostname[0] == 0)
    wifi_station_set_hostname("espruino");
  DBGV(" - old hostname=%s, new hostname=%s\n", old_hostname, wifi_station_get_hostname());

  // Set the WiFi mode of the ESP8266
  wifi_set_event_handler_cb(wifiEventHandler); // this seems to get lost sometimes...

  // Perform the network level connection.
  wifi_station_connect();
  DBG("Wifi.connect: ssid=%s pass_len=%d opmode=%s num_dns_srv=%d\n",
      ssid, strlen(password), wifiMode[wifi_get_opmode()], count);
  DBGV("< Wifi.connect\n");
}


void jswrap_wifi_scan(JsVar *jsCallback) {
  DBGV("> Wifi.scan\n");

  // If we have a saved scan callback function we must be scanning already
  if (g_jsScanCallback != NULL) {
    jsExceptionHere(JSET_ERROR, "A scan is already in progress.");
    return;
  }

  // Check and save callback
  if (!jsvIsFunction(jsCallback)) {
    EXPECT_CB_EXCEPTION(jsCallback);
    return;
  }
  g_jsScanCallback = jsvLockAgainSafe(jsCallback);
  g_disconnecting = false; // we don't want that to interfere

  // Ask the ESP8266 to perform a network scan after first entering
  // station mode.  The network scan will eventually result in a callback
  // being executed (scanCB) which will contain the results.
  wifi_set_opmode_current(wifi_get_opmode() | STATION_MODE);

  // Request a scan of the network calling "scanCB" on completion
  struct scan_config config = {0};
  config.show_hidden = true;
  wifi_station_scan(&config, scanCB);

  DBG("Wifi.scan starting: mode=%s\n", wifiMode[wifi_get_opmode()]);
  DBGV("< Wifi.scan\n");
}


void jswrap_wifi_startAP(
    JsVar *jsSsid,     //!< The network SSID that we will use to listen as.
    JsVar *jsOptions,  //!< Configuration options.
    JsVar *jsCallback  //!< A callback to be invoked when completed.
  ) {
  DBGV("> Wifi.startAP\n");

  // Check callback
  if (jsCallback != NULL && !jsvIsUndefined(jsCallback) && !jsvIsFunction(jsCallback)) {
    EXPECT_CB_EXCEPTION(jsCallback);
    return;
  }

  // Validate that the SSID is provided and is a string.
  if (!jsvIsString(jsSsid)) {
      jsExceptionHere(JSET_ERROR, "No SSID.");
    return;
  }

  // Make sure jsOptions is NULL or an object
  if (jsOptions != NULL && !jsvIsNull(jsOptions) && !jsvIsObject(jsOptions)) {
    EXPECT_OPT_EXCEPTION(jsOptions);
    return;
  }

  // Build our SoftAP configuration details
  struct softap_config softApConfig;
  memset(&softApConfig, 0, sizeof(softApConfig));

  softApConfig.max_connection = 4;
  softApConfig.beacon_interval = 100;
  softApConfig.authmode = AUTH_OPEN;
  // ssid is not null terminated
  softApConfig.ssid_len = jsvGetString(jsSsid, (char *)softApConfig.ssid, sizeof(softApConfig.ssid));

  // Handle any options that may have been supplied.
  if (jsvIsObject(jsOptions)) {
    // Handle hidden
    JsVar *jsHidden = jsvObjectGetChild(jsOptions, "hidden", 0); 
    if (jsvIsInt(jsHidden)) {
      int hidden = jsvGetInteger(jsHidden);
      if (hidden >= 0 && hidden <= 1) softApConfig.ssid_hidden = hidden;
    }
    jsvUnLock(jsHidden);

    // Handle channel
    JsVar *jsChan = jsvObjectGetChild(jsOptions, "channel", 0);
    if (jsvIsInt(jsChan)) {
      int chan = jsvGetInteger(jsChan);
      if (chan >= 1 && chan <= 13) softApConfig.channel = chan;
    }
    jsvUnLock(jsChan);

    // Handle password
    JsVar *jsPassword = jsvObjectGetChild(jsOptions, "password", 0);
    if (jsPassword != NULL) {
      // handle password:null
      if (jsvGetStringLength(jsPassword) != 0) {
        if (!jsvIsString(jsPassword) || jsvGetStringLength(jsPassword) < 8) {
          jsExceptionHere(JSET_ERROR, "Password must be string of at least 8 characters");
          jsvUnLock(jsPassword);
          return;
        }
        int len = jsvGetString(jsPassword, (char *)softApConfig.password, sizeof(softApConfig.password)-1);
        softApConfig.password[len] = '\0';
      }
    }
    jsvUnLock(jsPassword);

    // Handle "authMode" processing.  Here we check that "authMode", if supplied, is
    // one of the allowed values and set the softApConfig object property appropriately.
    JsVar *jsAuth = jsvObjectGetChild(jsOptions, "authMode", 0);
    if (jsvIsString(jsAuth)) {
      if (jsvIsStringEqual(jsAuth, "open")) {
        softApConfig.authmode = AUTH_OPEN;
      } else if (jsvIsStringEqual(jsAuth, "wpa2")) {
        softApConfig.authmode = AUTH_WPA2_PSK;
      } else if (jsvIsStringEqual(jsAuth, "wpa")) {
        softApConfig.authmode = AUTH_WPA_PSK;
      } else if (jsvIsStringEqual(jsAuth, "wpa_wpa2")) {
        softApConfig.authmode = AUTH_WPA_WPA2_PSK;
      } else {
        jsvUnLock(jsAuth);
        jsExceptionHere(JSET_ERROR, "Unknown authMode value.");
        return;
      }
    } else {
      // no explicit auth mode, set according to presence of password
      softApConfig.authmode = softApConfig.password[0] == 0 ? AUTH_OPEN : AUTH_WPA2_PSK;
    }
    jsvUnLock(jsAuth);

    // Make sure password and authmode match
    if (softApConfig.authmode != AUTH_OPEN && softApConfig.password[0] == 0) {
      jsExceptionHere(JSET_ERROR, "Password not set but authMode not open.");
      return;
    }
    if (softApConfig.authmode == AUTH_OPEN && softApConfig.password[0] != 0) {
      jsExceptionHere(JSET_ERROR, "Auth mode set to open but password supplied.");
      return;
    }
  }

  // Define that we are in Soft AP mode including station mode if required.
  DBGV("Wifi: switching to soft-AP mode, authmode=%d\n", softApConfig.authmode);
  wifi_set_opmode(wifi_get_opmode() | SOFTAP_MODE);
  wifi_set_event_handler_cb(wifiEventHandler); // this seems to get lost sometimes...

  // Set the WiFi configuration.
  bool ok = wifi_softap_set_config_current(&softApConfig);

  // Is this still true:
  // We should really check that becoming an access point works, however as of SDK 1.4, we
  // are finding that if we are currently connected to an access point and we switch to being
  // an access point, it works ... but returns 1 indicating an error.
  //if (!rc) DBG("Error %d returned from wifi_softap_set_config, probably ignore...\n", rc);

  if (jsCallback != NULL) {
    // Set the return error as a function of the return code returned from the call to
    // the ESP8266 API to create the AP
    JsVar *params[1];
    FLASH_STR(_fstr, "Error from wifi_softap_set_config");
    size_t len = flash_strlen(_fstr);
    char buff[len+1];
    flash_strncpy(buff, _fstr, len+1);
    params[0] = ok ? jsvNewNull() : jsvNewFromString(buff);
    jsiQueueEvents(NULL, jsCallback, params, 1);
    jsvUnLock(params[0]);
  }
  DBG("Wifi.startAP ssid=%s pass_len=%d opmode=%d auth=%d\n",
      softApConfig.ssid, os_strlen((char *)softApConfig.password), wifi_get_opmode(),
      softApConfig.authmode);
  DBGV("< Wifi.startAP\n");
}


JsVar *jswrap_wifi_getStatus(JsVar *jsCallback) {
  DBGV("> Wifi.getStatus\n");

  // Check callback
  if (jsCallback != NULL && !jsvIsNull(jsCallback) && !jsvIsFunction(jsCallback)) {
    EXPECT_CB_EXCEPTION(jsCallback);
    return NULL;
  }

  uint8 opMode = wifi_get_opmode();
  uint8 phy = wifi_get_phy_mode();
  uint8 sleep = wifi_get_sleep_type();
  int8 conn = wifi_station_get_connect_status();
  if (conn < 0) conn = 0;

  JsVar *jsWiFiStatus = jsvNewObject();
  jsvObjectSetChildAndUnLock(jsWiFiStatus, "mode",
    jsvNewFromString(wifiMode[opMode]));
  jsvObjectSetChildAndUnLock(jsWiFiStatus, "station",
    jsvNewFromString((opMode&STATION_MODE) ? wifiConn[conn] : "off"));
  jsvObjectSetChildAndUnLock(jsWiFiStatus, "ap",
    jsvNewFromString((opMode & SOFTAP_MODE) ? "enabled" : "disabled"));
  jsvObjectSetChildAndUnLock(jsWiFiStatus, "phy",
    jsvNewFromString(wifiPhy[phy]));
  jsvObjectSetChildAndUnLock(jsWiFiStatus, "powersave",
    jsvNewFromString(sleep == NONE_SLEEP_T ? "none" : "ps-poll"));
  jsvObjectSetChildAndUnLock(jsWiFiStatus, "savedMode",
    jsvNewFromString(wifiMode[savedMode]));

  // Schedule callback if a function was provided
  if (jsvIsFunction(jsCallback)) {
    DBGV("  Wifi.getStatus queuing CB\n");
    JsVar *params[1];
    params[0] = jsWiFiStatus;
    jsiQueueEvents(NULL, jsCallback, params, 1);
  }

  DBGV("< Wifi.getStatus\n");
  return jsWiFiStatus;
}


void jswrap_wifi_setConfig(JsVar *jsSettings) {
  DBGV("> Wifi.setConfig\n");

  // Make sure jsSetings an object
  if (!jsvIsObject(jsSettings)) {
    EXPECT_OPT_EXCEPTION(jsSettings);
    return;
  }

  // phy setting
  JsVar *jsPhy = jsvObjectGetChild(jsSettings, "phy", 0);
  if (jsvIsString(jsPhy)) {
    if (jsvIsStringEqual(jsPhy, "11b")) {
      wifi_set_phy_mode(PHY_MODE_11B);
    } else if (jsvIsStringEqual(jsPhy, "11g")) {
      wifi_set_phy_mode(PHY_MODE_11G);
    } else if (jsvIsStringEqual(jsPhy, "11n")) {
      wifi_set_phy_mode(PHY_MODE_11N);
    } else {
      jsvUnLock(jsPhy);
      jsExceptionHere(JSET_ERROR, "Unknown phy mode.");
      return;
    }
  }
  if (jsPhy != NULL) jsvUnLock(jsPhy);

  // powersave setting
  JsVar *jsPowerSave = jsvObjectGetChild(jsSettings, "powersave", 0);
  if (jsvIsString(jsPowerSave)) {
    if (jsvIsStringEqual(jsPowerSave, "none")) {
      wifi_set_sleep_type(NONE_SLEEP_T);
    } else if (jsvIsStringEqual(jsPowerSave, "ps-poll")) {
      wifi_set_sleep_type(MODEM_SLEEP_T);
    } else {
      jsvUnLock(jsPowerSave);
      jsExceptionHere(JSET_ERROR, "Unknown powersave mode.");
      return;
    }
  }
  if (jsPowerSave != NULL) jsvUnLock(jsPowerSave);

  DBGV("< Wifi.setConfig\n");
}


JsVar *jswrap_wifi_getDetails(JsVar *jsCallback) {{
  DBGV("> Wifi.getDetails\n");

  // Check callback
  if (jsCallback != NULL && !jsvIsNull(jsCallback) && !jsvIsFunction(jsCallback)) {
    EXPECT_CB_EXCEPTION(jsCallback);
    return NULL;
  }

  uint8 opMode = wifi_get_opmode();

  JsVar *jsDetails = jsvNewObject();

  int8 conn = wifi_station_get_connect_status();
  if (conn < 0) conn = 0;
  jsvObjectSetChildAndUnLock(jsDetails, "status", jsvNewFromString(wifiConn[conn]));

  struct station_config config;
  wifi_station_get_config(&config);
  char buf[65];
  // ssid
  os_strncpy(buf, (char *)config.ssid, 32);
  buf[32] = 0;
  jsvObjectSetChildAndUnLock(jsDetails, "ssid", jsvNewFromString(buf));
  // password
  os_strncpy(buf, (char *)config.password, 64);
  buf[64] = 0;
  jsvObjectSetChildAndUnLock(jsDetails, "password", jsvNewFromString((char *)config.password));

  if (opMode & STATION_MODE) {
    int rssi = wifi_station_get_rssi();
    if (rssi > 0) rssi = 0; // sanity...
    jsvObjectSetChildAndUnLock(jsDetails, "rssi", jsvNewFromInteger(rssi));

    //jsvObjectSetChildAndUnLock(jsDetails, "authMode", jsvNewFromString(wifiAuth[config.));
  }

  jsvObjectSetChildAndUnLock(jsDetails, "savedSsid", jsvNewNull());

  // Schedule callback if a function was provided
  if (jsvIsFunction(jsCallback)) {
    JsVar *params[1];
    params[0] = jsDetails;
    jsiQueueEvents(NULL, jsCallback, params, 1);
  }

  DBGV("< Wifi.getDetails\n");
  return jsDetails;
}
}


JsVar *jswrap_wifi_getAPDetails(JsVar *jsCallback) {
  DBGV("> Wifi.getAPDetails\n");

  // Check callback
  if (jsCallback != NULL && !jsvIsNull(jsCallback) && !jsvIsFunction(jsCallback)) {
    EXPECT_CB_EXCEPTION(jsCallback);
    return NULL;
  }

  uint8 opMode = wifi_get_opmode();

  JsVar *jsDetails = jsvNewObject();

  jsvObjectSetChildAndUnLock(jsDetails, "status",
    jsvNewFromString(opMode & SOFTAP_MODE ? "enabled" : "disabled"));

  struct softap_config config;
  wifi_softap_get_config(&config);
  jsvObjectSetChildAndUnLock(jsDetails, "authMode", jsvNewFromString(wifiAuth[config.authmode]));
  jsvObjectSetChildAndUnLock(jsDetails, "hidden", jsvNewFromBool(config.ssid_hidden));
  jsvObjectSetChildAndUnLock(jsDetails, "maxConn", jsvNewFromInteger(config.max_connection));
  char buf[65];
  // ssid
  os_strncpy(buf, (char *)config.ssid, 32);
  buf[32] = 0;
  jsvObjectSetChildAndUnLock(jsDetails, "ssid", jsvNewFromString(buf));
  // password
  os_strncpy(buf, (char *)config.password, 64);
  buf[64] = 0;
  jsvObjectSetChildAndUnLock(jsDetails, "password", jsvNewFromString((char *)config.password));

  jsvObjectSetChildAndUnLock(jsDetails, "savedSsid", jsvNewNull());

  if (opMode & SOFTAP_MODE) {
    JsVar *jsArray = jsvNewArray(NULL, 0);
    struct station_info *station = wifi_softap_get_station_info();
    while(station) {
      JsVar *jsSta = jsvNewObject();
      jsvObjectSetChildAndUnLock(jsSta, "ip",
        networkGetAddressAsString((uint8_t *)&station->ip.addr, 4, 10, '.'));
      char macAddrString[6*3 + 1];
      os_sprintf(macAddrString, macFmt,
        station->bssid[0], station->bssid[1], station->bssid[2],
        station->bssid[3], station->bssid[4], station->bssid[5]);
      jsvObjectSetChildAndUnLock(jsSta, "mac", jsvNewFromString(macAddrString));
      jsvArrayPush(jsArray, jsSta);
      jsvUnLock(jsSta);
      station = STAILQ_NEXT(station, next);
    }
    wifi_softap_free_station_info();
    jsvObjectSetChildAndUnLock(jsDetails, "stations", jsArray);
  }

  // Schedule callback if a function was provided
  if (jsvIsFunction(jsCallback)) {
    JsVar *params[1];
    params[0] = jsDetails;
    jsiQueueEvents(NULL, jsCallback, params, 1);
  }

  DBGV("< Wifi.getAPDetails\n");
  return jsDetails;
}

void jswrap_wifi_save(JsVar *what) {
  DBGV("> Wifi.save\n");  
  JsVar *o = jsvNewObject();
  if (!o) return;

  if (jsvIsString(what) && jsvIsStringEqual(what, "clear")) {
    JsVar *name = jsvNewFromString(WIFI_CONFIG_STORAGE_NAME);
    jswrap_storage_erase(name);
    jsvUnLock(name);
    DBG("Wifi.save(clear)\n");
    return;
  }

  // station stuff
  struct station_config sta_config;
  wifi_station_get_config(&sta_config);
  jsvObjectSetChildAndUnLock(o, "ssid", jsvNewFromString((char *)sta_config.ssid));
  jsvObjectSetChildAndUnLock(o, "password", jsvNewFromString((char *)sta_config.password));
  jsvObjectSetChildAndUnLock(o, "mode", jsvNewFromInteger(wifi_get_opmode()));
  jsvObjectSetChildAndUnLock(o, "phyMode", jsvNewFromInteger(wifi_get_phy_mode()));
  jsvObjectSetChildAndUnLock(o, "sleepType", jsvNewFromInteger(wifi_get_sleep_type()));

  char *hostname = wifi_station_get_hostname();
  if (hostname) jsvObjectSetChildAndUnLock(o, "hostname", jsvNewFromString((char *) hostname));

  // softap stuff
  struct softap_config ap_config;
  wifi_softap_get_config(&ap_config);
  jsvObjectSetChildAndUnLock(o, "ssidAP", jsvNewFromString((char *)ap_config.ssid));
  jsvObjectSetChildAndUnLock(o, "passwordAP", jsvNewFromString((char *) ap_config.password));
  jsvObjectSetChildAndUnLock(o, "authmodeAP", jsvNewFromInteger(ap_config.authmode));
  jsvObjectSetChildAndUnLock(o, "hiddenAP", jsvNewFromInteger(ap_config.ssid_hidden));
  jsvObjectSetChildAndUnLock(o, "channelAP", jsvNewFromInteger(ap_config.channel));
  
  savedMode = wifi_get_opmode();

  // save object
  JsVar *name = jsvNewFromString(WIFI_CONFIG_STORAGE_NAME);
  //JsVar *arr = jsvNewArray(&o,1);
  jswrap_storage_erase(name);
  jswrap_storage_write(name,o,0,0); 
  //jsvUnLock3(arr,name,o);
  jsvUnLock2(name,o);

  DBGV("< Wifi.save: write completed\n");
}

void jswrap_wifi_restore(void) {
  DBG("Wifi.restore\n");
  JsVar *name = jsvNewFromString(WIFI_CONFIG_STORAGE_NAME);
  JsVar *o = jswrap_storage_readJSON(name, true);
  if (!o) { // no data 
    jsvUnLock2(name,o);
    return; 
  }

  JsVar *v;
  v = jsvObjectGetChild(o,"mode",0);
  savedMode = jsvGetInteger(v);
  jsvUnLock(v);   
  wifi_set_opmode_current(savedMode);

  v = jsvObjectGetChild(o,"phyMode",0);
  wifi_set_phy_mode(jsvGetInteger(v));
  jsvUnLock(v); 

  v = jsvObjectGetChild(o,"sleepType",0);
  wifi_set_sleep_type(jsvGetInteger(v));
  jsvUnLock(v);
 
  if (savedMode & SOFTAP_MODE) {

    struct softap_config ap_config;
    os_memset(&ap_config, 0, sizeof(ap_config));

    v = jsvObjectGetChild(o,"authmodeAP",0);
    ap_config.authmode =jsvGetInteger(v);
    jsvUnLock(v); 

    v = jsvObjectGetChild(o,"hiddenAP",0);
    ap_config.ssid_hidden = jsvGetInteger(v);
    jsvUnLock(v);

    v = jsvObjectGetChild(o,"ssidAP",0);
    jsvGetString(v, (char *)ap_config.ssid, sizeof(ap_config.ssid));

    ap_config.ssid_len = jsvGetStringLength(v);
    jsvUnLock(v);

    v = jsvObjectGetChild(o,"passwordAP",0);
    jsvGetString(v, (char *)ap_config.password, sizeof(ap_config.password));
    jsvUnLock(v); 

    v = jsvObjectGetChild(o,"channelAP",0);
    ap_config.channel = jsvGetInteger(v);
    jsvUnLock(v);

    ap_config.max_connection = 4;
    ap_config.beacon_interval = 100;
    wifi_softap_set_config_current(&ap_config);
    DBG("Wifi.restore: AP=%s\n", ap_config.ssid);
  }

  if (savedMode & STATION_MODE) {

    v = jsvObjectGetChild(o,"hostname",0);
    
    if (v) {
      char hostname[64];
      jsvGetString(v, hostname, sizeof(hostname));
      DBG("Wifi.restore: hostname=%s\n", hostname);
      wifi_station_set_hostname(hostname);
    }
    jsvUnLock(v); 

    struct station_config sta_config;
    os_memset(&sta_config, 0, sizeof(sta_config));

    v = jsvObjectGetChild(o,"ssid",0);
    jsvGetString(v, (char *)sta_config.ssid, sizeof(sta_config.ssid)); 
    jsvUnLock(v); 

    v = jsvObjectGetChild(o,"password",0);
    jsvGetString(v, (char *)sta_config.password, sizeof(sta_config.password));
    jsvUnLock(v); 

    wifi_station_set_config_current(&sta_config);
    DBG("Wifi.restore: STA=%s\n", sta_config.ssid);

    //jsWarn("Station SSID '%s', password '%s'\n",sta_config.ssid,sta_config.password);

    wifi_station_connect(); // we're not supposed to call this from user_init but it doesn't harm
                            // and we need it when invoked from JS
  }
}


/**
 * Get the ip info for the given interface.  The interfaces are:
 * * 0 - Station
 * * 1 - Access Point
 */
static JsVar *getIPInfo(JsVar *jsCallback, int interface) {
  // Check callback
  if (jsCallback != NULL && !jsvIsNull(jsCallback) && !jsvIsFunction(jsCallback)) {
    EXPECT_CB_EXCEPTION(jsCallback);
    return NULL;
  }

  // first get IP address info, this may fail if we're not connected
  struct ip_info info;
  bool ok = wifi_get_ip_info(interface, &info);
  JsVar *jsIpInfo = jsvNewObject();
  if (ok) {
    jsvObjectSetChildAndUnLock(jsIpInfo, "ip",
      networkGetAddressAsString((uint8_t *)&info.ip.addr, 4, 10, '.'));
    jsvObjectSetChildAndUnLock(jsIpInfo, "netmask",
      networkGetAddressAsString((uint8_t *)&info.netmask.addr, 4, 10, '.'));
    jsvObjectSetChildAndUnLock(jsIpInfo, "gw",
      networkGetAddressAsString((uint8_t *)&info.gw.addr, 4, 10, '.'));
  }

  // now get MAC address (which always succeeds)
  uint8 macAddr[6];
  wifi_get_macaddr(interface, macAddr);
  char macAddrString[6*3 + 1];
  os_sprintf(macAddrString, macFmt,
    macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
  jsvObjectSetChildAndUnLock(jsIpInfo, "mac", jsvNewFromString(macAddrString));

  // Schedule callback if a function was provided
  if (jsvIsFunction(jsCallback)) {
    JsVar *params[2];
    params[0] = jsvNewWithFlags(JSV_NULL);
    params[1] = jsIpInfo;
    jsiQueueEvents(NULL, jsCallback, params, 2);
    jsvUnLock(params[0]);
  }

  return jsIpInfo;
}

JsVar *jswrap_wifi_getIP(JsVar *jsCallback) {
  DBGV("> Wifi.getIP\n");
  JsVar *jsIP = getIPInfo(jsCallback, 0);
  DBGV("< Wifi.getIP\n");
  return jsIP;
}

JsVar *jswrap_wifi_getAPIP(JsVar *jsCallback) {
  DBGV("> Wifi.getAPIP\n");
  JsVar *jsIP = getIPInfo(jsCallback, 1);
  DBGV("< Wifi.getAPIP\n");
  return jsIP;
}

/**
 * Handle a response from espconn_gethostbyname.
 * Invoke the callback function to inform the caller that a hostname has been converted to
 * an IP address.  The callback function should take a parameter that is the IP address.
 */
static void dnsFoundCallback(
    const char *hostname, //!< The hostname that was converted to an IP address.
    ip_addr_t *ipAddr,    //!< The ip address retrieved.  This may be 0.
    void *arg             //!< Parameter passed in from espconn_gethostbyname.
  ) {
  DBG(">> Wifi.getHostByName CB - %s %x\n", hostname, ipAddr ? ipAddr->addr : 0);
  if (g_jsHostByNameCallback != NULL) {
    JsVar *params[1];
    if (ipAddr == NULL) {
      params[0] = jsvNewNull();
    } else {
      params[0] = networkGetAddressAsString((uint8_t *)&ipAddr->addr, 4, 10, '.');
    }
    jsiQueueEvents(NULL, g_jsHostByNameCallback, params, 1);
    jsvUnLock(params[0]);
    jsvUnLock(g_jsHostByNameCallback);
    g_jsHostByNameCallback = NULL;
  }
  DBGV("<< Wifi.getHostByName CB\n");
}

void jswrap_wifi_getHostByName(
    JsVar *jsHostname,
    JsVar *jsCallback
) {
  ip_addr_t ipAddr;
  char hostname[256];

  DBGV("> Wifi.getHostByName\n");

  if (!jsvIsString(jsHostname)) {
    jsExceptionHere(JSET_ERROR, "Hostname parameter is not a string");
    return;
  }
  if (!jsvIsFunction(jsCallback)) {
    jsExceptionHere(JSET_ERROR, "Callback is not a function");
    return;
  }
  // Save the callback unlocking an old callback if needed.
  if (g_jsHostByNameCallback != NULL) jsvUnLock(g_jsHostByNameCallback);
  g_jsHostByNameCallback = jsCallback;
  jsvLockAgainSafe(g_jsHostByNameCallback);

  jsvGetString(jsHostname, hostname, sizeof(hostname));
  DBG("  Wifi.getHostByName: %s\n", hostname);
  err_t err = espconn_gethostbyname(NULL, hostname, &ipAddr, dnsFoundCallback);
  if (err == ESPCONN_OK) {
    DBGV("Already resolved\n");
    dnsFoundCallback(hostname, &ipAddr, NULL);
  } else if (err != ESPCONN_INPROGRESS) {
    os_printf("Error: %d from espconn_gethostbyname\n", err);
    dnsFoundCallback(hostname, NULL, NULL);
  }
  DBGV("< Wifi.getHostByName\n");
}

JsVar *jswrap_wifi_getHostname(JsVar *jsCallback) {
  char *hostname = wifi_station_get_hostname();
  if (hostname == NULL) {
    hostname = "";
  }
  return jsvNewFromString(hostname);
}

void jswrap_wifi_setHostname(
    JsVar *jsHostname, //!< The hostname to set for device.
    JsVar *jsCallback
) {
  char hostname[256];
  jsvGetString(jsHostname, hostname, sizeof(hostname));
  DBG("Wifi.setHostname: %s\n", hostname);
  wifi_station_set_hostname(hostname);

  // now start/restart DHCP for this to take effect
  if (wifi_station_dhcpc_status() == DHCP_STARTED)
    wifi_station_dhcpc_stop();
  wifi_station_dhcpc_start();

  // now update mDNS
  startMDNS(hostname);

  if (jsvIsFunction(jsCallback))
    jsiQueueEvents(0, jsCallback, 0, 0);
}

//===== mDNS

static bool mdns_started;

// FIXME: To be removed in favor of using mDNS.js module instead
void startMDNS(char *hostname) {
  return; // FIXME: debug

  if (mdns_started) stopMDNS();

  // find our IP address
  struct ip_info info;
  bool ok = wifi_get_ip_info(0, &info);
  if (!ok || info.ip.addr == 0) return; // no IP address

  // start mDNS
  struct mdns_info *mdns_info = (struct mdns_info *)os_zalloc(sizeof(struct mdns_info));
  mdns_info->host_name = hostname;
  mdns_info->server_name = "espruino";
  mdns_info->server_port = 23;
  mdns_info->ipAddr = info.ip.addr;
  espconn_mdns_init(mdns_info);
  mdns_started = true;
}

void stopMDNS() {
  espconn_mdns_server_unregister();
  espconn_mdns_close();
  mdns_started = false;
}

//===== SNTP

static os_timer_t sntpTimer;

static void sntpSync(void *arg) {
  uint32_t sysTime = (uint32_t)((jshGetSystemTime() + 500000) / 1000000);
  uint32_t ntpTime = sntp_get_current_timestamp();
  if (!ntpTime) {
    DBG("NTP time: null\n");
  } else {
    if (ntpTime-sysTime != 0) {
      DBG("NTP time: %ld delta=%ld %s\n", (long unsigned int) ntpTime, (long unsigned int)ntpTime-sysTime, sntp_get_real_time(ntpTime));
    }
    jswrap_interactive_setTime((JsVarFloat)ntpTime);
  }
  os_timer_disarm(&sntpTimer);
  os_timer_arm(&sntpTimer, 30*1000, 0);
}

void jswrap_wifi_setSNTP(JsVar *jsServer, JsVar *jsZone) {
  if (!jsvIsNumeric(jsZone)) {
    jsExceptionHere(JSET_ERROR, "Zone is not a number");
    return;
  }
  int zone = jsvGetInteger(jsZone);
  if (zone < -11 || zone > 13) {
    jsExceptionHere(JSET_ERROR, "Zone must be in range -11..13");
    return;
  }

  if (!jsvIsString(jsServer)) {
    jsExceptionHere(JSET_ERROR, "Server is not a string");
    return;
  }
  char server[64];
  jsvGetString(jsServer, server, 64);

  sntp_stop();
  if (sntp_set_timezone(zone)) {
    sntp_setservername(0, server);
    sntp_init();
    os_timer_disarm(&sntpTimer);
    os_timer_setfn(&sntpTimer, sntpSync, 0);
    os_timer_arm(&sntpTimer, 100, 0); // 100ms
  }
  DBG("SNTP: %s %s%d\n", server, zone>=0?"+":"", zone);
}

// When the Espruino environment is reset (e.g. the reset() function), this callback function
// will be invoked.
// The purpose is to reset the environment by cleaning up whatever might be needed
// to be cleaned up. This does not actually touch the wifi itself: we want the IDE to remain
// connected!
void jswrap_ESP8266_wifi_reset() {
  DBGV("> Wifi reset\n");

  g_jsGotIpCallback = NULL;
  g_jsPingCallback = NULL;
  g_jsScanCallback = NULL;
  g_jsHostByNameCallback = NULL;
  g_jsDisconnectCallback = NULL;
  g_disconnecting = false;

  DBGV("< Wifi reset\n");
}

// This function is called in the user_main's user_init() to set-up the wifi based on what
// was saved in flash. This will restore the settings from flash into the SDK so the SDK
// fires-up the right AP/STA modes and connections.
void   jswrap_ESP8266_wifi_init1() {
  DBGV("> Wifi.init1\n");

  // register the state change handler so we get debug printout for sure
  wifi_set_event_handler_cb(wifiEventHandler);

  // tell the SDK to let us have 10 connections
  espconn_tcp_set_max_con(MAX_SOCKETS);
  DBG("< Wifi init1, phy=%d mode=%d\n", wifi_get_phy_mode(), wifi_get_opmode());
}


/*JSON{
  "type":"init",
  "generate":"jswrap_ESP8266_wifi_soft_init"
}

// This function is called in soft_init to hook-up the network. This happens from user_main's
// init_done() and also from `reset()` in order to re-hook-up the network.
*/
void jswrap_ESP8266_wifi_soft_init() {
  DBGV("> Wifi.soft_init\n");

  // initialize the network stack
  netInit_esp8266_board();
  JsNetwork net;
  networkCreate(&net, JSNETWORKTYPE_ESP8266_BOARD);
  networkState = NETWORKSTATE_ONLINE;

  DBGV("< Wifi.soft_init\n");
}

/*JSON{
  "type"     : "staticmethod",
  "class"    : "ESP8266",
  "ifdef"    : "ESP8266",
  "name"     : "ping",
  "generate" : "jswrap_wifi_ping",
  "params"   : [
    ["ipAddr", "JsVar", "A string representation of an IP address."],
    ["pingCallback", "JsVar", "Optional callback function."]
  ]
}
**DEPRECATED** - please use `Wifi.ping` instead.

Perform a network ping request. The parameter can be either a String or a numeric IP address.
*/
void jswrap_wifi_ping(
    JsVar *ipAddr,      //!< A string or integer representation of an IP address.
    JsVar *pingCallback //!< Optional callback function.
) {
  memset(&pingOpt, 0, sizeof(pingOpt));
  // If the parameter is a string, get the IP address from the string
  // representation.
  if (jsvIsString(ipAddr)) {
    char ipString[20];
    int len = jsvGetString(ipAddr, ipString, sizeof(ipString)-1);
    ipString[len] = '\0';
    pingOpt.ip = networkParseIPAddress(ipString);
    if (pingOpt.ip == 0) {
        jsExceptionHere(JSET_ERROR, "Not a valid IP address.");
      return;
    }
  } else
  // If the parameter is an integer, treat it as an IP address.
  if (jsvIsInt(ipAddr)) {
    pingOpt.ip = jsvGetInteger(ipAddr);
  } else
  // The parameter was neither a string nor an IP address and hence we don't
  // know how to get the IP address of the partner to ping so throw an
  // exception.
  {
      jsExceptionHere(JSET_ERROR, "IP address must be string or integer.");
    return;
  }

  if (jsvIsUndefined(pingCallback) || jsvIsNull(pingCallback)) {
    if (g_jsPingCallback != NULL) {
      jsvUnLock(g_jsPingCallback);
    }
    g_jsPingCallback = NULL;
  } else if (!jsvIsFunction(pingCallback)) {
      jsExceptionHere(JSET_ERROR, "Callback is not a function.");
    return;
  } else {
    if (g_jsPingCallback != NULL) {
      jsvUnLock(g_jsPingCallback);
    }
    g_jsPingCallback = pingCallback;
    jsvLockAgainSafe(g_jsPingCallback);
  }

  // We now have an IP address to ping ... so ping.
  pingOpt.count = 5;
  pingOpt.recv_function = pingRecvCB;
  ping_start(&pingOpt);
}


/**
 * Handle receiving a response from a ping reply.
 * If a callback function has been supplied we invoked that callback by queuing it for future
 * execution.  A parameter is supplied to the callback which is a JavaScript object that contains:
 *  - totalCount
 *  - totalBytes
 *  - totalTime
 *  - respTime
 *  - seqNo
 *  - timeoutCount
 *  - bytes
 *  - error
 */
static void pingRecvCB(void *pingOpt, void *pingResponse) {
  struct ping_resp *pingResp = (struct ping_resp *)pingResponse;
  os_printf("Received a ping response!\n");
  if (g_jsPingCallback != NULL) {
    JsVar *jsPingResponse = jsvNewObject();
    jsvObjectSetChildAndUnLock(jsPingResponse, "totalCount",   jsvNewFromInteger(pingResp->total_count));
    jsvObjectSetChildAndUnLock(jsPingResponse, "totalBytes",   jsvNewFromInteger(pingResp->total_bytes));
    jsvObjectSetChildAndUnLock(jsPingResponse, "totalTime",    jsvNewFromInteger(pingResp->total_time));
    jsvObjectSetChildAndUnLock(jsPingResponse, "respTime",     jsvNewFromInteger(pingResp->resp_time));
    jsvObjectSetChildAndUnLock(jsPingResponse, "seqNo",        jsvNewFromInteger(pingResp->seqno));
    jsvObjectSetChildAndUnLock(jsPingResponse, "timeoutCount", jsvNewFromInteger(pingResp->timeout_count));
    jsvObjectSetChildAndUnLock(jsPingResponse, "bytes",        jsvNewFromInteger(pingResp->bytes));
    jsvObjectSetChildAndUnLock(jsPingResponse, "error",        jsvNewFromInteger(pingResp->ping_err));
    JsVar *params[1];
    params[0] = jsPingResponse;
    jsiQueueEvents(NULL, g_jsPingCallback, params, 1);
    jsvUnLock(jsPingResponse);
  }
}

// worker for jswrap_wifi_setIP and jswrap_wifi_setAPIP
static void setIP(JsVar *jsSettings, JsVar *jsCallback, int interface) {
  DBGV("> setIP\n");
  
  char ipTmp[20];
  int len = 0;
  bool rc = false;
  memset(&info, 0, sizeof(info));

// first check parameter 
  if (!jsvIsObject(jsSettings)) {
    EXPECT_OPT_EXCEPTION(jsSettings);
    return;
  }

// get,check and store ip
  JsVar *jsIP = jsvObjectGetChild(jsSettings, "ip", 0);
  if (jsIP != NULL && !jsvIsString(jsIP)) {
      EXPECT_OPT_EXCEPTION(jsIP);
      jsvUnLock(jsIP);
      return; 
  }
  jsvGetString(jsIP, ipTmp, sizeof(ipTmp)-1);
  //DBG(">> ip: %s\n",ipTmp);
  info.ip.addr = networkParseIPAddress(ipTmp); 
  if ( info.ip.addr  == 0) {
    jsExceptionHere(JSET_ERROR, "Not a valid IP address.");
    jsvUnLock(jsIP);
    return;
  }
  jsvUnLock(jsIP);

// get, check and store gw
  JsVar *jsGW = jsvObjectGetChild(jsSettings, "gw", 0);
  if (jsGW != NULL && !jsvIsString(jsGW)) {
      EXPECT_OPT_EXCEPTION(jsGW);
      jsvUnLock(jsGW);
      return ;
  }
  jsvGetString(jsGW, ipTmp, sizeof(ipTmp)-1);
  //DBG(">> gw: %s\n",ipTmp);
  info.gw.addr = networkParseIPAddress(ipTmp);
  if (info.gw.addr == 0) {
    jsExceptionHere(JSET_ERROR, "Not a valid Gateway address.");
    jsvUnLock(jsGW);
    return;
  }
  jsvUnLock(jsGW);

// netmask setting
  JsVar *jsNM = jsvObjectGetChild(jsSettings, "netmask", 0);
  if (jsNM != NULL && !jsvIsString(jsNM)) {
      EXPECT_OPT_EXCEPTION(jsNM);
      jsvUnLock(jsNM);
      return;
  }  
  jsvGetString(jsNM, ipTmp, sizeof(ipTmp)-1);
  //DBG(">> netmask: %s\n",ipTmp);
  info.netmask.addr = networkParseIPAddress(ipTmp); 
  if (info.netmask.addr == 0) {
    jsExceptionHere(JSET_ERROR, "Not a valid Netmask.");
    jsvUnLock(jsNM);    
    return;
  }
  jsvUnLock(jsNM);

// set IP for station
  if (interface == STATION_IF ) {
    wifi_station_dhcpc_stop();
    rc = wifi_set_ip_info(STATION_IF, &info);
  }
// set IP for access point
  else {
    wifi_softap_dhcps_stop();
    rc = wifi_set_ip_info(SOFTAP_IF, &info);
    wifi_softap_dhcps_start();
  }

  DBG(">> rc: %s\n", rc ? "true" : "false");

  // Schedule callback
  if (jsvIsFunction(jsCallback)) {
    JsVar *params[1];
    params[0] = rc ? jsvNewWithFlags(JSV_NULL) : jsvNewFromString("Failure");
    jsiQueueEvents(NULL, jsCallback, params, 1); 
    jsvUnLock(params[0]);
  }
  else {
    jsExceptionHere(JSET_ERROR, "Callback is not a function.");
  }
  DBGV("< setIP\n");
  return ;
};


void jswrap_wifi_setIP(JsVar *jsSettings, JsVar *jsCallback) {
  setIP(jsSettings, jsCallback, STATION_IF);
  return ;
}

void jswrap_wifi_setAPIP(JsVar *jsSettings, JsVar *jsCallback) {
  setIP(jsSettings, jsCallback, SOFTAP_IF);
  return ;
}


/**
 * Callback function that is invoked at the culmination of a scan.
 */
static void scanCB(void *arg, STATUS status) {
  /**
   * Create a JsVar that is an array of JS objects where each JS object represents a
   * retrieved access point set of information.   The structure of a record will be:
   * o authMode
   * o isHidden
   * o rssi
   * o channel
   * o ssid
   * When the array has been built, invoke the callback function passing in the array
   * of records.
   */

  DBGV(">> Wifi.scanCB\n");
  if (g_jsScanCallback == NULL) {
    DBGV("<< Wifi.scanCB\n");
    return;
  }

  // Create the Empty JS array that will be passed as a parameter to the callback.
  JsVar *jsAccessPointArray = jsvNewArray(NULL, 0);
  struct bss_info *bssInfo;

  bssInfo = (struct bss_info *)arg;
  short count = 0;
  while(bssInfo != NULL) {
    // Add a new object to the JS array that will be passed as a parameter to
    // the callback.
    // Create, populate and add a child ...
    JsVar *jsCurrentAccessPoint = jsvNewObject();
    if (bssInfo->rssi > 0) bssInfo->rssi = 0;
    jsvObjectSetChildAndUnLock(jsCurrentAccessPoint, "rssi", jsvNewFromInteger(bssInfo->rssi));
    jsvObjectSetChildAndUnLock(jsCurrentAccessPoint, "channel", jsvNewFromInteger(bssInfo->channel));
    jsvObjectSetChildAndUnLock(jsCurrentAccessPoint, "authMode", jsvNewFromString(wifiAuth[bssInfo->authmode]));
    jsvObjectSetChildAndUnLock(jsCurrentAccessPoint, "isHidden", jsvNewFromBool(bssInfo->is_hidden));

    // The SSID may **NOT** be NULL terminated ... so handle that.
    char ssid[sizeof(bssInfo->ssid) + 1];
    os_strncpy((char *)ssid, (char *)bssInfo->ssid, sizeof(bssInfo->ssid));
    ssid[sizeof(ssid)-1] = '\0';
    jsvObjectSetChildAndUnLock(jsCurrentAccessPoint, "ssid", jsvNewFromString(ssid));

    char macAddrString[6*3 + 1];
    os_sprintf(macAddrString, macFmt,
      bssInfo->bssid[0], bssInfo->bssid[1], bssInfo->bssid[2],
      bssInfo->bssid[3], bssInfo->bssid[4], bssInfo->bssid[5]);
    jsvObjectSetChildAndUnLock(jsCurrentAccessPoint, "mac", jsvNewFromString(macAddrString));

    // Add the new record to the array
    jsvArrayPush(jsAccessPointArray, jsCurrentAccessPoint);
    jsvUnLock(jsCurrentAccessPoint);
    count++;

    DBGV(" - ssid: %s\n", bssInfo->ssid);
    bssInfo = STAILQ_NEXT(bssInfo, next);
  }
  DBG("Wifi.scan completed, found %d\n", count);

  // We have now completed the scan callback, so now we can invoke the JS callback.
  JsVar *params[1];
  params[0] = jsAccessPointArray;
  jsiQueueEvents(NULL, g_jsScanCallback, params, 1);

  jsvUnLock(jsAccessPointArray);
  jsvUnLock(g_jsScanCallback);
  g_jsScanCallback = NULL;
  DBGV("<< Wifi.scanCB\n");
}


/**
 * Invoke the JavaScript callback to notify the program that an ESP8266
 * WiFi event has occurred.
 */
static void sendWifiEvent(
    uint32 eventType, //!< The ESP8266 WiFi event type.
    JsVar *jsDetails  //!< The JS object to be passed as a parameter to the callback.
) {
  JsVar *module = getWifiModule();
  if (!module) return; // out of memory?

  // get event name as string and compose param list
  JsVar *params[1];
  params[0] = jsDetails;
  char *eventName = wifiGetEvent(eventType);
  DBGV("wifi.on(%s)\n", eventName);
  jsiQueueObjectCallbacks(module, eventName, params, 1);
  jsvUnLock(module);
  return;
}

static void sendWifiCompletionCB(
    JsVar **g_jsCallback, //!< Pointer to the global callback variable
    char *reason          //!< NULL if successful, error string otherwise
) {
  if (!jsvIsFunction(*g_jsCallback)) return; // we ain't got a function pointer: nothing to do

  JsVar *params[1];
  params[0] = reason ? jsvNewFromString(reason) : jsvNewNull();
  jsiQueueEvents(NULL, *g_jsCallback, params, 1);
  jsvUnLock(params[0]);
  // unlock and delete the global callback
  jsvUnLock(*g_jsCallback);
  *g_jsCallback = NULL;
}

/**
 * ESP8266 WiFi Event handler.
 * This function is called by the ESP8266
 * environment when significant events happen related to the WiFi environment.
 * The event handler is registered with a call to wifi_set_event_handler_cb()
 * that is provided by the ESP8266 SDK.
 */
static void wifiEventHandler(System_Event_t *evt) {
  char buf[66];
  char macAddrString[6*3 + 1];
  uint8_t *mac;
  char *reason;

  JsVar *jsDetails = jsvNewObject();

  switch(evt->event) {
  // We have connected to an access point.
  case EVENT_STAMODE_CONNECTED:
    DBG("Wifi event: connected to ssid %s, ch %d\n", evt->event_info.connected.ssid,
      evt->event_info.connected.channel);

    // ssid
    os_strncpy(buf, (char *)evt->event_info.connected.ssid, 32);
    buf[evt->event_info.connected.ssid_len] = 0;
    jsvObjectSetChildAndUnLock(jsDetails, "ssid", jsvNewFromString(buf));
    // bssid = mac address
    mac = evt->event_info.connected.bssid;
    os_sprintf(macAddrString, macFmt, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    jsvObjectSetChildAndUnLock(jsDetails, "mac", jsvNewFromString(macAddrString));
    // channel
    jsvObjectSetChildAndUnLock(jsDetails, "channel",
        jsvNewFromInteger(evt->event_info.connected.channel));
    sendWifiEvent(evt->event, jsDetails);
    break;

  // We have disconnected or been disconnected from an access point.
  case EVENT_STAMODE_DISCONNECTED:
    reason = wifiGetReason(evt->event_info.disconnected.reason);
    int8 wifiConnectStatus = wifi_station_get_connect_status();
    if (wifiConnectStatus < 0) wifiConnectStatus = 0;
    DBG("Wifi event: disconnected from ssid %s, reason %s (%d) status=%s(%d)\n",
      evt->event_info.disconnected.ssid, reason, evt->event_info.disconnected.reason,
      wifiConn[wifiConnectStatus], wifiConnectStatus );

    if (g_skipDisconnect) {
      DBGV("  Skipping disconnect\n");
      g_skipDisconnect = false;
      break;
    }

    // if'were connecting and we get a fatal error, then make a callback
    // need two more cases
    if ((wifiConnectStatus == STATION_WRONG_PASSWORD ||
         wifiConnectStatus == STATION_NO_AP_FOUND ||
         wifiConnectStatus == STATION_CONNECT_FAIL ) 
         && jsvIsFunction(g_jsGotIpCallback)) {
      sendWifiCompletionCB(&g_jsGotIpCallback, wifiConn[wifiConnectStatus]);
    }
    // plus REASON_AUTH_EXPIRE
    if (wifiConnectStatus == STATION_CONNECTING  &&
         evt->event_info.disconnected.reason == REASON_AUTH_EXPIRE  &&
         jsvIsFunction(g_jsGotIpCallback)) {
      sendWifiCompletionCB(&g_jsGotIpCallback, reason);
    }

    
    // if we're in the process of disconnecting we want to turn STA mode off now
    // at that point we may need to make a callback too
    if (g_disconnecting) {
      DBGV("  Wifi.event: turning STA mode off\n");
      wifi_set_opmode(wifi_get_opmode() & SOFTAP_MODE);
      g_disconnecting = false;
      if (jsvIsFunction(g_jsDisconnectCallback)) {
        jsiQueueEvents(NULL, g_jsDisconnectCallback, NULL, 0);
        jsvUnLock(g_jsDisconnectCallback);
        g_jsDisconnectCallback = NULL;
      }
    }

    // ssid
    os_strncpy(buf, (char *)evt->event_info.connected.ssid, 32);
    buf[evt->event_info.connected.ssid_len] = 0;
    jsvObjectSetChildAndUnLock(jsDetails, "ssid", jsvNewFromString(buf));
    // bssid = mac address
    mac = evt->event_info.connected.bssid;
    os_sprintf(macAddrString, macFmt, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    jsvObjectSetChildAndUnLock(jsDetails, "mac", jsvNewFromString(macAddrString));
    jsvObjectSetChildAndUnLock(jsDetails, "reason", jsvNewFromString(reason));
    sendWifiEvent(evt->event, jsDetails);
    break;

  // The authentication information at the access point has changed.
  case EVENT_STAMODE_AUTHMODE_CHANGE:
    DBG("Wifi event: auth mode %s -> %s\n",
      wifiAuth[evt->event_info.auth_change.old_mode],
      wifiAuth[evt->event_info.auth_change.new_mode]);

    jsvObjectSetChildAndUnLock(jsDetails, "oldMode",
      jsvNewFromString(wifiAuth[evt->event_info.auth_change.old_mode]));
    jsvObjectSetChildAndUnLock(jsDetails, "newMode",
      jsvNewFromString(wifiAuth[evt->event_info.auth_change.new_mode]));
    sendWifiEvent(evt->event, jsDetails);
    break;

  // We have been allocated an IP address.
  case EVENT_STAMODE_GOT_IP:
    DBG("Wifi event: got ip:" IPSTR ", mask:" IPSTR ", gw:" IPSTR "\n",
      IP2STR(&evt->event_info.got_ip.ip), IP2STR(&evt->event_info.got_ip.mask),
      IP2STR(&evt->event_info.got_ip.gw));

    // start mDNS
    char *hostname = wifi_station_get_hostname();
    if (hostname && hostname[0] != 0) {
      startMDNS(hostname);
    }

    // Make Wifi.connected() callback
    if (jsvIsFunction(g_jsGotIpCallback)) {
      sendWifiCompletionCB(&g_jsGotIpCallback, NULL);
    }

    // "on" event callback
    jsvObjectSetChildAndUnLock(jsDetails, "ip",
      networkGetAddressAsString((uint8_t *)&evt->event_info.got_ip.ip, 4, 10, '.'));
    jsvObjectSetChildAndUnLock(jsDetails, "mask",
      networkGetAddressAsString((uint8_t *)&evt->event_info.got_ip.mask, 4, 10, '.'));
    jsvObjectSetChildAndUnLock(jsDetails, "gw",
      networkGetAddressAsString((uint8_t *)&evt->event_info.got_ip.gw, 4, 10, '.'));
    sendWifiEvent(evt->event, jsDetails);
    break;

  case EVENT_STAMODE_DHCP_TIMEOUT:
    os_printf("Wifi event: DHCP timeout");

    sendWifiEvent(evt->event, jsvNewNull());
    break;

  case EVENT_SOFTAPMODE_STACONNECTED:
    os_printf("Wifi event: station " MACSTR " joined, AID = %d\n",
      MAC2STR(evt->event_info.sta_connected.mac), evt->event_info.sta_connected.aid);
    // "on" event callback
    mac = evt->event_info.sta_connected.mac;
    os_sprintf(macAddrString, macFmt, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    jsvObjectSetChildAndUnLock(jsDetails, "mac", jsvNewFromString(macAddrString));
    sendWifiEvent(evt->event, jsDetails);
    break;

  case EVENT_SOFTAPMODE_STADISCONNECTED:
    os_printf("Wifi event: station " MACSTR " left, AID = %d\n",
      MAC2STR(evt->event_info.sta_disconnected.mac), evt->event_info.sta_disconnected.aid);
    // "on" event callback
    mac = evt->event_info.sta_disconnected.mac;
    os_sprintf(macAddrString, macFmt, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    jsvObjectSetChildAndUnLock(jsDetails, "mac", jsvNewFromString(macAddrString));
    sendWifiEvent(evt->event, jsDetails);
    break;

  case EVENT_SOFTAPMODE_PROBEREQRECVED:
    os_printf("Wifi event: probe request from station " MACSTR ", rssi = %d\n",
      MAC2STR(evt->event_info.ap_probereqrecved.mac), evt->event_info.ap_probereqrecved.rssi);
    // "on" event callback
    int rssi = evt->event_info.ap_probereqrecved.rssi;
    if (rssi > 0) rssi = 0;
    jsvObjectSetChildAndUnLock(jsDetails, "rssi", jsvNewFromInteger(rssi));
    mac = evt->event_info.ap_probereqrecved.mac;
    os_sprintf(macAddrString, macFmt, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    jsvObjectSetChildAndUnLock(jsDetails, "mac", jsvNewFromString(macAddrString));
    sendWifiEvent(evt->event, jsDetails);
    break;

  default:
    os_printf("Wifi: unexpected event %d\n", evt->event);
    break;
  }
  jsvUnLock(jsDetails);
}
