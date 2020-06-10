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
 * JavaScript 'process' object - for information about the Espruino board
 * ----------------------------------------------------------------------------
 */
#include "jsvar.h"
#include "jsparse.h"
#include "jswrap_process.h"
#include "jswrap_interactive.h"
#include "jswrap_espruino.h" // jswrap_espruino_getConsole
#include "jswrapper.h"
#include "jsinteractive.h"
#ifdef PUCKJS
#include "jswrap_puck.h" // process.env
#endif

/*JSON{
  "type" : "class",
  "class" : "process"
}
This class contains information about Espruino itself
 */

/*JSON{
  "type" : "event",
  "class" : "process",
  "name" : "uncaughtException",
  "params" : [["exception","JsVar","The uncaught exception"]]
}
This event is called when an exception gets thrown and isn't caught (eg. it gets all the way back to the event loop).

You can use this for logging potential problems that might occur during execution when you
might not be able to see what is written to the console, for example:

```
var lastError;
process.on('uncaughtException', function(e) {
  lastError=e;
  print(e,e.stack?"\n"+e.stack:"")
});

function checkError() {
  if (!lastError) return print("No Error");
  print(lastError,lastError.stack?"\n"+lastError.stack:"")
}
```

**Note:** When this is used, exceptions will cease to be reported on the console - which
may make debugging difficult!
*/

/*JSON{
  "type" : "staticproperty",
  "class" : "process",
  "name" : "version",
  "generate_full" : "jsvNewFromString(JS_VERSION)",
  "return" : ["JsVar","The version of Espruino"]
}
Returns the version of Espruino as a String
 */

#ifndef SAVE_ON_FLASH
/* NOTE: The order of these is very important, as 
the online compiler has its own copy of this table */
const void *exportPtrs[] = {
    jsvLockAgainSafe,
    jsvUnLock,
    jsvSkipName,
    jsvMathsOp,
    jsvNewWithFlags,
    jsvNewFromFloat,
    jsvNewFromInteger,
    jsvNewFromString,
    jsvNewFromBool,
    jsvGetFloat,
    jsvGetInteger,
    jsvGetBool,
    jsvReplaceWith,
    jspeFunctionCall,
    jspGetNamedVariable,
    jspGetNamedField,
    jspGetVarNamedField,
    0
};
#endif

/*JSON{
  "type" : "staticproperty",
  "class" : "process",
  "name" : "env",
  "generate" : "jswrap_process_env",
  "return" : ["JsVar","An object"]
}
Returns an Object containing various pre-defined variables. standard ones are BOARD, VERSION, FLASH, RAM, MODULES.

For example, to get a list of built-in modules, you can use `process.env.MODULES.split(',')`
 */
JsVar *jswrap_process_env() {
  JsVar *obj = jsvNewObject();
  jsvObjectSetChildAndUnLock(obj, "VERSION", jsvNewFromString(JS_VERSION));
#ifdef GIT_COMMIT
  jsvObjectSetChildAndUnLock(obj, "GIT_COMMIT", jsvNewFromString(STRINGIFY(GIT_COMMIT)));
#endif
  jsvObjectSetChildAndUnLock(obj, "BOARD", jsvNewFromString(PC_BOARD_ID));
  jsvObjectSetChildAndUnLock(obj, "FLASH", jsvNewFromInteger(FLASH_TOTAL));
#ifdef SPIFLASH_LENGTH
  jsvObjectSetChildAndUnLock(obj, "SPIFLASH", jsvNewFromInteger(SPIFLASH_LENGTH));
#endif
#ifdef PUCKJS
  jsvObjectSetChildAndUnLock(obj, "HWVERSION", jsvNewFromInteger(isPuckV2?2:1));
#endif
  jsvObjectSetChildAndUnLock(obj, "STORAGE", jsvNewFromInteger(FLASH_SAVED_CODE_LENGTH));
  jsvObjectSetChildAndUnLock(obj, "RAM", jsvNewFromInteger(RAM_TOTAL));
  jsvObjectSetChildAndUnLock(obj, "SERIAL", jswrap_interface_getSerial());
  jsvObjectSetChildAndUnLock(obj, "CONSOLE", jswrap_espruino_getConsole());
  jsvObjectSetChildAndUnLock(obj, "MODULES", jsvNewFromString(jswGetBuiltInLibraryNames()));
#ifndef SAVE_ON_FLASH
  // Pointer to a list of predefined exports - eventually we'll get rid of the array above
  jsvObjectSetChildAndUnLock(obj, "EXPTR", jsvNewFromInteger((JsVarInt)(size_t)exportPtrs));
#endif
  return obj;
}


/*JSON{
  "type" : "staticmethod",
  "class" : "process",
  "name" : "memory",
  "generate" : "jswrap_process_memory",
  "return" : ["JsVar","Information about memory usage"]
}
Run a Garbage Collection pass, and return an object containing information on memory usage.

* `free`  : Memory that is available to be used (in blocks)
* `usage` : Memory that has been used (in blocks)
* `total` : Total memory (in blocks)
* `history` : Memory used for command history - that is freed if memory is low. Note that this is INCLUDED in the figure for 'free'
* `gc`      : Memory freed during the GC pass
* `gctime`  : Time taken for GC pass (in milliseconds)
* `blocksize` : Size of a block (variable) in bytes
* `stackEndAddress` : (on ARM) the address (that can be used with peek/poke/etc) of the END of the stack. The stack grows down, so unless you do a lot of recursion the bytes above this can be used.
* `flash_start`      : (on ARM) the address of the start of flash memory (usually `0x8000000`)
* `flash_binary_end` : (on ARM) the address in flash memory of the end of Espruino's firmware.
* `flash_code_start` : (on ARM) the address in flash memory of pages that store any code that you save with `save()`.
* `flash_length` : (on ARM) the amount of flash memory this firmware was built for (in bytes). **Note:** Some STM32 chips actually have more memory than is advertised.

Memory units are specified in 'blocks', which are around 16 bytes each (depending on your device). The actual size is available in `blocksize`. See http://www.espruino.com/Performance for more information.

**Note:** To find free areas of flash memory, see `require('Flash').getFree()`
 */
JsVar *jswrap_process_memory() {
  JsSysTime time1 = jshGetSystemTime();
  int gc = jsvGarbageCollect();
  JsSysTime time2 = jshGetSystemTime();
  JsVar *obj = jsvNewObject();
  if (obj) {
    unsigned int history = 0;
    JsVar *historyVar = jsvObjectGetChild(execInfo.hiddenRoot, JSI_HISTORY_NAME, 0);
    if (historyVar) {
      history = (unsigned int)jsvCountJsVarsUsed(historyVar); // vars used to store history
      jsvUnLock(historyVar);
    }
    unsigned int usage = jsvGetMemoryUsage() - history;
    unsigned int total = jsvGetMemoryTotal();
    jsvObjectSetChildAndUnLock(obj, "free", jsvNewFromInteger((JsVarInt)(total-usage)));
    jsvObjectSetChildAndUnLock(obj, "usage", jsvNewFromInteger((JsVarInt)usage));
    jsvObjectSetChildAndUnLock(obj, "total", jsvNewFromInteger((JsVarInt)total));
    jsvObjectSetChildAndUnLock(obj, "history", jsvNewFromInteger((JsVarInt)history));
    jsvObjectSetChildAndUnLock(obj, "gc", jsvNewFromInteger((JsVarInt)gc));
    jsvObjectSetChildAndUnLock(obj, "gctime", jsvNewFromFloat(jshGetMillisecondsFromTime(time2-time1)));
    jsvObjectSetChildAndUnLock(obj, "blocksize", jsvNewFromInteger(sizeof(JsVar)));

#ifdef ARM
    extern uint32_t LINKER_END_VAR; // end of ram used (variables) - should be 'void', but 'int' avoids warnings
    extern uint32_t LINKER_ETEXT_VAR; // end of flash text (binary) section - should be 'void', but 'int' avoids warnings
    jsvObjectSetChildAndUnLock(obj, "stackEndAddress", jsvNewFromInteger((JsVarInt)(unsigned int)&LINKER_END_VAR));
    jsvObjectSetChildAndUnLock(obj, "flash_start", jsvNewFromInteger((JsVarInt)FLASH_START));
    jsvObjectSetChildAndUnLock(obj, "flash_binary_end", jsvNewFromInteger((JsVarInt)(unsigned int)&LINKER_ETEXT_VAR));
    jsvObjectSetChildAndUnLock(obj, "flash_code_start", jsvNewFromInteger((JsVarInt)FLASH_SAVED_CODE_START));
    jsvObjectSetChildAndUnLock(obj, "flash_length", jsvNewFromInteger((JsVarInt)FLASH_TOTAL));
#endif
  }
  return obj;
}
