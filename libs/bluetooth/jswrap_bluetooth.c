/* Copyright (c) 2014 Nordic Semiconductor. All Rights Reserved.
 *
 * The information contained herein is property of Nordic Semiconductor ASA.
 * Terms and conditions of usage are described in detail in NORDIC
 * SEMICONDUCTOR STANDARD SOFTWARE LICENSE AGREEMENT.
 *
 * Licensees are granted free, non-transferable use of the information. NO
 * WARRANTY of ANY KIND is provided. This heading must NOT be removed from
 * the file.
 *
 */

#include "jswrap_bluetooth.h"
#include "jsinteractive.h"
#include "jsdevices.h"
#include "jswrap_promise.h"
#include "jswrap_interactive.h"
#include "jswrap_string.h"
#include "jsnative.h"

#include "bluetooth_utils.h"
#include "bluetooth.h"

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#ifdef NRF5X
#include "nrf5x_utils.h"
#include "nordic_common.h"
#include "nrf.h"
#include "ble_gap.h"
#include "ble_hci.h"
#include "ble_advdata.h"
#include "ble_conn_params.h"
#include "app_timer.h"
#include "ble_nus.h"
#include "app_util_platform.h"
#if NRF_SD_BLE_API_VERSION<5
#include "softdevice_handler.h"
#endif

#ifdef USE_NFC
#include "nfc_uri_msg.h"
#include "nfc_ble_pair_msg.h"
#include "nfc_launchapp_msg.h"
#endif
#endif

#ifdef ESP32
#include "BLE/esp32_gap_func.h"
#include "BLE/esp32_gatts_func.h"
#include "BLE/esp32_gattc_func.h"
#define BLE_CONN_HANDLE_INVALID -1
#endif

// ------------------------------------------------------------------------------
// ------------------------------------------------------------------------------

JsVar *blePromise = 0;
JsVar *bleTaskInfo = 0;
BleTask bleTask = BLETASK_NONE;


bool bleInTask(BleTask task) {
  return bleTask==task;
}

BleTask bleGetCurrentTask() {
  return bleTask;
}

bool bleNewTask(BleTask task, JsVar *taskInfo) {
  if (bleTask) {
    jsExceptionHere(JSET_ERROR, "BLE task %d is already in progress", (int)bleTask);
    return false;
  }
/*  if (blePromise) {
    jsiConsolePrintf("Existing blePromise!\n");
    jsvTrace(blePromise,2);
  }
  if (bleTaskInfo) {
    jsiConsolePrintf("Existing bleTaskInfo!\n");
    jsvTrace(bleTaskInfo,2);
  }*/
  assert(!blePromise && !bleTaskInfo);
  blePromise = jspromise_create();
  bleTask = task;
  bleTaskInfo = jsvLockAgainSafe(taskInfo);
  return true;
}

void bleCompleteTask(BleTask task, bool ok, JsVar *data) {
  //jsiConsolePrintf(ok?"RES %d %v\n":"REJ %d %q\n", task, data);
  if (task != bleTask) {
    jsExceptionHere(JSET_INTERNALERROR, "BLE task completed that wasn't scheduled (%d/%d)", task, bleTask);
    return;
  }
  bleTask = BLETASK_NONE;
  if (blePromise) {
    if (ok) jspromise_resolve(blePromise, data);
    else jspromise_reject(blePromise, data);
    jsvUnLock(blePromise);
    blePromise = 0;
  }
  jsvUnLock(bleTaskInfo);
  bleTaskInfo = 0;
  jshHadEvent();
}

void bleCompleteTaskSuccess(BleTask task, JsVar *data) {
  bleCompleteTask(task, true, data);
}
void bleCompleteTaskSuccessAndUnLock(BleTask task, JsVar *data) {
  bleCompleteTask(task, true, data);
  jsvUnLock(data);
}
void bleCompleteTaskFail(BleTask task, JsVar *data) {
  bleCompleteTask(task, false, data);
}
void bleCompleteTaskFailAndUnLock(BleTask task, JsVar *data) {
  bleCompleteTask(task, false, data);
  jsvUnLock(data);
}
void bleSwitchTask(BleTask task) {
  bleTask = task;
}

// ------------------------------------------------------------------------------
// ------------------------------------------------------------------------------
#ifdef NRF52
void bleSetActiveBluetoothGattServer(JsVar *var) {
  jsvObjectSetChild(execInfo.hiddenRoot, BLE_NAME_GATT_SERVER, var);
}

JsVar *bleGetActiveBluetoothGattServer() {
  return jsvObjectGetChild(execInfo.hiddenRoot, BLE_NAME_GATT_SERVER, 0);
}
#endif
// ------------------------------------------------------------------------------
// ------------------------------------------------------------------------------

/*JSON{
  "type" : "init",
  "generate" : "jswrap_ble_init"
}*/
void jswrap_ble_init() {
  // Turn off sleeping if it was on before
  jsiStatus &= ~BLE_IS_SLEEPING;


  if (jsiStatus & JSIS_COMPLETELY_RESET) {
#if defined(USE_NFC) && defined(NFC_DEFAULT_URL)
    // By default Puck.js's NFC will send you to the PuckJS website
    // address is included so Web Bluetooth can connect to the correct one
    JsVar *addr = jswrap_ble_getAddress();
    JsVar *uri = jsvVarPrintf(NFC_DEFAULT_URL"?a=%v", addr);
    jsvUnLock(addr);
    jswrap_nfc_URL(uri);
    jsvUnLock(uri);
#endif
  } else {
#ifdef USE_NFC
    // start NFC, if it had been set
    JsVar *flatStr = jsvObjectGetChild(execInfo.hiddenRoot, "NfcEnabled", 0);
    if (flatStr) {
      uint8_t *flatStrPtr = (uint8_t*)jsvGetFlatStringPointer(flatStr);
      if (flatStrPtr) jsble_nfc_start(flatStrPtr, jsvGetLength(flatStr));
      jsvUnLock(flatStr);
    }
#endif
  }
  // Set advertising interval back to default
  bleAdvertisingInterval = DEFAULT_ADVERTISING_INTERVAL;
  // Now set up whatever advertising we were doing before
  jswrap_ble_reconfigure_softdevice();
}

/** Reconfigure the softdevice (on init or after restart) to have all the services/advertising we need */
void jswrap_ble_reconfigure_softdevice() {
  JsVar *v,*o;
  // restart various
  v = jsvObjectGetChild(execInfo.root, BLE_SCAN_EVENT,0);
  if (v) jsble_set_scanning(true, false);
  jsvUnLock(v);
  v = jsvObjectGetChild(execInfo.root, BLE_RSSI_EVENT,0);
  if (v) jsble_set_rssi_scan(true);
  jsvUnLock(v);
  // advertising
  v = jsvObjectGetChild(execInfo.hiddenRoot, BLE_NAME_ADVERTISE_DATA, 0);
  o = jsvObjectGetChild(execInfo.hiddenRoot, BLE_NAME_ADVERTISE_OPTIONS, 0);
  if (v || o) jswrap_ble_setAdvertising(v, o);
  jsvUnLock2(v,o);
  // services
  v = jsvObjectGetChild(execInfo.hiddenRoot, BLE_NAME_SERVICE_DATA, 0);
  if (v) jsble_set_services(v);
  jsvUnLock(v);
  // If we had scan response data set, update it
  JsVar *scanData = jsvObjectGetChild(execInfo.hiddenRoot, BLE_NAME_SCAN_RESPONSE_DATA, 0);
  if (scanData) jswrap_ble_setScanResponse(scanData);
  jsvUnLock(scanData);
  // Set up security related stuff
  jsble_update_security();
}

/*JSON{
  "type" : "idle",
  "generate" : "jswrap_ble_idle"
}*/
bool jswrap_ble_idle() {
  return false;
}

/*JSON{
  "type" : "kill",
  "generate" : "jswrap_ble_kill"
}*/
void jswrap_ble_kill() {
#ifdef USE_NFC
  // stop NFC emulation
  jsble_nfc_stop(); // not a problem to call this if NFC isn't started
#endif
  // stop any BLE tasks
  bleTask = BLETASK_NONE;
  if (blePromise) jsvUnLock(blePromise);
  blePromise = 0;
  if (bleTaskInfo) jsvUnLock(bleTaskInfo);
  bleTaskInfo = 0;
  // if we were scanning, make sure we stop
  jsble_set_scanning(false, false);
  jsble_set_rssi_scan(false);

#if CENTRAL_LINK_COUNT>0
  // if we were connected to something, disconnect
  if (jsble_has_central_connection()) {
    jsble_disconnect(m_central_conn_handle);
  }
#endif
}

void jswrap_ble_dumpBluetoothInitialisation(vcbprintf_callback user_callback, void *user_data) {


  JsVar *v,*o;
  v = jsvObjectGetChild(execInfo.root, BLE_SCAN_EVENT,0);
  if (v) {
    user_callback("NRF.setScan(", user_data);
    jsiDumpJSON(user_callback, user_data, v, 0);
    user_callback(");\n", user_data);
  }
  jsvUnLock(v);
  v = jsvObjectGetChild(execInfo.root, BLE_RSSI_EVENT,0);
  if (v) {
    user_callback("NRF.setRSSIHandler(", user_data);
    jsiDumpJSON(user_callback, user_data, v, 0);
    user_callback(");\n", user_data);
  }
  jsvUnLock(v);
  // advertising
  v = jsvObjectGetChild(execInfo.hiddenRoot, BLE_NAME_ADVERTISE_DATA, 0);
  o = jsvObjectGetChild(execInfo.hiddenRoot, BLE_NAME_ADVERTISE_OPTIONS, 0);
  if (v || o)
    cbprintf(user_callback, user_data, "NRF.setAdvertising(%j, %j);\n",v,o);
  jsvUnLock2(v,o);
  // services
  v = jsvObjectGetChild(execInfo.hiddenRoot, BLE_NAME_SERVICE_DATA, 0);
  o = jsvObjectGetChild(execInfo.hiddenRoot, BLE_NAME_SERVICE_OPTIONS, 0);
  if (v || o)
    cbprintf(user_callback, user_data, "NRF.setServices(%j, %j);\n",v,o);
  jsvUnLock2(v,o);
  // security
  v = jsvObjectGetChild(execInfo.hiddenRoot, BLE_NAME_SECURITY, 0);
  if (v)
    cbprintf(user_callback, user_data, "NRF.setSecurity(%j);\n",v);
  jsvUnLock(v);
  // mac address
  v = jsvObjectGetChild(execInfo.hiddenRoot, BLE_NAME_MAC_ADDRESS, 0);
  if (v)
    cbprintf(user_callback, user_data, "NRF.setAddress(%j);\n",v);
  jsvUnLock(v);
}

// ------------------------------------------------------------------------------
// ------------------------------------------------------------------------------

/*JSON{
    "type": "class",
    "class" : "NRF"
}
The NRF class is for controlling functionality of the Nordic nRF51/nRF52 chips.

Most functionality is related to Bluetooth Low Energy, however there are also some functions related to NFC that apply to NRF52-based devices.

*/

// ------------------------------------------------------------------------------
// ------------------------------------------------------------------------------

/*JSON{
  "type" : "event",
  "class" : "NRF",
  "name" : "connect",
  "params" : [
    ["addr","JsVar","The address of the device that has connected"]
  ]
}
Called when a host device connects to Espruino. The first argument contains the address.
 */
/*JSON{
  "type" : "event",
  "class" : "NRF",
  "name" : "disconnect",
  "params" : [
    ["reason","int","The reason code reported back by the BLE stack - see Nordic's [`ble_hci.h` file](https://github.com/espruino/Espruino/blob/master/targetlibs/nrf5x_12/components/softdevice/s132/headers/ble_hci.h#L71) for more information"]
  ]
}
Called when a host device disconnects from Espruino.

The most common reason is:
* 19 - `REMOTE_USER_TERMINATED_CONNECTION`
* 22 - `LOCAL_HOST_TERMINATED_CONNECTION`
 */
/*JSON{
  "type" : "event",
  "class" : "NRF",
  "name" : "security",
  "params" : [
    ["status","JsVar","An object containing `{auth_status,bonded,lv4,kdist_own,kdist_peer}"]
  ]
}
Contains updates on the security of the current Bluetooth link.

See Nordic's `ble_gap_evt_auth_status_t` structure for more information.
*/
/*JSON{
  "type" : "event",
  "class" : "NRF",
  "name" : "HID",
  "#if" : "defined(NRF52)"
}
Called with a single byte value when Espruino is set up as
a HID device and the computer it is connected to sends a
HID report back to Espruino. This is usually used for handling
indications such as the Caps Lock LED.
 */

/*JSON{
  "type" : "event",
  "class" : "NRF",
  "name" : "servicesDiscover",
  "#if" : "defined(NRF52) || defined(ESP32)"
}
Called with discovered services when discovery is finished
 */
/*JSON{
  "type" : "event",
  "class" : "NRF",
  "name" : "characteristicsDiscover",
  "#if" : "defined(NRF52) || defined(ESP32)"
}
Called with discovered characteristics when discovery is finished
 */


/*JSON{
  "type" : "event",
  "class" : "NRF",
  "name" : "NFCon",
  "ifdef" : "NRF52"
}
Called when an NFC field is detected
 */
/*JSON{
  "type" : "event",
  "class" : "NRF",
  "name" : "NFCoff",
  "ifdef" : "NRF52"
}
Called when an NFC field is no longer detected
 */
/*JSON{
  "type" : "event",
  "class" : "NRF",
  "name" : "NFCrx",
  "params" : [
    ["arr","JsVar","An ArrayBuffer containign the received data"]
  ],
  "ifdef" : "NRF52"
}
When NFC is started with `NRF.nfcStart`, this is fired
when NFC data is received. It doesn't get called if
NFC is started with `NRF.nfcURL` or `NRF.nfcRaw`
 */
/*JSON{
  "type" : "event",
  "class" : "BluetoothDevice",
  "name" : "gattserverdisconnected",
  "params" : [
    ["reason","int","The reason code reported back by the BLE stack - see Nordic's `ble_hci.h` file for more information"]
  ],
  "ifdef" : "NRF52"
}
Called when the device gets disconnected.

To connect and then print `Disconnected` when the device is
disconnected, just do the following:

```
var gatt;
NRF.connect("aa:bb:cc:dd:ee:ff").then(function(gatt) {
  gatt.device.on('gattserverdisconnected', function(reason) {
    console.log("Disconnected ",reason);
  });
});
```
 */
/*JSON{
  "type" : "event",
  "class" : "BluetoothRemoteGATTCharacteristic",
  "name" : "characteristicvaluechanged",
  "ifdef" : "NRF52"
}
Called when a characteristic's value changes, *after* `BluetoothRemoteGATTCharacteristic.startNotifications` has been called.

```
  ...
  return service.getCharacteristic("characteristic_uuid");
}).then(function(c) {
  c.on('characteristicvaluechanged', function(event) {
    console.log("-> "+event.target.value);
  });
  return c.startNotifications();
}).then(...
```

The first argument is of the form `{target : BluetoothRemoteGATTCharacteristic}`, and `BluetoothRemoteGATTCharacteristic.value`
will then contain the new value (as a DataView).
 */

/*JSON{
  "type" : "object",
  "name" : "Bluetooth",
  "instanceof" : "Serial",
  "ifdef" : "BLUETOOTH"
}
The Bluetooth Serial port - used when data is sent or received over Bluetooth Smart on nRF51/nRF52 chips.
 */

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "disconnect",
    "generate" : "jswrap_ble_disconnect"
}
If a device is connected to Espruino, disconnect from it.
*/
void jswrap_ble_disconnect() {
  uint32_t err_code;
  if (jsble_has_peripheral_connection()) {
    err_code = jsble_disconnect(m_peripheral_conn_handle);
    jsble_check_error(err_code);
  }
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "sleep",
    "generate" : "jswrap_ble_sleep"
}
Disable Bluetooth advertising and disconnect from any device that
connected to Puck.js as a peripheral (this won't affect any devices
that Puck.js initiated connections to).

This makes Puck.js undiscoverable, so it can't be connected to.

Use `NRF.wake()` to wake up and make Puck.js connectable again.
*/
void jswrap_ble_sleep() {
  // set as sleeping
  bleStatus |= BLE_IS_SLEEPING;
  // stop advertising
  jsble_advertising_stop();
  // If connected, disconnect.
  // when we disconnect, we'll see BLE_IS_SLEEPING and won't advertise
  jswrap_ble_disconnect();
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "wake",
    "generate" : "jswrap_ble_wake"
}
Enable Bluetooth advertising (this is enabled by default), which
allows other devices to discover and connect to Puck.js.

Use `NRF.sleep()` to disable advertising.
*/
void jswrap_ble_wake() {
  bleStatus &= ~BLE_IS_SLEEPING;
  jsble_check_error(jsble_advertising_start());
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "restart",
    "generate" : "jswrap_ble_restart"
}
Restart the Bluetooth softdevice (if there is currently a BLE connection,
it will queue a restart to be done when the connection closes).

You shouldn't need to call this function in normal usage. However, Nordic's
BLE softdevice has some settings that cannot be reset. For example there
are only a certain number of unique UUIDs. Once these are all used the
only option is to restart the softdevice to clear them all out.
*/
void jswrap_ble_restart() {
  if (jsble_has_connection()) {
    jsiConsolePrintf("BLE Connected, queueing BLE restart for later\n");
    bleStatus |= BLE_NEEDS_SOFTDEVICE_RESTART;
    return;
  } else {
    // Not connected, so we can restart now
    jsble_restart_softdevice();
    return;
  }
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "getAddress",
    "generate" : "jswrap_ble_getAddress",
    "return" : ["JsVar", "MAC address - a string of the form 'aa:bb:cc:dd:ee:ff'" ]
}
Get this device's default Bluetooth MAC address.

For Puck.js, the last 5 characters of this (eg. `ee:ff`)
are used in the device's advertised Bluetooth name.
*/
JsVar *jswrap_ble_getAddress() {
#ifdef NRF5X
  uint32_t addr0 =  NRF_FICR->DEVICEADDR[0];
  uint32_t addr1 =  NRF_FICR->DEVICEADDR[1];
#else
  uint32_t addr0 = 0xDEADDEAD;
  uint32_t addr1 = 0xDEAD;
#endif
  return jsvVarPrintf("%02x:%02x:%02x:%02x:%02x:%02x",
      ((addr1>>8 )&0xFF)|0xC0,
      ((addr1    )&0xFF),
      ((addr0>>24)&0xFF),
      ((addr0>>16)&0xFF),
      ((addr0>>8 )&0xFF),
      ((addr0    )&0xFF));
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "setAddress",
    "#if" : "defined(NRF52)",
    "generate" : "jswrap_ble_setAddress",
    "params" : [
      ["addr","JsVar","The address to use (as a string)"]
    ]
}
Set this device's default Bluetooth MAC address:

```
NRF.setAddress("ff:ee:dd:cc:bb:aa random");
```

Addresses take the form:

* `"ff:ee:dd:cc:bb:aa"` or `"ff:ee:dd:cc:bb:aa public"` for a public address
* `"ff:ee:dd:cc:bb:aa random"` for a random static address (the default for Espruino)

This may throw a `INVALID_BLE_ADDR` error if the upper two bits
of the address don't match the address type.

To change the address, Espruino must restart the softdevice. It will only do
so when it is disconnected from other devices.
*/
void jswrap_ble_setAddress(JsVar *address) {
#ifdef NRF52
  ble_gap_addr_t p_addr;
  if (!bleVarToAddr(address, &p_addr)) {
    jsExceptionHere(JSET_ERROR, "Expecting a mac address of the form aa:bb:cc:dd:ee:ff");
    return;
  }
  jsvObjectSetChild(execInfo.hiddenRoot, BLE_NAME_MAC_ADDRESS, address);
  jswrap_ble_restart();
#else
  jsExceptionHere(JSET_ERROR, "Not implemented");
#endif
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "getBattery",
    "generate" : "jswrap_ble_getBattery",
    "return" : ["float", "Battery level in volts" ]
}
Get the battery level in volts (the voltage that the NRF chip is running off of).

This is the battery level of the device itself - it has nothing to with any
device that might be connected.
*/
JsVarFloat jswrap_ble_getBattery() {
  return jshReadVRef();
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "setAdvertising",
    "generate" : "jswrap_ble_setAdvertising",
    "params" : [
      ["data","JsVar","The data to advertise as an object - see below for more info"],
      ["options","JsVar","An optional object of options"]
    ]
}
Change the data that Espruino advertises.

Data can be of the form `{ UUID : data_as_byte_array }`. The UUID should be
a [Bluetooth Service ID](https://developer.bluetooth.org/gatt/services/Pages/ServicesHome.aspx).

For example to return battery level at 95%, do:

```
NRF.setAdvertising({
  0x180F : [95]
});
```

Or you could report the current temperature:

```
setInterval(function() {
  NRF.setAdvertising({
    0x1809 : [Math.round(E.getTemperature())]
  });
}, 30000);
```

You can also supply the raw advertising data in an array. For example
to advertise as an Eddystone beacon:

```
NRF.setAdvertising([0x03,  // Length of Service List
  0x03,  // Param: Service List
  0xAA, 0xFE,  // Eddystone ID
  0x13,  // Length of Service Data
  0x16,  // Service Data
  0xAA, 0xFE, // Eddystone ID
  0x10,  // Frame type: URL
  0xF8, // Power
  0x03, // https://
  'g','o','o','.','g','l','/','B','3','J','0','O','c'],
    {interval:100});
```

(However for Eddystone we'd advise that you use the [Espruino Eddystone library](/Puck.js+Eddystone))

**Note:** When specifying data as an array, certain advertising options such as
`discoverable` and `showName` won't have any effect.

**Note:** The size of Bluetooth LE advertising packets is limited to 31 bytes. If
you want to advertise more data, consider using an array for `data` (See below), or
`NRF.setScanResponse`.

You can even specify an array of arrays or objects, in which case each advertising packet
will be used in turn - for instance to make your device advertise battery level and its name
as well as both Eddystone and iBeacon :

```
NRF.setAdvertising([
  {0x180F : [Puck.getBatteryPercentage()]}, // normal advertising, with battery %
  require("ble_ibeacon").get(...), // iBeacon
  require("ble_eddystone").get(...), // eddystone
], {interval:300});
```

`options` is an object, which can contain:

```
{
  name: "Hello" // The name of the device
  showName: true/false // include full name, or nothing
  discoverable: true/false // general discoverable, or limited - default is limited
  connectable: true/false // whether device is connectable - default is true
  scannable : true/false // whether device can be scanned for scan response packets - default is true
  interval: 600 // Advertising interval in msec, between 20 and 10000 (default is 375ms)
  manufacturer: 0x0590 // IF sending manufacturer data, this is the manufacturer ID
  manufacturerData: [...] // IF sending manufacturer data, this is an array of data
}
```

Setting `connectable` and `scannable` to false gives the lowest power consumption
as the BLE radio doesn't have to listen after sending advertising.

**NOTE:** Non-`connectable` advertising can't have an advertising interval less than 100ms
according to the BLE spec.

So for instance to set the name of Puck.js without advertising any
other data you can just use the command:

```
NRF.setAdvertising({},{name:"Hello"});
```

You can also specify 'manufacturer data', which is another form of advertising data.
We've registered the Manufacturer ID 0x0590 (as Pur3 Ltd) for use with *Official
Espruino devices* - use it to advertise whatever data you'd like, but we'd recommend
using JSON. 

For example by not advertising a device name you can send up to 24 bytes of JSON on
Espruino's manufacturer ID:

```
var data = {a:1,b:2};
NRF.setAdvertising({},{
  showName:false,
  manufacturer:0x0590,
  manufacturerData:JSON.stringify(data)
});
```

If you're using [EspruinoHub](https://github.com/espruino/EspruinoHub) then it will
automatically decode this into the folling MQTT topics:

* `/ble/advertise/ma:c_:_a:dd:re:ss/espruino` -> `{"a":10,"b":15}`
* `/ble/advertise/ma:c_:_a:dd:re:ss/a` -> `1`
* `/ble/advertise/ma:c_:_a:dd:re:ss/b` -> `2`

Note that **you only have 24 characters available for JSON**, so try to use
the shortest field names possible and avoid floating point values that can
be very long when converted to a String.
*/
void jswrap_ble_setAdvertising(JsVar *data, JsVar *options) {
  uint32_t err_code = 0;
  bool isAdvertising = bleStatus & BLE_IS_ADVERTISING;

  if (jsvIsObject(options)) {
    JsVar *v;

    v = jsvObjectGetChild(options, "interval", 0);
    if (v) {
      uint16_t new_advertising_interval = MSEC_TO_UNITS(jsvGetIntegerAndUnLock(v), UNIT_0_625_MS);
      if (new_advertising_interval<0x0020) new_advertising_interval=0x0020;
      if (new_advertising_interval>0x4000) new_advertising_interval=0x4000;
      if (new_advertising_interval != bleAdvertisingInterval) {
        bleAdvertisingInterval = new_advertising_interval;
      }
    }

    v = jsvObjectGetChild(options, "connectable", 0);
    if (v) {
      if (jsvGetBoolAndUnLock(v)) bleStatus &= ~BLE_IS_NOT_CONNECTABLE;
      else bleStatus |= BLE_IS_NOT_CONNECTABLE;
    }
    v = jsvObjectGetChild(options, "scannable", 0);
    if (v) {
      if (jsvGetBoolAndUnLock(v)) bleStatus &= ~BLE_IS_NOT_SCANNABLE;
      else bleStatus |= BLE_IS_NOT_SCANNABLE;
    }

    v = jsvObjectGetChild(options, "name", 0);
    if (v) {
      JSV_GET_AS_CHAR_ARRAY(namePtr, nameLen, v);
      if (namePtr) {
#ifdef NRF5X
        ble_gap_conn_sec_mode_t sec_mode;
        BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);
        err_code = sd_ble_gap_device_name_set(&sec_mode,
                                              (const uint8_t *)namePtr,
                                              nameLen);
//#else
//        err_code = 0xDEAD;
//        jsiConsolePrintf("FIXME\n");
#endif
#ifdef ESP32
		bluetooth_setDeviceName(v);
#endif
        jsble_check_error(err_code);
      }
      jsvUnLock(v);
    }
  } else if (!jsvIsUndefined(options)) {
    jsExceptionHere(JSET_TYPEERROR, "Expecting 'options' to be object or undefined, got %t", options);
    return;
  }

  JsVar *advArray = 0;

  if (jsvIsObject(data) || jsvIsUndefined(data)) {
    // if it's an object, work out what the advertising data for it is
    advArray = jswrap_ble_getAdvertisingData(data, options);
    // if undefined, make sure we *save* undefined
    if (jsvIsUndefined(data)) {
      advArray = 0;
    }
  } else if (jsvIsArray(data)) {
    advArray = jsvLockAgain(data);
    // Check if it's nested arrays - if so we alternate between advertising types
    bleStatus &= ~(BLE_IS_ADVERTISING_MULTIPLE|BLE_ADVERTISING_MULTIPLE_MASK);
    // check for nested, and if so then preconvert the objects into arrays
    bool isNested = false;
    int elements = 0;
    JsvObjectIterator it;
    jsvObjectIteratorNew(&it, advArray);
    while (jsvObjectIteratorHasValue(&it)) {
      JsVar *v = jsvObjectIteratorGetValue(&it);
      if (jsvIsObject(v) || jsvIsUndefined(v)) {
        JsVar *newv = jswrap_ble_getAdvertisingData(v, options);
        jsvObjectIteratorSetValue(&it, newv);
        jsvUnLock(newv);
        isNested = true;
      } else if (jsvIsArray(v) || jsvIsArrayBuffer(v)) {
        isNested = true;
      }
      elements++;
      jsvUnLock(v);
      jsvObjectIteratorNext(&it);
    }
    jsvObjectIteratorFree(&it);
    // it's nested - set multiple advertising mode
    if (isNested) {
      // nested - enable multiple advertising - start at index 0
      if (elements>1)
        bleStatus |= BLE_IS_ADVERTISING_MULTIPLE;
    }
  } else if (jsvIsArrayBuffer(data)) {
    // it's just data - no multiple advertising
    advArray = jsvLockAgain(data);
    bleStatus &= ~(BLE_IS_ADVERTISING_MULTIPLE|BLE_ADVERTISING_MULTIPLE_MASK);
  }
  // Save the current service data
  jsvObjectSetOrRemoveChild(execInfo.hiddenRoot, BLE_NAME_ADVERTISE_DATA, advArray);
  jsvObjectSetOrRemoveChild(execInfo.hiddenRoot, BLE_NAME_ADVERTISE_OPTIONS, options);
  jsvUnLock(advArray);
  // now actually update advertising
  if (isAdvertising)
    jsble_advertising_stop();
#ifdef ESP32
  err_code = bluetooth_gap_setAdvertizing(advArray);
#endif
  jsble_check_error(err_code);
  if (isAdvertising)
    jsble_check_error(jsble_advertising_start()); // sets up advertising data again
}

/// Used by bluetooth.c internally when it needs to set up advertising at first
JsVar *jswrap_ble_getCurrentAdvertisingData() {
  JsVar *adv = jsvObjectGetChild(execInfo.hiddenRoot, BLE_NAME_ADVERTISE_DATA, 0);
  if (!adv) adv = jswrap_ble_getAdvertisingData(NULL, NULL); // use the defaults
  else {
    if (bleStatus&BLE_IS_ADVERTISING_MULTIPLE) {
      JsVar *v = jsvGetArrayItem(adv, 0);
      jsvUnLock(adv);
      adv = v;
    }
  }
  return adv;
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "getAdvertisingData",
    "generate" : "jswrap_ble_getAdvertisingData",
    "params" : [
      ["data","JsVar","The data to advertise as an object"],
      ["options","JsVar","An optional object of options"]
    ],
    "return" : ["JsVar", "An array containing the advertising data" ]
}
This is just like `NRF.setAdvertising`, except instead of advertising
the data, it returns the packet that would be advertised as an array.
*/
JsVar *jswrap_ble_getAdvertisingData(JsVar *data, JsVar *options) {
  uint32_t err_code;
#ifdef ESP32
  JsVar *r;
  r = bluetooth_gap_getAdvertisingData(data,options);
  return r;
#endif
#ifdef NRF5X
  ble_advdata_t advdata;
  jsble_setup_advdata(&advdata);
#endif

  if (jsvIsObject(options)) {
    JsVar *v;
#ifdef NRF5X
    v = jsvObjectGetChild(options, "showName", 0);
    if (v) advdata.name_type = jsvGetBoolAndUnLock(v) ?
        BLE_ADVDATA_FULL_NAME :
        BLE_ADVDATA_NO_NAME;

    v = jsvObjectGetChild(options, "discoverable", 0);
    if (v) advdata.flags = jsvGetBoolAndUnLock(v) ?
        BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE :
        BLE_GAP_ADV_FLAGS_LE_ONLY_LIMITED_DISC_MODE;

    v = jsvObjectGetChild(options, "manufacturerData", 0);
    if (v) {
      JSV_GET_AS_CHAR_ARRAY(dPtr, dLen, v);
      if (dPtr && dLen) {
        advdata.p_manuf_specific_data = (ble_advdata_manuf_data_t*)alloca(sizeof(ble_advdata_manuf_data_t));
        advdata.p_manuf_specific_data->company_identifier = 0xFFFF; // pre-fill with test manufacturer data
        advdata.p_manuf_specific_data->data.size = dLen;
        advdata.p_manuf_specific_data->data.p_data = (uint8_t*)dPtr;
      }
      jsvUnLock(v);
    }
    v = jsvObjectGetChild(options, "manufacturer", 0);
    if (v) {
      if (advdata.p_manuf_specific_data)
        advdata.p_manuf_specific_data->company_identifier = jsvGetInteger(v);
      else
        jsExceptionHere(JSET_TYPEERROR, "'manufacturer' specified without 'manufacturerdata'");
      jsvUnLock(v);
    }
#endif
  } else if (!jsvIsUndefined(options)) {
    jsExceptionHere(JSET_TYPEERROR, "Expecting 'options' to be object or undefined, got %t", options);
    return 0;
  }

  if (jsvIsArray(data) || jsvIsArrayBuffer(data)) {
    return jsvLockAgain(data);
  } else if (jsvIsObject(data)) {
#ifdef NRF5X
    ble_advdata_service_data_t *service_data = (ble_advdata_service_data_t*)alloca(jsvGetChildren(data)*sizeof(ble_advdata_service_data_t));
#endif
    int n = 0;
    JsvObjectIterator it;
    jsvObjectIteratorNew(&it, data);
    while (jsvObjectIteratorHasValue(&it)) {
      JsVar *v = jsvObjectIteratorGetValue(&it);
      JSV_GET_AS_CHAR_ARRAY(dPtr, dLen, v);
      jsvUnLock(v);
#ifdef NRF5X
      service_data[n].service_uuid = jsvGetIntegerAndUnLock(jsvObjectIteratorGetKey(&it));
      service_data[n].data.size    = dLen;
      service_data[n].data.p_data  = (uint8_t*)dPtr;
#endif
      jsvObjectIteratorNext(&it);
      n++;
    }
    jsvObjectIteratorFree(&it);
#ifdef NRF5X
    advdata.service_data_count   = n;
    advdata.p_service_data_array = service_data;
#endif
  } else if (!jsvIsUndefined(data)) {
    jsExceptionHere(JSET_TYPEERROR, "Expecting object, array or undefined, got %t", data);
    return 0;
  }

  uint16_t  len_advdata = BLE_GAP_ADV_MAX_SIZE;
  uint8_t   encoded_advdata[BLE_GAP_ADV_MAX_SIZE];

#ifdef NRF5X
#if NRF_SD_BLE_API_VERSION<5
  err_code = adv_data_encode(&advdata, encoded_advdata, &len_advdata);
#else
  err_code = ble_advdata_encode(&advdata, encoded_advdata, &len_advdata);
#endif
#else
  err_code = 0xDEAD;
  jsiConsolePrintf("FIXME\n");
#endif
  if (jsble_check_error(err_code)) return 0;
  return jsvNewArrayBufferWithData(len_advdata, encoded_advdata);
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "setScanResponse",
    "generate" : "jswrap_ble_setScanResponse",
    "params" : [
      ["data","JsVar","The data to for the scan response"]
    ]
}

The raw scan response data should be supplied as an array. For example to return "Sample" for the device name:

```
NRF.setScanResponse([0x07,  // Length of Data
  0x09,  // Param: Complete Local Name
  'S', 'a', 'm', 'p', 'l', 'e']);
```

**Note:** `NRF.setServices(..., {advertise:[ ... ]})` writes advertised
services into the scan response - so you can't use both `advertise`
and `NRF.setServices` or one will overwrite the other.
*/
void jswrap_ble_setScanResponse(JsVar *data) {
  uint32_t err_code = 0;

  jsvObjectSetOrRemoveChild(execInfo.hiddenRoot, BLE_NAME_SCAN_RESPONSE_DATA, data);

  if (jsvIsArray(data) || jsvIsArrayBuffer(data)) {
    JSV_GET_AS_CHAR_ARRAY(dPtr, dLen, data);
    if (!dPtr) {
      jsExceptionHere(JSET_TYPEERROR, "Unable to convert data argument to an array");
      return;
    }
#ifdef NRF5X
#if NRF_SD_BLE_API_VERSION<5
    err_code = sd_ble_gap_adv_data_set(NULL, 0, (uint8_t *)dPtr, dLen);
#else
    jsWarn("setScanResponse not working on SDK15\n");
#endif
#else
    err_code = 0xDEAD;
    jsiConsolePrintf("FIXME\n");
#endif
    jsble_check_error(err_code);
  } else if (!jsvIsUndefined(data)) {
    jsExceptionHere(JSET_TYPEERROR, "Expecting array-like object or undefined, got %t", data);
  }
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "setServices",
    "generate" : "jswrap_ble_setServices",
    "params" : [
      ["data","JsVar","The service (and characteristics) to advertise"],
      ["options","JsVar","Optional object containing options"]
    ]
}

Change the services and characteristics Espruino advertises.

If you want to **change** the value of a characteristic, you need
to use `NRF.updateServices()` instead

To expose some information on Characteristic `ABCD` on service `BCDE` you could do:

```
NRF.setServices({
  0xBCDE : {
    0xABCD : {
      value : "Hello",
      readable : true
    }
  }
});
```

Or to allow the 3 LEDs to be controlled by writing numbers 0 to 7 to a
characteristic, you can do the following. `evt.data` is an ArrayBuffer.

```
NRF.setServices({
  0xBCDE : {
    0xABCD : {
      writable : true,
      onWrite : function(evt) {
        digitalWrite([LED3,LED2,LED1], evt.data[0]);
      }
    }
  }
});
```

You can supply many different options:

```
NRF.setServices({
  0xBCDE : {
    0xABCD : {
      value : "Hello", // optional
      maxLen : 5, // optional (otherwise is length of initial value)
      broadcast : false, // optional, default is false
      readable : true,   // optional, default is false
      writable : true,   // optional, default is false
      notify : true,   // optional, default is false
      indicate : true,   // optional, default is false
      description: "My Characteristic",  // optional, default is null,
      security: { // optional - see NRF.setSecurity
        read: { // optional
          encrypted: false, // optional, default is false
          mitm: false, // optional, default is false
          lesc: false, // optional, default is false
          signed: false // optional, default is false
        },
        write: { // optional
          encrypted: true, // optional, default is false
          mitm: false, // optional, default is false
          lesc: false, // optional, default is false
          signed: false // optional, default is false
        }
      },
      onWrite : function(evt) { // optional
        console.log("Got ", evt.data); // an ArrayBuffer
      }
    }
    // more characteristics allowed
  }
  // more services allowed
});
```

**Note:** UUIDs can be integers between `0` and `0xFFFF`, strings of
the form `"ABCD"`, or strings of the form `"ABCDABCD-ABCD-ABCD-ABCD-ABCDABCDABCD"`

`options` can be of the form:

```
NRF.setServices(undefined, {
  hid : new Uint8Array(...), // optional, default is undefined. Enable BLE HID support
  uart : true, // optional, default is true. Enable BLE UART support
  advertise: [ '180D' ] // optional, list of service UUIDs to advertise
});
```

To enable BLE HID, you must set `hid` to an array which is the BLE report
descriptor. The easiest way to do this is to use the `ble_hid_controls`
or `ble_hid_keyboard` modules.

**Note:** Just creating a service doesn't mean that the service will
be advertised. It will only be available after a device connects. To
advertise, specify the UUIDs you wish to advertise in the `advertise`
field of the second `options` argument. For example this will create
and advertise a heart rate service:

```
NRF.setServices({
  0x180D: { // heart_rate
    0x2A37: { // heart_rate_measurement
      notify: true,
      value : [0x06, heartrate],
    }
  }
}, { advertise: [ '180D' ] });
```

You may specify 128 bit UUIDs to advertise, however you may get a `DATA_SIZE`
exception because there is insufficient space in the Bluetooth LE advertising
packet for the 128 bit UART UUID as well as the UUID you specified. In this
case you can add `uart:false` after the `advertise` element to disable the
UART, however you then be unable to connect to Puck.js's console via Bluetooth.

If you absolutely require two or more 128 bit UUIDs then you will have to
specify your own raw advertising data packets with `NRF.setAdvertising`

**Note:** The services on Espruino can only be modified when there is
no device connected to it as it requires a restart of the Bluetooth stack.
**iOS devices will 'cache' the list of services** so apps like
NRF Connect may incorrectly display the old services even after you 
have modified them. To fix this, disable and re-enable Bluetooth on your
iOS device, or use an Android device to run NRF Connect.

**Note:** Not all combinations of security configuration values are valid, the valid combinations are: encrypted,
encrypted + mitm, lesc, signed, signed + mitm. See `NRF.setSecurity` for more information.
*/
void jswrap_ble_setServices(JsVar *data, JsVar *options) {
  if (!(jsvIsObject(data) || jsvIsUndefined(data))) {
    jsExceptionHere(JSET_TYPEERROR, "Expecting object or undefined, got %t", data);
    return;
  }

#if BLE_HIDS_ENABLED
  JsVar *use_hid = 0;
#endif
  bool use_uart = true;
  JsVar *advertise = 0;

  jsvConfigObject configs[] = {
#if BLE_HIDS_ENABLED
      {"hid", JSV_ARRAY, &use_hid},
#endif
      {"uart", JSV_BOOLEAN, &use_uart},
      {"advertise",  JSV_ARRAY, &advertise},
  };
  if (!jsvReadConfigObject(options, configs, sizeof(configs) / sizeof(jsvConfigObject))) {
    return;
  }

#if BLE_HIDS_ENABLED
  // Handle turning on/off of HID
  if (jsvIsIterable(use_hid)) {
    jsvObjectSetChild(execInfo.hiddenRoot, BLE_NAME_HID_DATA, use_hid);
    bleStatus |= BLE_NEEDS_SOFTDEVICE_RESTART;
  } else if (!use_hid) {
    jsvObjectRemoveChild(execInfo.hiddenRoot, BLE_NAME_HID_DATA);
    if (bleStatus & BLE_HID_INITED)
      bleStatus |= BLE_NEEDS_SOFTDEVICE_RESTART;
  } else {
    jsExceptionHere(JSET_TYPEERROR, "'hid' must be undefined, or an array");
  }
  jsvUnLock(use_hid);
#endif
  if (use_uart) {
    if (!(bleStatus & BLE_NUS_INITED))
      bleStatus |= BLE_NEEDS_SOFTDEVICE_RESTART;
    jsvObjectRemoveChild(execInfo.hiddenRoot, BLE_NAME_NUS);
  } else {
    if (bleStatus & BLE_NUS_INITED)
      bleStatus |= BLE_NEEDS_SOFTDEVICE_RESTART;
    jsvObjectSetChildAndUnLock(execInfo.hiddenRoot, BLE_NAME_NUS, jsvNewFromBool(false));
  }

  // Save the current service data and options
  jsvObjectSetOrRemoveChild(execInfo.hiddenRoot, BLE_NAME_SERVICE_DATA, data);
  jsvObjectSetOrRemoveChild(execInfo.hiddenRoot, BLE_NAME_SERVICE_OPTIONS, options);
  // Service UUIDs to advertise
  if (advertise) bleStatus|=BLE_NEEDS_SOFTDEVICE_RESTART;
  jsvObjectSetOrRemoveChild(execInfo.hiddenRoot, BLE_NAME_SERVICE_ADVERTISE, advertise);
  jsvUnLock(advertise);

  // work out whether to apply changes
  if (bleStatus & (BLE_SERVICES_WERE_SET|BLE_NEEDS_SOFTDEVICE_RESTART)) {
    jswrap_ble_restart();
  } else {
    /* otherwise, we can set the services now, since we're only adding
     * and not changing anything we don't need a restart. */
    jsble_set_services(data);
  }
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "updateServices",
    "generate" : "jswrap_ble_updateServices",
    "params" : [
      ["data","JsVar","The service (and characteristics) to update"]
    ]
}

Update values for the services and characteristics Espruino advertises.
Only services and characteristics previously declared using `NRF.setServices` are affected.

To update the '0xABCD' characteristic in the '0xBCDE' service:

```
NRF.updateServices({
  0xBCDE : {
    0xABCD : {
      value : "World"
    }
  }
});
```

You can also use 128 bit UUIDs, for example `"b7920001-3c1b-4b40-869f-3c0db9be80c6"`.

To define a service and characteristic and then notify connected clients of a
change to it when a button is pressed:

```
NRF.setServices({
  0xBCDE : {
    0xABCD : {
      value : "Hello",
      maxLen : 20,
      notify: true
    }
  }
});
setWatch(function() {
  NRF.updateServices({
    0xBCDE : {
      0xABCD : {
        value : "World!",
        notify: true
      }
    }
  });
}, BTN, { repeat:true, edge:"rising", debounce: 50 });
```

This only works if the characteristic was created with `notify: true` using `NRF.setServices`,
otherwise the characteristic will be updated but no notification will be sent.

Also note that `maxLen` was specified. If it wasn't then the maximum length of
the characteristic would have been 5 - the length of `"Hello"`.

To indicate (i.e. notify with ACK) connected clients of a change to the '0xABCD' characteristic in the '0xBCDE' service:

```
NRF.updateServices({
  0xBCDE : {
    0xABCD : {
      value : "World",
      indicate: true
    }
  }
});
```

This only works if the characteristic was created with `indicate: true` using `NRF.setServices`,
otherwise the characteristic will be updated but no notification will be sent.

**Note:** See `NRF.setServices` for more information
*/
void jswrap_ble_updateServices(JsVar *data) {
  uint32_t err_code;
  bool ok = true;

  if (bleStatus & BLE_NEEDS_SOFTDEVICE_RESTART) {
    jsExceptionHere(JSET_ERROR, "Can't update services until BLE restart");
    /* TODO: We could conceivably update hiddenRoot->BLE_NAME_SERVICE_DATA so that
    when the softdevice restarts it contains the updated data, but this seems like
    overkill and potentially could cause nasty hidden bugs. */
    return;
  }

#ifdef NRF5X
  jsble_peripheral_activity(); // flag that we've been busy
#endif

  if (jsvIsObject(data)) {
    JsvObjectIterator it;
    jsvObjectIteratorNew(&it, data);
    while (jsvObjectIteratorHasValue(&it)) {
      ble_uuid_t ble_uuid;
      memset(&ble_uuid, 0, sizeof(ble_uuid));

      const char *errorStr;
      if ((errorStr = bleVarToUUIDAndUnLock(&ble_uuid,
          jsvObjectIteratorGetKey(&it)))) {
        jsExceptionHere(JSET_ERROR, "Invalid Service UUID: %s", errorStr);
        break;
      }

      JsVar *serviceVar = jsvObjectIteratorGetValue(&it);
      JsvObjectIterator serviceit;
      jsvObjectIteratorNew(&serviceit, serviceVar);

      while (ok && jsvObjectIteratorHasValue(&serviceit)) {
        ble_uuid_t char_uuid;
        if ((errorStr = bleVarToUUIDAndUnLock(&char_uuid,
            jsvObjectIteratorGetKey(&serviceit)))) {
          jsExceptionHere(JSET_ERROR, "Invalid Characteristic UUID: %s",
              errorStr);
          break;
        }

        uint16_t char_handle = bleGetGATTHandle(char_uuid);
        if (char_handle != BLE_GATT_HANDLE_INVALID) {
          JsVar *charVar = jsvObjectIteratorGetValue(&serviceit);
          JsVar *charValue = jsvObjectGetChild(charVar, "value", 0);

          bool notification_requested = jsvGetBoolAndUnLock(jsvObjectGetChild(charVar, "notify", 0));
          bool indication_requested = jsvGetBoolAndUnLock(jsvObjectGetChild(charVar, "indicate", 0));

          if (charValue) {
            JSV_GET_AS_CHAR_ARRAY(vPtr, vLen, charValue);
            if (vPtr && vLen) {
#ifdef NRF5X
              ble_gatts_hvx_params_t hvx_params;
              ble_gatts_value_t gatts_value;

              // Update the value for subsequent reads even if no client is currently connected
              memset(&gatts_value, 0, sizeof(gatts_value));
              gatts_value.len = vLen;
              gatts_value.offset = 0;
              gatts_value.p_value = (uint8_t*)vPtr;
              err_code = sd_ble_gatts_value_set(m_peripheral_conn_handle, char_handle, &gatts_value);
              if (jsble_check_error(err_code)) {
                ok = false;
              } if ((notification_requested || indication_requested) && jsble_has_peripheral_connection()) {
                // Notify/indicate connected clients if necessary
                memset(&hvx_params, 0, sizeof(hvx_params));
                uint16_t len = (uint16_t)vLen;
                hvx_params.handle = char_handle;
                hvx_params.type = indication_requested ? BLE_GATT_HVX_INDICATION : BLE_GATT_HVX_NOTIFICATION;
                hvx_params.offset = 0;
                hvx_params.p_len = &len;
                hvx_params.p_data = (uint8_t*)vPtr;

                err_code = sd_ble_gatts_hvx(m_peripheral_conn_handle, &hvx_params);
                if ((err_code != NRF_SUCCESS)
                  && (err_code != NRF_ERROR_INVALID_STATE)
#if NRF_SD_BLE_API_VERSION<5
                  && (err_code != BLE_ERROR_NO_TX_PACKETS)
#else
                  && (err_code != NRF_ERROR_RESOURCES)
#endif
                  && (err_code != BLE_ERROR_GATTS_SYS_ATTR_MISSING)) {
                  if (jsble_check_error(err_code))
                    ok = false;
                }
              }
#endif
            }
          }
          jsvUnLock(charValue);
          jsvUnLock(charVar);
        } else {
          JsVar *str = bleUUIDToStr(char_uuid);
          jsExceptionHere(JSET_ERROR, "Unable to find service with UUID %v", str);
          jsvUnLock(str);
        }

        jsvObjectIteratorNext(&serviceit);
      }
      jsvObjectIteratorFree(&serviceit);
      jsvUnLock(serviceVar);

      jsvObjectIteratorNext(&it);
    }
    jsvObjectIteratorFree(&it);

  } else if (!jsvIsUndefined(data)) {
    jsExceptionHere(JSET_TYPEERROR, "Expecting object or undefined, got %t", data);
  }
}


/// Filter device based on a list of filters (like .requestDevice. Return true if it matches ANY of the filters
bool jswrap_ble_filter_device(JsVar *filters, JsVar *device) {
  bool matches = false;
  JsvObjectIterator fit;
  jsvObjectIteratorNew(&fit, filters);
  while (!matches && jsvObjectIteratorHasValue(&fit)) {
    JsVar *filter = jsvObjectIteratorGetValue(&fit);
    matches = true;
    JsVar *v;
    if ((v = jsvObjectGetChild(filter, "services", 0))) {
      // Find one service in the device's service
      JsVar *deviceServices = jsvObjectGetChild(device, "services", 0);
      JsvObjectIterator it;
      jsvObjectIteratorNew(&it, v);
      while (jsvObjectIteratorHasValue(&it)) {
        bool foundService = false;
        if (deviceServices) {
          JsVar *uservice = jsvObjectIteratorGetValue(&it);
          ble_uuid_t userviceUuid;
          bleVarToUUIDAndUnLock(&userviceUuid, uservice);
          JsvObjectIterator dit;
          jsvObjectIteratorNew(&dit, deviceServices);
          while (jsvObjectIteratorHasValue(&dit)) {
            JsVar *deviceService = jsvObjectIteratorGetValue(&dit);
            ble_uuid_t deviceServiceUuid;
            bleVarToUUIDAndUnLock(&deviceServiceUuid, deviceService);
            if (bleUUIDEqual(userviceUuid, deviceServiceUuid))
              foundService = true;
            jsvObjectIteratorNext(&dit);
          }
          jsvObjectIteratorFree(&dit);
        }
        if (!foundService) matches = false;
        jsvObjectIteratorNext(&it);
      }
      jsvObjectIteratorFree(&it);
      jsvUnLock2(v, deviceServices);
    }
    if ((v = jsvObjectGetChild(filter, "name", 0))) {
      // match name exactly
      JsVar *deviceName = jsvObjectGetChild(device, "name", 0);
      if (!jsvIsEqual(v, deviceName))
        matches = false;
      jsvUnLock2(v, deviceName);
    }
    if ((v = jsvObjectGetChild(filter, "namePrefix", 0))) {
      // match start of name
      JsVar *deviceName = jsvObjectGetChild(device, "name", 0);
      if (!jsvIsString(v) ||
          !jsvIsString(deviceName) ||
          jsvGetStringLength(v)>jsvGetStringLength(deviceName) ||
          jsvCompareString(v, deviceName,0,0,true)!=0)
        matches = false;
      jsvUnLock2(v, deviceName);
    }
    // Non-standard 'id' element
    if ((v = jsvObjectGetChild(filter, "id", 0))) {
      JsVar *w = jsvObjectGetChild(device, "id", 0);
      if (!jsvIsBasicVarEqual(v,w))
        matches = false;
      jsvUnLock2(v,w);
    }
    // match service data
    if ((v = jsvObjectGetChild(filter, "serviceData", 0))) {
      if (jsvIsObject(v)) {
        JsvObjectIterator it;
        jsvObjectIteratorNew(&it,v);
        while (jsvObjectIteratorHasValue(&it)) {
          JsVar *childName = jsvObjectIteratorGetKey(&it);
          JsVar *serviceData = jsvObjectGetChild(device, "serviceData", 0);
          if (!serviceData) matches = false;
          else {
            JsVar *child = jsvFindChildFromVar(serviceData, childName, false);
            if (!child) matches = false;
            jsvUnLock(child);
          }
          jsvUnLock2(childName, serviceData);
          jsvObjectIteratorNext(&it);
        }
        jsvObjectIteratorFree(&it);
      }
      jsvUnLock(v);
    }
    // match manufacturer data
    if ((v = jsvObjectGetChild(filter, "manufacturerData", 0))) {
      if (jsvIsObject(v)) {
        JsvObjectIterator it;
        jsvObjectIteratorNew(&it,v);
        while (jsvObjectIteratorHasValue(&it)) {
          JsVar* manfacturera = jsvObjectIteratorGetKey(&it);
          JsVar* manfacturerb = jsvObjectGetChild(device, "manufacturer", 0);
          if (!jsvIsBasicVarEqual(manfacturera, manfacturerb))
            matches = false;
          jsvUnLock2(manfacturera, manfacturerb);
          jsvObjectIteratorNext(&it);
        }
        jsvObjectIteratorFree(&it);
      }
      jsvUnLock(v);
    }
    // check if all ok
    jsvUnLock(filter);
    jsvObjectIteratorNext(&fit);
  }
  jsvObjectIteratorFree(&fit);
  return matches;
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "setScan",
    "generate" : "jswrap_ble_setScan",
    "params" : [
      ["callback","JsVar","The callback to call with received advertising packets, or undefined to stop"],
      ["options","JsVar","An optional object `{filters: ...}` (as would be passed to `NRF.requestDevice`) to filter devices by"]
    ]
}

Start/stop listening for BLE advertising packets within range. Returns a
`BluetoothDevice` for each advertsing packet. **By default this is not an active scan, so
Scan Response advertising data is not included (see below)**

```
// Start scanning
packets=10;
NRF.setScan(function(d) {
  packets--;
  if (packets<=0)
    NRF.setScan(); // stop scanning
  else
    console.log(d); // print packet info
});
```

Each `BluetoothDevice` will look a bit like:

```
BluetoothDevice {
  "id": "aa:bb:cc:dd:ee:ff", // address
  "rssi": -89,               // signal strength
  "services": [ "128bit-uuid", ... ],     // zero or more service UUIDs
  "data": new Uint8Array([ ... ]).buffer, // ArrayBuffer of returned data
  "serviceData" : { "0123" : [ 1 ] }, // if service data is in 'data', it's extracted here
  "manufacturer" : 0x1234, // if manufacturer data is in 'data', the 16 bit manufacturer ID is extracted here
  "manufacturerData" : [...], // if manufacturer data is in 'data', the data is extracted here
  "name": "DeviceName"       // the advertised device name
 }
```

You can also supply a set of filters (as decribed in `NRF.requestDevice`) as a second argument, which will
allow you to filter the devices you get a callback for. This helps
to cut down on the time spent processing JavaScript code in areas with
a lot of Bluetooth advertisements. For example to find only devices
with the manufacturer data `0x0590` (Espruino's ID) you could do:

```
NRF.setScan(function(d) {
  console.log(d.manufacturerData);
}, { filters: [{ manufacturerData:{0x0590:{}} }] });
```

You can also specify `active:true` in the second argument to perform
active scanning (this requests scan response packets) from any
devices it finds.

**Note:** BLE advertising packets can arrive quickly - faster than you'll
be able to print them to the console. It's best only to print a few, or
to use a function like `NRF.findDevices(..)` which will collate a list
of available devices.

**Note:** Using setScan turns the radio's receive mode on constantly. This
can draw a *lot* of power (12mA or so), so you should use it sparingly or
you can run your battery down quickly.
*/
void jswrap_ble_setScan_cb(JsVar *callback, JsVar *filters, JsVar *adv) {
  /* This is called when we get data - do some processing here in the main loop
  then call the callback with it (it avoids us doing more allocations than
  needed inside the IRQ) */
  if (!adv) return;
  // Create a proper BluetoothDevice object
  JsVar *device = jspNewObject(0, "BluetoothDevice");
  jsvObjectSetChildAndUnLock(device, "id", jsvObjectGetChild(adv, "id", 0));
  jsvObjectSetChildAndUnLock(device, "rssi", jsvObjectGetChild(adv, "rssi", 0));
  JsVar *services = jsvNewEmptyArray();
  JsVar *serviceData = jsvNewObject();
  JsVar *data = jsvObjectGetChild(adv, "data", 0);
  if (data) {
    jsvObjectSetChild(device, "data", data);
    JSV_GET_AS_CHAR_ARRAY(dPtr, dLen, data);
    if (dPtr && dLen) {
      if (services && serviceData) {
        uint32_t i = 0;
        while (i < dLen) {
          uint8_t field_length = dPtr[i];
          uint8_t field_type   = dPtr[i + 1];

          if (field_type == BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME) { // 0x08 - Short Name
            jsvObjectSetChildAndUnLock(device, "shortName", jsvNewStringOfLength(field_length-1, (char*)&dPtr[i+2]));
          } else if (field_type == BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME) { // 0x09 - Complete Name
            jsvObjectSetChildAndUnLock(device, "name", jsvNewStringOfLength(field_length-1, (char*)&dPtr[i+2]));
          } else if (field_type == BLE_GAP_AD_TYPE_16BIT_SERVICE_UUID_MORE_AVAILABLE || // 0x02, 0x03 - 16 bit UUID
                     field_type == BLE_GAP_AD_TYPE_16BIT_SERVICE_UUID_COMPLETE) {
            for (int svc_idx = 2; svc_idx < field_length + 1; svc_idx += 2) {
              JsVar *s = jsvVarPrintf("%04x", UNALIGNED_UINT16(&dPtr[i+svc_idx]));
              jsvArrayPushAndUnLock(services, s);
            }
          } else if (field_type == BLE_GAP_AD_TYPE_128BIT_SERVICE_UUID_MORE_AVAILABLE || // 0x06, 0x07 - 128 bit UUID
                     field_type == BLE_GAP_AD_TYPE_128BIT_SERVICE_UUID_COMPLETE) {
            JsVar *s = bleUUID128ToStr((uint8_t*)&dPtr[i+2]);
            jsvArrayPushAndUnLock(services, s);
          } else if (field_type == BLE_GAP_AD_TYPE_SERVICE_DATA) { // 0x16 - service data 16 bit UUID
            JsVar *childName = jsvAsArrayIndexAndUnLock(jsvVarPrintf("%04x", UNALIGNED_UINT16(&dPtr[i+2])));
            if (childName) {
              JsVar *child = jsvFindChildFromVar(serviceData, childName, true);
              JsVar *value = jsvNewArrayBufferWithData(field_length-3, (unsigned char*)&dPtr[i+4]);
              if (child && value) jsvSetValueOfName(child, value);
              jsvUnLock2(child, value);
            }
            jsvUnLock(childName);
          } else if (field_type == BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA) {
            jsvObjectSetChildAndUnLock(device, "manufacturer",
                            jsvNewFromInteger((dPtr[i+3]<<8) | dPtr[i+2]));
            jsvObjectSetChildAndUnLock(device, "manufacturerData",
                jsvNewArrayBufferWithData(field_length-3, (unsigned char*)&dPtr[i+4]));
          } // or unknown...
          i += field_length + 1;
        }
      }
    }
  }
  if (jsvGetArrayLength(services))
    jsvObjectSetChild(device, "services", services);
  if (jsvGetLength(serviceData))
    jsvObjectSetChild(device, "serviceData", serviceData);
  jsvUnLock3(data, services, serviceData);

  if (!filters || jswrap_ble_filter_device(filters, device))
    jspExecuteFunction(callback, 0, 1, &device);
  jsvUnLock(device);
}

void jswrap_ble_setScan(JsVar *callback, JsVar *options) {
  JsVar *filters = 0;
  bool activeScan = false;
  if (jsvIsObject(options)) {
    activeScan = jsvGetBoolAndUnLock(jsvObjectGetChild(options, "active", 0));
    filters = jsvObjectGetChild(options, "filters", 0);
    if (filters && !jsvIsArray(filters)) {
      jsvUnLock(filters);
      jsExceptionHere(JSET_TYPEERROR, "requestDevice expecting an array of filters, got %t", filters);
      return;
    }
  } else if (options)
    jsExceptionHere(JSET_TYPEERROR, "Expecting Object got %t\n", options);
  // set the callback event variable
  if (!jsvIsFunction(callback)) callback=0;
  if (callback) {
    JsVar *fn = jsvNewNativeFunction((void (*)(void))jswrap_ble_setScan_cb, JSWAT_THIS_ARG|(JSWAT_JSVAR<<JSWAT_BITS)|(JSWAT_JSVAR<<(JSWAT_BITS*2)));
    if (fn) {
      jsvAddFunctionParameter(fn, 0, filters); // bind param 1
      jsvObjectSetChild(fn, JSPARSE_FUNCTION_THIS_NAME, callback); // bind 'this'
      jsvObjectSetChild(execInfo.root, BLE_SCAN_EVENT, fn);
      jsvUnLock(fn);
    }
  } else {
    jsvObjectRemoveChild(execInfo.root, BLE_SCAN_EVENT);
  }
  // either start or stop scanning
  uint32_t err_code = jsble_set_scanning(callback != 0, activeScan);
  jsble_check_error(err_code);
  jsvUnLock(filters);
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "filterDevices",
    "generate" : "jswrap_ble_filterDevices",
    "params" : [
      ["devices","JsVar","An array of `BluetoothDevice` objects, from `NRF.findDevices` or similar"],
      ["filters","JsVar","A list of filters (as would be passed to `NRF.requestDevice`) to filter devices by"]
    ],
    "return" : ["JsVar","An array of `BluetoothDevice` objects that match the given filters"]
}
This function can be used to quickly filter through Bluetooth devices.

For instance if you wish to scan for multiple different types of device at the same time
then you could use `NRF.findDevices` with all the filters you're interested in. When scanning
is finished you can then use `NRF.filterDevices` to pick out just the devices of interest.

```
// the two types of device we're interested in
var filter1 = [{serviceData:{"fe95":{}}}];
var filter2 = [{namePrefix:"Pixl.js"}];
// the following filter will return both types of device
var allFilters = filter1.concat(filter2);
// now scan for both types of device, and filter them out afterwards
NRF.findDevices(function(devices) {
  var devices1 = NRF.filterDevices(devices, filter1);
  var devices2 = NRF.filterDevices(devices, filter2);
  // ...
}, {filters : allFilters});
```

*/
JsVar *jswrap_ble_filterDevices(JsVar *devices, JsVar *filters) {
  if (!jsvIsArray(devices) || !jsvIsArray(filters)) {
    jsExceptionHere(JSET_TYPEERROR, "Expecting both arguments to be arrays");
    return 0;
  }
  JsVar *result = jsvNewEmptyArray();
  if (!result) return 0;
  JsvObjectIterator it;
  jsvObjectIteratorNew(&it, devices);
  while (jsvObjectIteratorHasValue(&it)) {
    JsVar *device = jsvObjectIteratorGetValue(&it);
    if (jswrap_ble_filter_device(filters, device))
      jsvArrayPush(result, device);
    jsvUnLock(device);
    jsvObjectIteratorNext(&it);
  }
  jsvObjectIteratorFree(&it);
  return result;
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "findDevices",
    "generate" : "jswrap_ble_findDevices",
    "params" : [
      ["callback","JsVar","The callback to call with received advertising packets (as `BluetoothDevice`), or undefined to stop"],
      ["options","JsVar","A time in milliseconds to scan for (defaults to 2000), Or an optional object `{filters: ..., timeout : ..., active: bool}` (as would be passed to `NRF.requestDevice`) to filter devices by"]
    ]
}
Utility function to return a list of BLE devices detected in range. Behind the scenes,
this uses `NRF.setScan(...)` and collates the results.

```
NRF.findDevices(function(devices) {
  console.log(devices);
}, 1000);
```

prints something like:

```
[
  BluetoothDevice {
    "id": "e7:e0:57:ad:36:a2 random",
    "rssi": -45,
    "services": [ "4567" ],
    "serviceData" : { "0123" : [ 1 ] },
    "manufacturerData" : [...],
    "data": new ArrayBuffer([ ... ]),
    "name": "Puck.js 36a2"
   },
  BluetoothDevice {
    "id": "c0:52:3f:50:42:c9 random",
    "rssi": -65,
    "data": new ArrayBuffer([ ... ]),
    "name": "Puck.js 8f57"
   }
 ]
```

For more information on the structure returned, see `NRF.setScan`.

If you want to scan only for specific devices you can replace the timeout with an object
of the form `{filters: ..., timeout : ..., active: bool}` using the filters
described in `NRF.requestDevice`. For example to search for devices with Espruino's `manufacturerData`:

```
NRF.findDevices(function(devices) {
  ...
}, {timeout : 2000, filters : [{ manufacturerData:{0x0590:{}} }] });
```

You could then use [`BluetoothDevice.gatt.connect(...)`](/Reference#l_BluetoothRemoteGATTServer_connect) on
the device returned to make a connection.

You can also use [`NRF.connect(...)`](/Reference#l_NRF_connect) on just the `id` string returned, which
may be useful if you always want to connect to a specific device.

**Note:** Using findDevices turns the radio's receive mode on for 2000ms (or however long you specify). This
can draw a *lot* of power (12mA or so), so you should use it sparingly or you can run your battery down quickly.

**Note:** The 'data' field contains the data of *the last packet received*. There may have been more
packets. To get data for each packet individually use `NRF.setScan` instead.
*/
void jswrap_ble_findDevices_found_cb(JsVar *device) {
  JsVar *arr = jsvObjectGetChild(execInfo.hiddenRoot, "BLEADV", JSV_ARRAY);
  if (!arr) return;
  JsVar *deviceAddr = jsvObjectGetChild(device, "id", 0);
  JsVar *found = 0;
  JsvObjectIterator it;
  jsvObjectIteratorNew(&it, arr);
  while (!found && jsvObjectIteratorHasValue(&it)) {
    JsVar *obj = jsvObjectIteratorGetValue(&it);
    JsVar *addr = jsvObjectGetChild(obj, "id", 0);
    if (jsvCompareString(addr, deviceAddr, 0, 0, true) == 0)
      found = jsvLockAgain(obj);
    jsvUnLock2(addr, obj);
    jsvObjectIteratorNext(&it);
  }
  jsvObjectIteratorFree(&it);
  if (found) {
    JsvObjectIterator oit;
    jsvObjectIteratorNew(&oit, device);
    while (jsvObjectIteratorHasValue(&oit)) {
      JsVar *key = jsvObjectIteratorGetKey(&oit);
      JsVar *value = jsvSkipName(key);
      JsVar *existingKey = jsvFindChildFromVar(found, key, true);
      bool isServices = jsvIsStringEqual(key,"services");
      bool isServiceData = jsvIsStringEqual(key,"serviceData");
      if (isServices || isServiceData) {
        // for services or servicedata we append to the array/object
        JsVar *existingValue = jsvSkipName(existingKey);
        if (existingValue) {
          if (isServices) {
            jsvArrayPushAll(existingValue, value, true);
          } else {
            jsvObjectAppendAll(existingValue, value);
          }
          jsvUnLock(existingValue);
        } else // nothing already - just copy
          jsvSetValueOfName(existingKey, value);
      }
      jsvUnLock3(existingKey, key, value);
      jsvObjectIteratorNext(&oit);
    }
    jsvObjectIteratorFree(&oit);
  } else
    jsvArrayPush(arr, device);
  jsvUnLock3(found, deviceAddr, arr);
}
void jswrap_ble_findDevices_timeout_cb() {
  jswrap_ble_setScan(0,0);
  JsVar *arr = jsvObjectGetChild(execInfo.hiddenRoot, "BLEADV", JSV_ARRAY);
  JsVar *cb = jsvObjectGetChild(execInfo.hiddenRoot, "BLEADVCB", 0);
  jsvObjectRemoveChild(execInfo.hiddenRoot, "BLEADV");
  jsvObjectRemoveChild(execInfo.hiddenRoot, "BLEADVCB");
  if (arr && cb) {
    jsiQueueEvents(0, cb, &arr, 1);
  }
  jsvUnLock2(arr,cb);
}
void jswrap_ble_findDevices(JsVar *callback, JsVar *options) {
  JsVarFloat time = 2000;
  if (!jsvIsFunction(callback)) {
    jsExceptionHere(JSET_ERROR, "Expecting function for first argument, got %t", callback);
    return;
  }
  if (jsvIsNumeric(options)) {
    time = jsvGetFloat(options);
    options = 0;
  } else if (jsvIsObject(options)) {
    JsVar *v = jsvObjectGetChild(options,"timeout",0);
    if (v) time = jsvGetFloatAndUnLock(v);
  } else if (options) {
    jsExceptionHere(JSET_ERROR, "Expecting number or object, got %t", options);
    return;
  }
  if (isnan(time) || time < 10) {
    jsExceptionHere(JSET_ERROR, "Invalid timeout");
    return;
  }

  jsvObjectSetChildAndUnLock(execInfo.hiddenRoot, "BLEADV", jsvNewEmptyArray());
  jsvObjectSetChild(execInfo.hiddenRoot, "BLEADVCB", callback);
  JsVar *fn;
  fn = jsvNewNativeFunction((void (*)(void))jswrap_ble_findDevices_found_cb, JSWAT_VOID|(JSWAT_JSVAR<<JSWAT_BITS));
  if (fn) {
    jswrap_ble_setScan(fn, options);
    jsvUnLock(fn);
  }
  jsvUnLock(jsiSetTimeout(jswrap_ble_findDevices_timeout_cb, time));
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "setRSSIHandler",
    "generate" : "jswrap_ble_setRSSIHandler",
    "params" : [
      ["callback","JsVar","The callback to call with the RSSI value, or undefined to stop"]
    ]
}

Start/stop listening for RSSI values on the currently active connection
(where This device is a peripheral and is being connected to by a 'central' device)

```
// Start scanning
NRF.setRSSIHandler(function(rssi) {
  console.log(rssi); // prints -85 (or similar)
});
// Stop Scanning
NRF.setRSSIHandler();
```

RSSI is the 'Received Signal Strength Indication' in dBm
*/
void jswrap_ble_setRSSIHandler(JsVar *callback) {
  // set the callback event variable
  if (!jsvIsFunction(callback)) callback=0;
  jsvObjectSetChild(execInfo.root, BLE_RSSI_EVENT, callback);
  // either start or stop scanning
  uint32_t err_code = jsble_set_rssi_scan(callback != 0);
  jsble_check_error(err_code);
}




/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "setTxPower",
    "generate" : "jswrap_ble_setTxPower",
    "params" : [
      ["power","int","Transmit power. Accepted values are -40(nRF52 only), -30(nRF51 only), -20, -16, -12, -8, -4, 0, and 4 dBm. Others will give an error code."]
    ]
}
Set the BLE radio transmit power. The default TX power is 0 dBm.
*/
void jswrap_ble_setTxPower(JsVarInt pwr) {
  uint32_t              err_code;
#ifdef NRF5X
#if NRF_SD_BLE_API_VERSION > 5
  // TODO: what about BLE_GAP_TX_POWER_ROLE_ADV and BLE_GAP_TX_POWER_ROLE_CONN
  err_code = sd_ble_gap_tx_power_set(BLE_GAP_TX_POWER_ROLE_SCAN_INIT, 0/*ignored*/, pwr);
#else
  err_code = sd_ble_gap_tx_power_set(pwr);
#endif
#else
  err_code = 0xDEAD;
  jsiConsolePrintf("FIXME\n");
#endif
  jsble_check_error(err_code);
}


/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "setLowPowerConnection",
    "generate" : "jswrap_ble_setLowPowerConnection",
    "params" : [
      ["lowPower","bool","Whether the connection is low power or not"]
    ]
}

**THIS IS DEPRECATED** - please use `NRF.setConnectionInterval` for
peripheral and `NRF.connect(addr, options)`/`BluetoothRemoteGATTServer.connect(options)`
for central connections.

This sets the connection parameters - these affect the transfer speed and
power usage when the device is connected.

* When not low power, the connection interval is between 7.5 and 20ms
* When low power, the connection interval is between 500 and 1000ms

When low power connection is enabled, transfers of data over Bluetooth
will be very slow, however power usage while connected will be drastically
decreased.

This will only take effect after the connection is disconnected and
re-established.
*/
void jswrap_ble_setLowPowerConnection(bool lowPower) {
  BLEFlags oldflags = jsvGetIntegerAndUnLock(jsvObjectGetChild(execInfo.hiddenRoot, BLE_NAME_FLAGS, 0));
  BLEFlags flags = oldflags;
  if (lowPower)
    flags |= BLE_FLAGS_LOW_POWER;
  else
    flags &= ~BLE_FLAGS_LOW_POWER;
  if (flags != oldflags) {
    jsvObjectSetChildAndUnLock(execInfo.hiddenRoot, BLE_NAME_FLAGS, jsvNewFromInteger(flags));
    jswrap_ble_restart();
  }
}


/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "nfcURL",
    "ifdef" : "NRF52",
    "generate" : "jswrap_nfc_URL",
    "params" : [
      ["url","JsVar","The URL string to expose on NFC, or `undefined` to disable NFC"]
    ]
}
Enables NFC and starts advertising the given URL. For example:

```
NRF.nfcURL("http://espruino.com");
```
*/
void jswrap_nfc_URL(JsVar *url) {
#ifdef USE_NFC
  // Check for disabling NFC
  if (jsvIsUndefined(url)) {
    jsvObjectRemoveChild(execInfo.hiddenRoot, "NfcData");
    jswrap_nfc_stop();
    return;
  }

  if (!jsvIsString(url)) {
    jsExceptionHere(JSET_TYPEERROR, "Expecting a String, got %t", url);
    return;
  }

  JSV_GET_AS_CHAR_ARRAY(urlPtr, urlLen, url);
  if (!urlPtr || !urlLen)
    return jsExceptionHere(JSET_ERROR, "Unable to get URL data");

  nfc_uri_id_t uriType = NFC_URI_NONE;
  if (memcmp(urlPtr, "http://", 7)==0) {
    urlPtr+=7;
    urlLen-=7;
    uriType = NFC_URI_HTTP;
  } else if (memcmp(urlPtr, "https://", 8)==0) {
    urlPtr+=8;
    urlLen-=8;
    uriType = NFC_URI_HTTPS;
  }

  /* Encode NDEF message into a flat string - we need this to store the
   * data so it hangs around. Avoid having a static var so we have RAM
   * available if not using NFC. NFC data is read by nfc_callback */
  JsVar *flatStr = jsvNewFlatStringOfLength(NDEF_FULL_URL_HEADER_LEN + urlLen + NDEF_TERM_TLV_LEN);
  if (!flatStr)
    return jsExceptionHere(JSET_ERROR, "Unable to create string with URI data in");
  jsvObjectSetChild(execInfo.hiddenRoot, "NfcData", flatStr);
  uint8_t *flatStrPtr = (uint8_t*)jsvGetFlatStringPointer(flatStr);
  jsvUnLock(flatStr);

  /* assemble NDEF Message */
  memcpy(flatStrPtr, NDEF_HEADER, NDEF_FULL_URL_HEADER_LEN); /* fill header */
  flatStrPtr[NDEF_IC_OFFSET] = uriType; /* set URI Identifier Code */
  memcpy(flatStrPtr+NDEF_FULL_URL_HEADER_LEN, urlPtr, urlLen); /* add payload */

  /* inject length fields into header */
  flatStrPtr[NDEF_MSG_LEN_OFFSET] = NDEF_RECORD_HEADER_LEN + urlLen;
  flatStrPtr[NDEF_PL_LEN_LSB_OFFSET] = NDEF_IC_LEN + urlLen;

  /* write terminator TLV block */
  flatStrPtr[NDEF_FULL_URL_HEADER_LEN + urlLen] = NDEF_TERM_TLV;

  /* start nfc peripheral */
  JsVar* uid = jswrap_nfc_start(NULL);

  /* inject UID/BCC */
  size_t len;
  char *uidPtr = jsvGetDataPointer(uid, &len);
  if(uidPtr) memcpy(flatStrPtr, uidPtr, TAG_HEADER_LEN);
  jsvUnLock(uid);
#endif
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "nfcPair",
    "ifdef" : "NRF52",
    "generate" : "jswrap_nfc_pair",
    "params" : [
      ["key","JsVar","16 byte out of band key"]
    ]
}
Enables NFC and with an out of band 16 byte pairing key.

For example the following will enable out of band pairing on BLE
such that the device will pair when you tap the phone against it:

```
var bleKey = [0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00];
NRF.on('security',s=>print("security",JSON.stringify(s)));
NRF.nfcPair(bleKey);
NRF.setSecurity({oob:bleKey, mitm:true});
```
*/
void jswrap_nfc_pair(JsVar *key) {
#ifdef USE_NFC
  // Check for disabling NFC
  if (jsvIsUndefined(key)) {
    jsvObjectRemoveChild(execInfo.hiddenRoot, "NfcData");
    jswrap_nfc_stop();
    return;
  }

  JSV_GET_AS_CHAR_ARRAY(keyPtr, keyLen, key);
  if (!keyPtr || keyLen!=BLE_GAP_SEC_KEY_LEN)
    return jsExceptionHere(JSET_ERROR, "Unable to get key data or key isn't 16 bytes long");

  /* assemble NDEF Message */
  /* Encode BLE pairing message into the buffer. */
  uint8_t buf[256];
  uint32_t ndef_msg_len = sizeof(buf);
  uint32_t err_code = nfc_ble_pair_default_msg_encode(NFC_BLE_PAIR_MSG_FULL,
                                             (ble_advdata_tk_value_t *)keyPtr,
                                             NULL,
                                             buf,
                                             &ndef_msg_len);
  if (jsble_check_error(err_code)) return;

  /* Encode NDEF message into a flat string - we need this to store the
   * data so it hangs around. Avoid having a static var so we have RAM
   * available if not using NFC. NFC data is read by nfc_callback */

  JsVar *flatStr = jsvNewFlatStringOfLength(ndef_msg_len);
  if (!flatStr)
    return jsExceptionHere(JSET_ERROR, "Unable to create string with pairing data in");
  uint8_t *flatStrPtr = (uint8_t*)jsvGetFlatStringPointer(flatStr);
  memcpy(flatStrPtr, buf, ndef_msg_len);

  jswrap_nfc_raw(flatStr);
  jsvUnLock(flatStr);
#endif
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "nfcAndroidApp",
    "ifdef" : "NRF52",
    "generate" : "jswrap_nfc_androidApp",
    "params" : [
      ["app","JsVar","The unique identifier of the given Android App"]
    ]
}
Enables NFC with a record that will launch the given android app.

For example:

```
NRF.nfcAndroidApp("no.nordicsemi.android.nrftoolbox")
```
*/
void jswrap_nfc_androidApp(JsVar *appName) {
#ifdef USE_NFC
  // Check for disabling NFC
  if (jsvIsUndefined(appName)) {
    jsvObjectRemoveChild(execInfo.hiddenRoot, "NfcData");
    jswrap_nfc_stop();
    return;
  }

  JSV_GET_AS_CHAR_ARRAY(appNamePtr, appNameLen, appName);
  if (!appNamePtr || !appNameLen)
    return jsExceptionHere(JSET_ERROR, "Unable to get app name");

  /* assemble NDEF Message */
  /* Encode BLE pairing message into the buffer. */
  uint8_t buf[512];
  uint32_t ndef_msg_len = sizeof(buf);
  /* Encode launchapp message into the buffer. */
  uint32_t err_code = nfc_launchapp_msg_encode((uint8_t*)appNamePtr,
                                      appNameLen,
                                      0,
                                      0,
                                      buf,
                                      &ndef_msg_len);
  if (jsble_check_error(err_code)) return;

  /* Encode NDEF message into a flat string - we need this to store the
   * data so it hangs around. Avoid having a static var so we have RAM
   * available if not using NFC. NFC data is read by nfc_callback */

  JsVar *flatStr = jsvNewFlatStringOfLength(ndef_msg_len);
  if (!flatStr)
    return jsExceptionHere(JSET_ERROR, "Unable to create string with pairing data in");
  uint8_t *flatStrPtr = (uint8_t*)jsvGetFlatStringPointer(flatStr);
  memcpy(flatStrPtr, buf, ndef_msg_len);

  jswrap_nfc_raw(flatStr);
  jsvUnLock(flatStr);
#endif
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "nfcRaw",
    "ifdef" : "NRF52",
    "generate" : "jswrap_nfc_raw",
    "params" : [
      ["payload","JsVar","The NFC NDEF message to deliver to the reader"]
    ]
}
Enables NFC and starts advertising with Raw data. For example:

```
NRF.nfcRaw(new Uint8Array([193, 1, 0, 0, 0, 13, 85, 3, 101, 115, 112, 114, 117, 105, 110, 111, 46, 99, 111, 109]));
// same as NRF.nfcURL("http://espruino.com");
```
*/
void jswrap_nfc_raw(JsVar *payload) {
#ifdef USE_NFC
  // Check for disabling NFC
  if (jsvIsUndefined(payload)) {
    jsvObjectRemoveChild(execInfo.hiddenRoot, "NfcData");
    jswrap_nfc_stop();
    return;
  }

  JSV_GET_AS_CHAR_ARRAY(dataPtr, dataLen, payload);
  if (!dataPtr || !dataLen)
    return jsExceptionHere(JSET_ERROR, "Unable to get NFC data");

  /* Create a flat string - we need this to store the NFC data so it hangs around.
   * Avoid having a static var so we have RAM available if not using NFC.
   * NFC data is read by nfc_callback in bluetooth.c */
  JsVar *flatStr = jsvNewFlatStringOfLength(NDEF_FULL_RAW_HEADER_LEN + dataLen + NDEF_TERM_TLV_LEN);
  if (!flatStr)
    return jsExceptionHere(JSET_ERROR, "Unable to create string with NFC data in");
  jsvObjectSetChild(execInfo.hiddenRoot, "NfcData", flatStr);
  uint8_t *flatStrPtr = (uint8_t*)jsvGetFlatStringPointer(flatStr);
  jsvUnLock(flatStr);

  /* assemble NDEF Message */
  memcpy(flatStrPtr, NDEF_HEADER, NDEF_FULL_RAW_HEADER_LEN); /* fill header */
  memcpy(flatStrPtr+NDEF_FULL_RAW_HEADER_LEN, dataPtr, dataLen); /* add payload */

  /* inject length fields into header */
  flatStrPtr[NDEF_MSG_LEN_OFFSET] = dataLen;

  /* write terminator TLV block */
  flatStrPtr[NDEF_FULL_RAW_HEADER_LEN + dataLen] = NDEF_TERM_TLV;

  /* start nfc peripheral */
  JsVar* uid = jswrap_nfc_start(NULL);

  /* inject UID/BCC */
  size_t len;
  char *uidPtr = jsvGetDataPointer(uid, &len);
  if(uidPtr) memcpy(flatStrPtr, uidPtr, TAG_HEADER_LEN);
  jsvUnLock(uid);
#endif
}


/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "nfcStart",
    "ifdef" : "NRF52",
    "generate" : "jswrap_nfc_start",
    "params" : [
      ["payload","JsVar","Optional 7 byte UID"]
    ],
    "return" : ["JsVar", "Internal tag memory (first 10 bytes of tag data)" ]
}
**Advanced NFC Functionality.** If you just want to advertise a URL, use `NRF.nfcURL` instead.

Enables NFC and starts advertising. `NFCrx` events will be
fired when data is received.

```
NRF.nfcStart();
```
*/
JsVar *jswrap_nfc_start(JsVar *payload) {
#ifdef USE_NFC
  /* Turn off NFC */
  jsble_nfc_stop();

  /* Create a flat string - we need this to store the NFC data so it hangs around.
   * Avoid having a static var so we have RAM available if not using NFC */
  JsVar *flatStr = 0;
  if (!jsvIsUndefined(payload)) {
    /* Custom UID */
    JSV_GET_AS_CHAR_ARRAY(dataPtr, dataLen, payload);
    if (!dataPtr || !dataLen) {
      jsExceptionHere(JSET_ERROR, "Unable to get NFC data");
      return 0;
    }
    flatStr = jsvNewFlatStringOfLength(dataLen);
    if (!flatStr) {
      jsExceptionHere(JSET_ERROR, "Unable to create string with NFC data in");
      return 0;
    }
    jsvObjectSetChild(execInfo.hiddenRoot, "NfcEnabled", flatStr);
    jsvUnLock(flatStr);
    uint8_t *flatStrPtr = (uint8_t*)jsvGetFlatStringPointer(flatStr);
    memcpy(flatStrPtr, dataPtr, dataLen);
  } else {
    /* Default UID */
    flatStr = jsvNewFlatStringOfLength(0);
    if (!flatStr) {
      jsExceptionHere(JSET_ERROR, "Unable to create string with NFC data in");
      return 0;
    }
    jsvObjectSetChild(execInfo.hiddenRoot, "NfcEnabled", flatStr);
    jsvUnLock(flatStr);
  }

  /* start nfc */
  uint8_t *flatStrPtr = (uint8_t*)jsvGetFlatStringPointer(flatStr);
  jsble_nfc_start(flatStrPtr, jsvGetLength(flatStr));

  /* return internal tag header */
  char *ptr = 0; size_t size = TAG_HEADER_LEN;
  JsVar *arr = jsvNewArrayBufferWithPtr(size, &ptr);
  if (ptr) jsble_nfc_get_internal((uint8_t *)ptr, &size);
  return arr;
#else
  return 0;
#endif
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "nfcStop",
    "ifdef" : "NRF52",
    "generate" : "jswrap_nfc_stop",
    "params" : [ ]
}
**Advanced NFC Functionality.** If you just want to advertise a URL, use `NRF.nfcURL` instead.

Disables NFC.

```
NRF.nfcStop();
```
*/
void jswrap_nfc_stop() {
#ifdef USE_NFC
  jsvObjectRemoveChild(execInfo.hiddenRoot, "NfcEnabled");
  jsble_nfc_stop();
#endif
}


/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "nfcSend",
    "ifdef" : "NRF52",
    "generate" : "jswrap_nfc_send",
    "params" : [
      ["payload","JsVar","Optional tx data"]
    ]
}
**Advanced NFC Functionality.** If you just want to advertise a URL, use `NRF.nfcURL` instead.

Acknowledges the last frame and optionally transmits a response.
If payload is an array, then a array.length byte nfc frame is sent.
If payload is a int, then a 4bit ACK/NACK is sent.
**Note:** ```nfcSend``` should always be called after an ```NFCrx``` event.

```
NRF.nfcSend(new Uint8Array([0x01, 0x02, ...]));
// or
NRF.nfcSend(0x0A);
// or
NRF.nfcSend();
```
*/
void jswrap_nfc_send(JsVar *payload) {
#ifdef USE_NFC
  /* Switch to RX */
  if (jsvIsUndefined(payload))
    return jsble_nfc_send_rsp(0, 0);

  /* Send 4 bit ACK/NACK */
  if (jsvIsInt(payload))
    return jsble_nfc_send_rsp(jsvGetInteger(payload), 4);

  /* Send n byte payload */
  JSV_GET_AS_CHAR_ARRAY(dataPtr, dataLen, payload);
  if (!dataPtr || !dataLen)
    return jsExceptionHere(JSET_ERROR, "Unable to get NFC data");

  jsble_nfc_send((uint8_t*)dataPtr, dataLen);
#endif
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "sendHIDReport",
    "ifdef" : "NRF52",
    "generate" : "jswrap_ble_sendHIDReport",
    "params" : [
      ["data","JsVar","Input report data as an array"],
      ["callback","JsVar","A callback function to be called when the data is sent"]
    ]
}
Send a USB HID report. HID must first be enabled with `NRF.setServices({}, {hid: hid_report})`
*/
void jswrap_ble_sendHIDReport(JsVar *data, JsVar *callback) {
#if BLE_HIDS_ENABLED
  JSV_GET_AS_CHAR_ARRAY(vPtr, vLen, data)
  if (vPtr && vLen) {
    if (jsvIsFunction(callback))
      jsvObjectSetChild(execInfo.root, BLE_HID_SENT_EVENT, callback);
    jsble_send_hid_input_report((uint8_t*)vPtr, vLen);
  } else {
    jsExceptionHere(JSET_ERROR, "Expecting array, got %t", data);
  }
#endif
}


/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "requestDevice",
    "#if" : "defined(NRF52) || defined(ESP32)",
    "generate" : "jswrap_ble_requestDevice",
    "params" : [
      ["options","JsVar","Options used to filter the device to use"]
    ],
    "return" : ["JsVar", "A `Promise` that is resolved (or rejected) when the connection is complete" ],
    "return_object" : "Promise"
}
Search for available devices matching the given filters. Since we have no UI here,
Espruino will pick the FIRST device it finds, or it'll call `catch`.

`options` can have the following fields:

* `filters` - a list of filters that a device must match before it is returned (see below)
* `timeout` - the maximum time to scan for in milliseconds (scanning stops when a match
is found. eg. `NRF.requestDevice({ timeout:2000, filters: [ ... ] })`
* `active` - whether to perform active scanning (requesting 'scan response' packets from any
devices that are found). eg. `NRF.requestDevice({ active:true, filters: [ ... ] })`

**NOTE:** `timeout` and `active` are not part of the Web Bluetooth standard.

The following filter types are implemented:

* `services` - list of services as strings (all of which must match). 128 bit services must be in the form '01230123-0123-0123-0123-012301230123'
* `name` - exact device name
* `namePrefix` - starting characters of device name
* `id` - exact device address (`id:"e9:53:86:09:89:99 random"`) (this is Espruino-specific, and is not part of the Web Bluetooth spec)
* `serviceData` - an object containing service characteristics which must all match (`serviceData:{"1809":{}}`). Matching of actual service data is not supported yet.
* `manufacturerData` - an object containing manufacturer UUIDs which must all match (`manufacturerData:{0x0590:{}}`). Matching of actual manufacturer data is not supported yet.

```
NRF.requestDevice({ filters: [{ namePrefix: 'Puck.js' }] }).then(function(device) { ... });
// or
NRF.requestDevice({ filters: [{ services: ['1823'] }] }).then(function(device) { ... });
// or
NRF.requestDevice({ filters: [{ manufacturerData:{0x0590:{}} }] }).then(function(device) { ... });
```

As a full example, to send data to another Puck.js to turn an LED on:

```
var gatt;
NRF.requestDevice({ filters: [{ namePrefix: 'Puck.js' }] }).then(function(device) {
  return device.gatt.connect();
}).then(function(g) {
  gatt = g;
  return gatt.getPrimaryService("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
}).then(function(service) {
  return service.getCharacteristic("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
}).then(function(characteristic) {
  return characteristic.writeValue("LED1.set()\n");
}).then(function() {
  gatt.disconnect();
  console.log("Done!");
});
```

Or slightly more concisely, using ES6 arrow functions:

```
var gatt;
NRF.requestDevice({ filters: [{ namePrefix: 'Puck.js' }]}).then(
  device => device.gatt.connect()).then(
  g => (gatt=g).getPrimaryService("6e400001-b5a3-f393-e0a9-e50e24dcca9e")).then(
  service => service.getCharacteristic("6e400002-b5a3-f393-e0a9-e50e24dcca9e")).then(
  characteristic => characteristic.writeValue("LED1.reset()\n")).then(
  () => { gatt.disconnect(); console.log("Done!"); } );
```

Note that you have to keep track of the `gatt` variable so that you can
disconnect the Bluetooth connection when you're done.
*/
#if CENTRAL_LINK_COUNT>0
/// Called when we timeout waiting for a device
void jswrap_ble_requestDevice_finish() {
  if (!bleInTask(BLETASK_REQUEST_DEVICE))
    return;
  jswrap_ble_setScan(0,0);  // stop scanning
  bleCompleteTaskFailAndUnLock(BLETASK_REQUEST_DEVICE, jsvNewFromString("No device found matching filters"));
}

/// Called when a device is found
void jswrap_ble_requestDevice_scan(JsVar *device) {
  if (!bleInTask(BLETASK_REQUEST_DEVICE))
    return;
  // We know the device matches because setScan would have checked for us
  jswrap_ble_setScan(0,0); // stop scanning
  JsVar *argArr = jsvNewArray(&bleTaskInfo, 1);
  jswrap_interface_clearTimeout(argArr /*the timeout*/); // cancel the timeout
  jsvUnLock(argArr);
  bleCompleteTaskSuccess(BLETASK_REQUEST_DEVICE, device);
}
#endif

JsVar *jswrap_ble_requestDevice(JsVar *options) {
#if CENTRAL_LINK_COUNT>0
  if (!(jsvIsUndefined(options) || jsvIsObject(options))) {
    jsExceptionHere(JSET_TYPEERROR, "Expecting an object, for %t", options);
    return 0;
  }
  JsVar *filters = jsvObjectGetChild(options, "filters", 0);
  if (!jsvIsArray(filters)) {
    jsvUnLock(filters);
    jsExceptionHere(JSET_TYPEERROR, "requestDevice expecting an array of filters, got %t", filters);
    return 0;
  }
  jsvUnLock(filters);

  JsVarFloat timeout = jsvGetFloatAndUnLock(jsvObjectGetChild(options, "timeout", 0));
  if (isnan(timeout) || timeout<=0) timeout = 2000;

  JsVar *promise = 0;

  // Set a timeout for when we finish if we didn't find anything
  JsVar *timeoutIndex = jsiSetTimeout(jswrap_ble_requestDevice_finish, timeout);
  // Now create a promise, and pass in the timeout index so we can cancel the timeout if we find something
  if (bleNewTask(BLETASK_REQUEST_DEVICE, timeoutIndex)) {
    // Start scanning
    JsVar *fn = jsvNewNativeFunction((void (*)(void))jswrap_ble_requestDevice_scan, (JSWAT_JSVAR<<JSWAT_BITS));
    if (fn) {
      jswrap_ble_setScan(fn, options);
      jsvUnLock(fn);
    }
    promise = jsvLockAgainSafe(blePromise);
  }
  jsvUnLock(timeoutIndex);
  return promise;
#else
  jsExceptionHere(JSET_ERROR, "Unimplemented");
  return 0;
#endif
}


/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "connect",
    "#if" : "defined(NRF52) || defined(ESP32)",
    "generate" : "jswrap_ble_connect",
    "params" : [
      ["mac","JsVar","The MAC address to connect to"],
      ["options","JsVar","(Espruino-specific) An object of connection options (see `BluetoothRemoteGATTServer.connect` for full details)"]
    ],
    "return" : ["JsVar", "A `Promise` that is resolved (or rejected) when the connection is complete" ],
    "return_object" : "Promise"
}
Connect to a BLE device by MAC address. Returns a promise,
the argument of which is the `BluetoothRemoteGATTServer` connection.

```
NRF.connect("aa:bb:cc:dd:ee").then(function(server) {
  // ...
});
```

This has the same effect as calling `BluetoothDevice.gatt.connect` on a `BluetoothDevice` requested
using `NRF.requestDevice`. It just allows you to specify the address directly (without having to scan).

You can use it as follows - this would connect to another Puck device and turn its LED on:

```
var gatt;
NRF.connect("aa:bb:cc:dd:ee random").then(function(g) {
  gatt = g;
  return gatt.getPrimaryService("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
}).then(function(service) {
  return service.getCharacteristic("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
}).then(function(characteristic) {
  return characteristic.writeValue("LED1.set()\n");
}).then(function() {
  gatt.disconnect();
  console.log("Done!");
});
```

**Note:** Espruino Bluetooth devices use a type of BLE address known as 'random static',
which is different to a 'public' address. To connect to an Espruino device you'll need 
to use an address string of the form `"aa:bb:cc:dd:ee random"` rather than just 
`"aa:bb:cc:dd:ee"`. If you scan for devices with `NRF.findDevices`/`NRF.setScan` then
addresses are already reported in the correct format.
*/
JsVar *jswrap_ble_connect(JsVar *mac, JsVar *options) {
#if CENTRAL_LINK_COUNT>0
  JsVar *device = jspNewObject(0, "BluetoothDevice");
  if (!device) return 0;
  jsvObjectSetChild(device, "id", mac);
  JsVar *gatt = jswrap_BluetoothDevice_gatt(device);
  jsvUnLock(device);
  if (!gatt) return 0;
  JsVar *promise = jswrap_ble_BluetoothRemoteGATTServer_connect(gatt, options);
  jsvUnLock(gatt);
  return promise;
#else
  jsExceptionHere(JSET_ERROR, "Unimplemented");
  return 0;
#endif
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "setWhitelist",
    "ifdef" : "NRF52",
    "generate" : "jswrap_ble_setWhitelist",
    "params" : [
      ["whitelisting","bool","Are we using a whitelist? (default false)"]
    ]
}
If set to true, whenever a device bonds it will be added to the
whitelist.

When set to false, the whitelist is cleared and newly bonded
devices will not be added to the whitelist.

**Note:** This is remembered between `reset()`s but isn't
remembered after power-on (you'll have to add it to `onInit()`.
*/
void jswrap_ble_setWhitelist(bool whitelist) {
#if PEER_MANAGER_ENABLED
  jsble_central_setWhitelist(whitelist);
#endif
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "setConnectionInterval",
    "ifdef" : "NRF52",
    "generate" : "jswrap_ble_setConnectionInterval",
    "params" : [
      ["interval","JsVar","The connection interval to use (see below)"]
    ]
}
When connected, Bluetooth LE devices communicate at a set interval.
Lowering the interval (eg. more packets/second) means a lower delay when
sending data, higher bandwidth, but also more power consumption.

By default, when connected as a peripheral Espruino automatically adjusts the
connection interval. When connected it's as fast as possible (7.5ms) but when idle
for over a minute it drops to 200ms. On continued activity (>1 BLE operation) the
interval is raised to 7.5ms again.

The options for `interval` are:

* `undefined` / `"auto"` : (default) automatically adjust connection interval
* `100` : set min and max connection interval to the same number (between 7.5ms and 4000ms)
* `{minInterval:20, maxInterval:100}` : set min and max connection interval as a range

This configuration is not remembered during a `save()` - you will have to
re-set it via `onInit`.

**Note:** If connecting to another device (as Central), you can use
an extra argument to `NRF.connect` or `BluetoothRemoteGATTServer.connect`
to specify a connection interval.

**Note:** This overwrites any changes imposed by the deprecated `NRF.setLowPowerConnection`
*/
void jswrap_ble_setConnectionInterval(JsVar *interval) {
#if NRF52
  if (jsvIsUndefined(interval) || jsvIsStringEqual(interval,"auto")) {
    // allow automatic interval setting
    bleStatus &= ~BLE_DISABLE_DYNAMIC_INTERVAL;
  } else if (jsvIsNumeric(interval)) {
    // disable auto interval
    bleStatus |= BLE_DISABLE_DYNAMIC_INTERVAL;
    JsVarFloat f = jsvGetFloat(interval);
    jsble_check_error(jsble_set_periph_connection_interval(f,f));
  } else if (jsvIsObject(interval)) {
    // disable auto interval
    bleStatus |= BLE_DISABLE_DYNAMIC_INTERVAL;
    JsVarFloat min = jsvGetFloatAndUnLock(jsvObjectGetChild(interval,"minInterval",0));
    JsVarFloat max = jsvGetFloatAndUnLock(jsvObjectGetChild(interval,"maxInterval",0));
    jsble_check_error(jsble_set_periph_connection_interval(min, max));
  }
#endif
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "setSecurity",
    "ifdef" : "NRF52",
    "generate" : "jswrap_ble_setSecurity",
    "params" : [
      ["options","JsVar","An object containing security-related options (see below)"]
    ]
}
Sets the security options used when connecting/pairing. This applies to both central
*and* peripheral mode.

```
NRF.setSecurity({
  display : bool  // default false, can this device display a passkey
                  // - sent via the `BluetoothDevice.passkey` event
  keyboard : bool // default false, can this device enter a passkey
                  // - request sent via the `BluetoothDevice.passkeyRequest` event
  bond : bool // default true, Perform bonding
  mitm : bool // default false, Man In The Middle protection
  lesc : bool // default false, LE Secure Connections
  passkey : // default "", or a 6 digit passkey to use
  oob : [0..15] // if specified, Out Of Band pairing is enabled and
                // the 16 byte pairing code supplied here is used
});
```

**NOTE:** Some combinations of arguments will cause an error. For example
supplying a passkey without `display:1` is not allowed. If `display:1` is set
you do not require a physical display, the user just needs to know
the passkey you supplied.

For instance, to require pairing and to specify a passkey, use:

```
NRF.setSecurity({passkey:"123456", mitm:1, display:1});
```

However, while most devices will request a passkey for pairing at
this point it is still possible for a device to connect without
requiring one (eg. using the 'NRF Connect' app).

To force a passkey you need to protect each characteristic
you define with `NRF.setSecurity`. For instance the following
code will *require* that the passkey `123456` is entered
before the characteristic `9d020002-bf5f-1d1a-b52a-fe52091d5b12`
can be read.

```
NRF.setSecurity({passkey:"123456", mitm:1, display:1});
NRF.setServices({
  "9d020001-bf5f-1d1a-b52a-fe52091d5b12" : {
    "9d020002-bf5f-1d1a-b52a-fe52091d5b12" : {
      // readable always
      value : "Not Secret"
    },
    "9d020003-bf5f-1d1a-b52a-fe52091d5b12" : {
      // readable only once bonded
      value : "Secret",
      readable : true,
      security: {
        read: {
          mitm: true,
          encrypted: true
        }
      }
    },
    "9d020004-bf5f-1d1a-b52a-fe52091d5b12" : {
      // readable always
      // writable only once bonded
      value : "Readable",
      readable : true,
      writable : true,
      onWrite : function(evt) {
        console.log("Wrote ", evt.data);
      },
      security: {
        write: {
          mitm: true,
          encrypted: true
        }
      }
    }
  }
});
```
*/
void jswrap_ble_setSecurity(JsVar *options) {
  if (!jsvIsObject(options) && !jsvIsUndefined(options))
    jsExceptionHere(JSET_TYPEERROR, "Expecting an object or undefined, got %t", options);
  else {
    jsvObjectSetOrRemoveChild(execInfo.hiddenRoot, BLE_NAME_SECURITY, options);
    jsble_update_security();
  }
}

/*JSON{
    "type" : "staticmethod",
    "class" : "NRF",
    "name" : "getSecurityStatus",
    "ifdef" : "NRF52",
    "generate" : "jswrap_ble_getSecurityStatus",
    "return" : ["JsVar", "An object" ]
}
Return an object with information about the security
state of the current peripheral connection:

```
{
  connected       // The connection is active (not disconnected).
  encrypted       // Communication on this link is encrypted.
  mitm_protected  // The encrypted communication is also protected against man-in-the-middle attacks.
  bonded          // The peer is bonded with us
}
```

If there is no active connection, `{connected:false}` will be returned.

See `NRF.setSecurity` for information about negotiating a secure connection.
*/
JsVar *jswrap_ble_getSecurityStatus(JsVar *parent) {
  return jsble_get_security_status(m_peripheral_conn_handle);
}

/*JSON{
  "type" : "class",
  "class" : "BluetoothDevice",
  "ifdef" : "NRF52"
}
A Web Bluetooth-style device - you can request one using `NRF.requestDevice(address)`

For example:

```
var gatt;
NRF.requestDevice({ filters: [{ name: 'Puck.js abcd' }] }).then(function(device) {
  console.log("found device");
  return device.gatt.connect();
}).then(function(g) {
  gatt = g;
  console.log("connected");
  return gatt.startBonding();
}).then(function() {
  console.log("bonded", gatt.getSecurityStatus());
  gatt.disconnect();
}).catch(function(e) {
  console.log("ERROR",e);
});
```
*/
/*JSON{
    "type" : "property",
    "class" : "BluetoothDevice",
    "name" : "gatt",
    "#if" : "defined(NRF52) || defined(ESP32)",
    "generate" : "jswrap_BluetoothDevice_gatt",
    "return" : ["JsVar", "A `BluetoothRemoteGATTServer` for this device" ]
}
*/
JsVar *jswrap_BluetoothDevice_gatt(JsVar *parent) {
#if CENTRAL_LINK_COUNT>0
  JsVar *gatt = jsvObjectGetChild(parent, "gatt", 0);
  if (gatt) return gatt;

  gatt = jspNewObject(0, "BluetoothRemoteGATTServer");
  jsvObjectSetChild(parent, "gatt", gatt);
  jsvObjectSetChild(gatt, "device", parent);
  jsvObjectSetChildAndUnLock(gatt, "connected", jsvNewFromBool(false));
  return gatt;
#else
  jsExceptionHere(JSET_ERROR, "Unimplemented");
  return 0;
#endif
}
/*JSON{
    "type" : "property",
    "class" : "BluetoothDevice",
    "name" : "rssi",
    "ifdef" : "NRF52",
    "generate" : false,
    "return" : ["bool", "The last received RSSI (signal strength) for this device" ]
}
*//*Documentation only*/
/*JSON{
    "type" : "event",
    "class" : "BluetoothDevice",
    "name" : "passkey",
    "ifdef" : "NRF52",
    "params" : [
      ["passkey","JsVar","A 6 character numeric String to be displayed"]
    ]
}
Called when the device pairs and sends a passkey that Espruino should display.

For this to be used, you'll have to specify that there's a display using `NRF.setSecurity`

**This is not part of the Web Bluetooth Specification.** It has been added
specifically for Espruino.
*/
/*JSON{
    "type" : "event",
    "class" : "BluetoothDevice",
    "name" : "passkeyRequest",
    "ifdef" : "NRF52"
}
Called when the device pairs, displays a passkey, and wants Espruino to tell it what the passkey was.

Respond with `BluetoothDevice.sendPasskey()` with a 6 character string containing only `0..9`.

For this to be used, you'll have to specify that there's a keyboard using `NRF.setSecurity`

**This is not part of the Web Bluetooth Specification.** It has been added
specifically for Espruino.
*/
/*JSON{
    "type" : "method",
    "class" : "BluetoothDevice",
    "name" : "sendPasskey",
    "ifdef" : "NRF52",
    "generate" : "jswrap_ble_BluetoothDevice_sendPasskey",
    "params" : [
      ["passkey","JsVar","A 6 character numeric String to be returned to the device"]
    ]
}
To be used as a response when the event `BluetoothDevice.sendPasskey` has been received.

**This is not part of the Web Bluetooth Specification.** It has been added
specifically for Espruino.
*/
#if NRF52
void jswrap_ble_BluetoothDevice_sendPasskey(JsVar *parent, JsVar *passkeyVar) {
  char passkey[BLE_GAP_PASSKEY_LEN+1];
  memset(passkey, 0, sizeof(passkey));
  jsvGetString(passkeyVar, passkey, sizeof(passkey));
  uint32_t err_code = jsble_central_send_passkey(passkey);
  jsble_check_error(err_code);
}
#endif

/*JSON{
    "type" : "method",
    "class" : "BluetoothRemoteGATTServer",
    "name" : "connect",
    "#if" : "defined(NRF52) || defined(ESP32)",
    "generate" : "jswrap_ble_BluetoothRemoteGATTServer_connect",
    "params" : [
      ["options","JsVar","(Espruino-specific) An object of connection options (see below)"]
    ],
    "return" : ["JsVar", "A `Promise` that is resolved (or rejected) when the connection is complete" ],
    "return_object" : "Promise"
}
Connect to a BLE device - returns a promise,
the argument of which is the `BluetoothRemoteGATTServer` connection.

See [`NRF.requestDevice`](/Reference#l_NRF_requestDevice) for usage examples.

`options` is an optional object containing:

```
{
   minInterval // min connection interval in milliseconds, 7.5 ms to 4 s
   maxInterval // max connection interval in milliseconds, 7.5 ms to 4 s
}
```

By default the interval is 20-200ms (or 500-1000ms if `NRF.setLowPowerConnection(true)` was called.
During connection Espruino negotiates with the other device to find a common interval that can be
used.

For instance calling:

```
NRF.requestDevice({ filters: [{ namePrefix: 'Pixl.js' }] }).then(function(device) {
  return device.gatt.connect({minInterval:7.5, maxInterval:7.5});
}).then(function(g) {
```

will force the connection to use the fastest connection interval possible (as long as the device
at the other end supports it).
*/
#if CENTRAL_LINK_COUNT>0
static void _jswrap_ble_central_connect(JsVar *addr, JsVar *options) {
  // this function gets called on idle - just to make it less
  // likely we get connected while in the middle of executing stuff
  ble_gap_addr_t peer_addr;
  // this should be ok since we checked in jswrap_ble_BluetoothRemoteGATTServer_connect
  if (!bleVarToAddr(addr, &peer_addr)) return;
  jsble_central_connect(peer_addr, options);
}
#endif

JsVar *jswrap_ble_BluetoothRemoteGATTServer_connect(JsVar *parent, JsVar *options) {
#if CENTRAL_LINK_COUNT>0

  JsVar *device = jsvObjectGetChild(parent, "device", 0);
  JsVar *addr = jsvObjectGetChild(device, "id", 0);
  // Convert mac address to something readable
  ble_gap_addr_t peer_addr;
  if (!bleVarToAddr(addr, &peer_addr)) {
    jsvUnLock2(device, addr);
    jsExceptionHere(JSET_TYPEERROR, "Expecting a device with a mac address of the form aa:bb:cc:dd:ee:ff");
    return 0;
  }
  jsvUnLock(device);

  // we're already connected - just return a resolved promise
  if (jsvGetBoolAndUnLock(jsvObjectGetChild(parent,"connected",0))) {
    return jswrap_promise_resolve(parent);
  }

  JsVar *promise = 0;
  if (bleNewTask(BLETASK_CONNECT, parent/*BluetoothRemoteGATTServer*/)) {
    JsVar *fn = jsvNewNativeFunction((void (*)(void))_jswrap_ble_central_connect, JSWAT_VOID|(JSWAT_JSVAR<<JSWAT_BITS)|(JSWAT_JSVAR<<(2*JSWAT_BITS)));
    if (fn) {
      JsVar *args[] = {addr, options};
      jsiQueueEvents(0, fn, args, 2);
      jsvUnLock(fn);
      promise = jsvLockAgainSafe(blePromise);
    }
  }
  jsvUnLock(addr);
  return promise;
#else
  jsExceptionHere(JSET_ERROR, "Unimplemented");
  return 0;
#endif
}

/*JSON{
  "type" : "class",
  "class" : "BluetoothRemoteGATTServer",
    "#if" : "defined(NRF52) || defined(ESP32)"
}
Web Bluetooth-style GATT server - get this using `NRF.connect(address)`
or `NRF.requestDevice(options)` and `response.gatt.connect`

https://webbluetoothcg.github.io/web-bluetooth/#bluetoothremotegattserver
*/
/*JSON{
    "type" : "property",
    "class" : "BluetoothDevice",
    "name" : "connected",
    "generate" : false,
    "return" : ["bool", "Whether the device is connected or not" ]
}
*//*Documentation only*/
/*JSON{
    "type" : "method",
    "class" : "BluetoothRemoteGATTServer",
    "name" : "disconnect",
    "generate" : "jswrap_BluetoothRemoteGATTServer_disconnect",
    "return" : ["JsVar", "A `Promise` that is resolved (or rejected) when the disconnection is complete (non-standard)" ],
    "return_object" : "Promise",
    "#if" : "defined(NRF52) || defined(ESP32)"
}
Disconnect from a previously connected BLE device connected with
`BluetoothRemoteGATTServer.connect` - this does not disconnect from something that has
connected to the Espruino.

**Note:** While `.disconnect` is standard Web Bluetooth, in the spec it
returns undefined not a `Promise` for implementation reasons. In Espruino
we return a `Promise` to make it easier to detect when Espruino is free
to connect to something else.
*/
JsVar *jswrap_BluetoothRemoteGATTServer_disconnect(JsVar *parent) {
#if CENTRAL_LINK_COUNT>0
  uint32_t              err_code;

  if (m_central_conn_handle != BLE_CONN_HANDLE_INVALID) {
    // we have a connection, disconnect
    JsVar *promise = 0;
    if (bleNewTask(BLETASK_DISCONNECT, parent/*BluetoothRemoteGATTServer*/))
      promise = jsvLockAgainSafe(blePromise);
    err_code = jsble_disconnect(m_central_conn_handle);
    jsble_check_error(err_code);
    return promise;
  } else {
    // no connection - try and cancel the connect attempt (assume we have one)
#ifdef NRF52
    err_code = sd_ble_gap_connect_cancel();
#endif
#ifdef ESP32
    jsWarn("connect cancel not implemented yet\n");
#endif
    // maybe we don't, in which case we don't care about the error code
    return jswrap_promise_resolve(parent);
  }
#else
  jsExceptionHere(JSET_ERROR, "Unimplemented");
#endif
  return 0;
}

/*JSON{
    "type" : "method",
    "class" : "BluetoothRemoteGATTServer",
    "name" : "startBonding",
    "ifdef" : "NRF52",
    "generate" : "jswrap_ble_BluetoothRemoteGATTServer_startBonding",
    "params" : [
      ["forceRePair","bool","If the device is already bonded, re-pair it"]
    ],
    "return" : ["JsVar", "A `Promise` that is resolved (or rejected) when the bonding is complete" ],
    "return_object" : "Promise"
}
Start negotiating bonding (secure communications) with the connected device,
and return a Promise that is completed on success or failure.

```
var gatt;
NRF.requestDevice({ filters: [{ name: 'Puck.js abcd' }] }).then(function(device) {
  console.log("found device");
  return device.gatt.connect();
}).then(function(g) {
  gatt = g;
  console.log("connected");
  return gatt.startBonding();
}).then(function() {
  console.log("bonded", gatt.getSecurityStatus());
  gatt.disconnect();
}).catch(function(e) {
  console.log("ERROR",e);
});
```

**This is not part of the Web Bluetooth Specification.** It has been added
specifically for Espruino.
*/
JsVar *jswrap_ble_BluetoothRemoteGATTServer_startBonding(JsVar *parent, bool forceRePair) {
#if CENTRAL_LINK_COUNT>0
  if (bleNewTask(BLETASK_BONDING, parent/*BluetoothRemoteGATTServer*/)) {
    JsVar *promise = jsvLockAgainSafe(blePromise);
    jsble_central_startBonding(forceRePair);
    return promise;
  }
  return 0;
#else
  jsExceptionHere(JSET_ERROR, "Unimplemented");
  return 0;
#endif
}


/*JSON{
    "type" : "method",
    "class" : "BluetoothRemoteGATTServer",
    "name" : "getSecurityStatus",
    "ifdef" : "NRF52",
    "generate" : "jswrap_ble_BluetoothRemoteGATTServer_getSecurityStatus",
    "return" : ["JsVar", "An object" ]
}
Return an object with information about the security
state of the current connection:


```
{
  connected       // The connection is active (not disconnected).
  encrypted       // Communication on this link is encrypted.
  mitm_protected  // The encrypted communication is also protected against man-in-the-middle attacks.
  bonded          // The peer is bonded with us
}
```

See `BluetoothRemoteGATTServer.startBonding` for information about
negotiating a secure connection.

**This is not part of the Web Bluetooth Specification.** It has been added
specifically for Puck.js.
*/
JsVar *jswrap_ble_BluetoothRemoteGATTServer_getSecurityStatus(JsVar *parent) {
#if CENTRAL_LINK_COUNT>0
  return jsble_get_security_status(m_central_conn_handle);
#else
  jsExceptionHere(JSET_ERROR, "Unimplemented");
  return 0;
#endif
}

/*JSON{
  "type" : "method",
  "class" : "BluetoothRemoteGATTServer",
  "name" : "getPrimaryService",
  "generate" : "jswrap_BluetoothRemoteGATTServer_getPrimaryService",
  "params" : [ ["service","JsVar","The service UUID"] ],
  "return" : ["JsVar", "A `Promise` that is resolved (or rejected) when the primary service is found (the argument contains a `BluetoothRemoteGATTService`)" ],
    "return_object" : "Promise",
  "#if" : "defined(NRF52) || defined(ESP32)"
}
See `NRF.connect` for usage examples.
*/
JsVar *jswrap_BluetoothRemoteGATTServer_getPrimaryService(JsVar *parent, JsVar *service) {
#if CENTRAL_LINK_COUNT>0
  const char *err;
  ble_uuid_t uuid;

  if (!bleNewTask(BLETASK_PRIMARYSERVICE, 0))
    return 0;

  err = bleVarToUUID(&uuid, service);
  if (err) {
    jsExceptionHere(JSET_ERROR, "%s", err);
    return 0;
  }

  JsVar *promise = jsvLockAgainSafe(blePromise);
  jsble_central_getPrimaryServices(uuid);
  return promise;
#else
  jsExceptionHere(JSET_ERROR, "Unimplemented");
  return 0;
#endif
}
/*JSON{
  "type" : "method",
  "class" : "BluetoothRemoteGATTServer",
  "name" : "getPrimaryServices",
  "generate" : "jswrap_BluetoothRemoteGATTServer_getPrimaryServices",
  "return" : ["JsVar", "A `Promise` that is resolved (or rejected) when the primary services are found (the argument contains an array of `BluetoothRemoteGATTService`)" ],
    "return_object" : "Promise",
  "#if" : "defined(NRF52) || defined(ESP32)"
}
*/
JsVar *jswrap_BluetoothRemoteGATTServer_getPrimaryServices(JsVar *parent) {
#if CENTRAL_LINK_COUNT>0
  ble_uuid_t uuid;
  uuid.type = BLE_UUID_TYPE_UNKNOWN;

  if (!bleNewTask(BLETASK_PRIMARYSERVICE, 0))
    return 0;
  JsVar *promise = jsvLockAgainSafe(blePromise);
  jsble_central_getPrimaryServices(uuid);
  return promise;
#else
  jsExceptionHere(JSET_ERROR, "Unimplemented");
  return 0;
#endif
}

/*JSON{
  "type" : "method",
  "class" : "BluetoothRemoteGATTServer",
  "name" : "setRSSIHandler",
  "generate" : "jswrap_BluetoothRemoteGATTServer_setRSSIHandler",
  "params" : [
    ["callback","JsVar","The callback to call with the RSSI value, or undefined to stop"]
  ],
  "#if" : "defined(NRF52) || defined(ESP32)"
}

Start/stop listening for RSSI values on the active GATT connection

```
// Start listening for RSSI value updates
gattServer.setRSSIHandler(function(rssi) {
  console.log(rssi); // prints -85 (or similar)
});
// Stop listening
gattServer.setRSSIHandler();
```

RSSI is the 'Received Signal Strength Indication' in dBm

*/
void jswrap_BluetoothRemoteGATTServer_setRSSIHandler(JsVar *parent, JsVar *callback) {
#if CENTRAL_LINK_COUNT>0
  // set the callback event variable
  if (!jsvIsFunction(callback)) callback=0;
  jsvObjectSetChild(parent, BLE_RSSI_EVENT, callback);
  // either start or stop scanning
  uint32_t err_code = jsble_set_central_rssi_scan(callback != 0);
  jsble_check_error(err_code);
#else
  jsExceptionHere(JSET_ERROR, "Unimplemented");
  return;
#endif
}

/*JSON{
  "type" : "class",
  "class" : "BluetoothRemoteGATTService",
  "#if" : "defined(NRF52) || defined(ESP32)"
}
Web Bluetooth-style GATT service - get this using `BluetoothRemoteGATTServer.getPrimaryService(s)`

https://webbluetoothcg.github.io/web-bluetooth/#bluetoothremotegattservice
*/
/*JSON{
  "type" : "method",
  "class" : "BluetoothRemoteGATTService",
  "name" : "getCharacteristic",
  "generate" : "jswrap_BluetoothRemoteGATTService_getCharacteristic",
  "params" : [ ["characteristic","JsVar","The characteristic UUID"] ],
  "return" : ["JsVar", "A `Promise` that is resolved (or rejected) when the characteristic is found (the argument contains a `BluetoothRemoteGATTCharacteristic`)" ],
    "return_object" : "Promise",
  "#if" : "defined(NRF52) || defined(ESP32)"
}
See `NRF.connect` for usage examples.
*/
JsVar *jswrap_BluetoothRemoteGATTService_getCharacteristic(JsVar *parent, JsVar *characteristic) {
#if CENTRAL_LINK_COUNT>0
  const char *err;
  ble_uuid_t uuid;

  if (!bleNewTask(BLETASK_CHARACTERISTIC, 0))
    return 0;

  err = bleVarToUUID(&uuid, characteristic);
  if (err) {
    jsExceptionHere(JSET_ERROR, "%s", err);
    return 0;
  }

  JsVar *promise = jsvLockAgainSafe(blePromise);
  jsble_central_getCharacteristics(parent, uuid);
  return promise;
#else
  jsExceptionHere(JSET_ERROR, "Unimplemented");
  return 0;
#endif
}
/*JSON{
  "type" : "method",
  "class" : "BluetoothRemoteGATTService",
  "name" : "getCharacteristics",
  "generate" : "jswrap_BluetoothRemoteGATTService_getCharacteristics",
  "return" : ["JsVar", "A `Promise` that is resolved (or rejected) when the characteristic is found (the argument contains an array of `BluetoothRemoteGATTCharacteristic`)" ],
    "return_object" : "Promise",
  "#if" : "defined(NRF52) || defined(ESP32)"
}
*/
JsVar *jswrap_BluetoothRemoteGATTService_getCharacteristics(JsVar *parent) {
#if CENTRAL_LINK_COUNT>0
  ble_uuid_t uuid;
  uuid.type = BLE_UUID_TYPE_UNKNOWN;

  if (!bleNewTask(BLETASK_CHARACTERISTIC, 0))
    return 0;

  JsVar *promise = jsvLockAgainSafe(blePromise);
  jsble_central_getCharacteristics(parent, uuid);
  return promise;
#else
  jsExceptionHere(JSET_ERROR, "Unimplemented");
  return 0;
#endif
}

/*JSON{
  "type" : "class",
  "class" : "BluetoothRemoteGATTCharacteristic",
  "#if" : "defined(NRF52) || defined(ESP32)"
}
Web Bluetooth-style GATT characteristic - get this using `BluetoothRemoteGATTService.getCharacteristic(s)`

https://webbluetoothcg.github.io/web-bluetooth/#bluetoothremotegattcharacteristic
*/
/*JSON{
    "type" : "method",
    "class" : "BluetoothRemoteGATTCharacteristic",
    "name" : "writeValue",
    "generate" : "jswrap_ble_BluetoothRemoteGATTCharacteristic_writeValue",
    "params" : [
      ["data","JsVar","The data to write"]
    ],
    "return" : ["JsVar", "A `Promise` that is resolved (or rejected) when the characteristic is written" ],
    "return_object" : "Promise",
    "#if" : "defined(NRF52) || defined(ESP32)"
}

Write a characteristic's value

```
var device;
NRF.connect(device_address).then(function(d) {
  device = d;
  return d.getPrimaryService("service_uuid");
}).then(function(s) {
  console.log("Service ",s);
  return s.getCharacteristic("characteristic_uuid");
}).then(function(c) {
  return c.writeValue("Hello");
}).then(function(d) {
  device.disconnect();
}).catch(function() {
  console.log("Something's broken.");
});
```
*/
JsVar *jswrap_ble_BluetoothRemoteGATTCharacteristic_writeValue(JsVar *characteristic, JsVar *data) {
#if CENTRAL_LINK_COUNT>0
  JSV_GET_AS_CHAR_ARRAY(dataPtr, dataLen, data);
  if (!dataPtr) return 0;

  if (!bleNewTask(BLETASK_CHARACTERISTIC_WRITE, 0))
    return 0;

  JsVar *promise = jsvLockAgainSafe(blePromise);
  jsble_central_characteristicWrite(characteristic, dataPtr, dataLen);
  return promise;
#else
  jsExceptionHere(JSET_ERROR, "Unimplemented");
  return 0;
#endif
}
/*JSON{
    "type" : "method",
    "class" : "BluetoothRemoteGATTCharacteristic",
    "name" : "readValue",
    "generate" : "jswrap_ble_BluetoothRemoteGATTCharacteristic_readValue",
    "return" : ["JsVar", "A `Promise` that is resolved (or rejected) with a `DataView` when the characteristic is read" ],
    "return_object" : "Promise",
    "#if" : "defined(NRF52) || defined(ESP32)"
}

Read a characteristic's value, return a promise containing a `DataView`

```
var device;
NRF.connect(device_address).then(function(d) {
  device = d;
  return d.getPrimaryService("service_uuid");
}).then(function(s) {
  console.log("Service ",s);
  return s.getCharacteristic("characteristic_uuid");
}).then(function(c) {
  return c.readValue();
}).then(function(d) {
  console.log("Got:", JSON.stringify(d.buffer));
  device.disconnect();
}).catch(function() {
  console.log("Something's broken.");
});
```
*/
JsVar *jswrap_ble_BluetoothRemoteGATTCharacteristic_readValue(JsVar *characteristic) {
#if CENTRAL_LINK_COUNT>0
  if (!bleNewTask(BLETASK_CHARACTERISTIC_READ, characteristic))
    return 0;

  JsVar *promise = jsvLockAgainSafe(blePromise);
  jsble_central_characteristicRead(characteristic);
  return promise;
#else
  jsExceptionHere(JSET_ERROR, "Unimplemented");
  return 0;
#endif
}

/*JSON{
    "type" : "method",
    "class" : "BluetoothRemoteGATTCharacteristic",
    "name" : "startNotifications",
    "generate" : "jswrap_ble_BluetoothRemoteGATTCharacteristic_startNotifications",
    "return" : ["JsVar", "A `Promise` that is resolved (or rejected) with data when notifications have been added" ],
    "return_object" : "Promise",
    "ifdef" : "NRF52"
}
Starts notifications - whenever this characteristic's value changes, a `characteristicvaluechanged` event is fired
and `characteristic.value` will then contain the new value as a `DataView`.

```
var device;
NRF.connect(device_address).then(function(d) {
  device = d;
  return d.getPrimaryService("service_uuid");
}).then(function(s) {
  console.log("Service ",s);
  return s.getCharacteristic("characteristic_uuid");
}).then(function(c) {
  c.on('characteristicvaluechanged', function(event) {
    console.log("-> "+event.target.value);
  });
  return c.startNotifications();
}).then(function(d) {
  console.log("Waiting for notifications");
}).catch(function() {
  console.log("Something's broken.");
});
```

For example, to listen to the output of another Puck.js's Nordic
Serial port service, you can use:

```
var gatt;
NRF.connect("pu:ck:js:ad:dr:es random").then(function(g) {
  gatt = g;
  return gatt.getPrimaryService("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
}).then(function(service) {
  return service.getCharacteristic("6e400003-b5a3-f393-e0a9-e50e24dcca9e");
}).then(function(characteristic) {
  characteristic.on('characteristicvaluechanged', function(event) {
    console.log("RX: "+JSON.stringify(event.target.value.buffer));
  });
  return characteristic.startNotifications();
}).then(function() {
  console.log("Done!");
});
```
*/
JsVar *jswrap_ble_BluetoothRemoteGATTCharacteristic_startNotifications(JsVar *characteristic) {
#if CENTRAL_LINK_COUNT>0
  
  // Set our characteristic's handle up in the list of handles to notify for
  // TODO: What happens when we close the connection and re-open another?
  uint16_t handle = (uint16_t)jsvGetIntegerAndUnLock(jsvObjectGetChild(characteristic, "handle_value", 0));
  JsVar *handles = jsvObjectGetChild(execInfo.hiddenRoot, "bleHdl", JSV_ARRAY);
  if (handles) {
    jsvSetArrayItem(handles, handle, characteristic);
    jsvUnLock(handles);
  }
  
  JsVar *promise;
  
  // Check for existing cccd_handle 
  uint16_t cccd = (uint16_t)jsvGetIntegerAndUnLock(jsvObjectGetChild(characteristic,"handle_cccd", 0));
  if ( !cccd ) {
    if (!bleNewTask(BLETASK_CHARACTERISTIC_DESC_AND_STARTNOTIFY, characteristic))
      return 0;
    promise = jsvLockAgainSafe(blePromise);
    jsble_central_characteristicDescDiscover(characteristic);
  }
  else {
    if (!bleNewTask(BLETASK_CHARACTERISTIC_NOTIFY, 0))
      return 0;
    promise = jsvLockAgainSafe(blePromise);    
    jsble_central_characteristicNotify(characteristic, true);
  }
  return promise;
#else
  jsExceptionHere(JSET_ERROR, "Unimplemented");
  return 0;
#endif
}

/*JSON{
    "type" : "method",
    "class" : "BluetoothRemoteGATTCharacteristic",
    "name" : "stopNotifications",
    "generate" : "jswrap_ble_BluetoothRemoteGATTCharacteristic_stopNotifications",
    "return" : ["JsVar", "A `Promise` that is resolved (or rejected) with data when notifications have been removed" ],
    "return_object" : "Promise",
    "ifdef" : "NRF52"
}
Stop notifications (that were requested with `BluetoothRemoteGATTCharacteristic.startNotifications`)
*/
JsVar *jswrap_ble_BluetoothRemoteGATTCharacteristic_stopNotifications(JsVar *characteristic) {
#if CENTRAL_LINK_COUNT>0
  // Remove our characteristic handle from the list of handles to notify for
  uint16_t handle = (uint16_t)jsvGetIntegerAndUnLock(jsvObjectGetChild(characteristic, "handle_value", 0));
  JsVar *handles = jsvObjectGetChild(execInfo.hiddenRoot, "bleHdl", JSV_ARRAY);
  if (handles) {
    jsvSetArrayItem(handles, handle, 0);
    jsvUnLock(handles);
  }
  JsVar *promise = jsvLockAgainSafe(blePromise);
  jsble_central_characteristicNotify(characteristic, false);
  return promise;
#else
  jsExceptionHere(JSET_ERROR, "Unimplemented");
  return 0;
#endif
}
