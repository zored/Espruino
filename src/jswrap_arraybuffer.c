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
 * JavaScript methods for ArrayBuffers
 * ----------------------------------------------------------------------------
 */
#include "jswrap_arraybuffer.h"
#include "jsparse.h"
#include "jsinteractive.h"

/*JSON{
  "type" : "class",
  "class" : "ArrayBuffer",
  "check" : "jsvIsArrayBuffer(var) && var->varData.arraybuffer.type==ARRAYBUFFERVIEW_ARRAYBUFFER",
  "not_real_object" : "Don't treat this as a real object - it's handled differently internally"
}
This is the built-in JavaScript class for array buffers.

If you want to access arrays of differing types of data
you may also find `DataView` useful.
 */

/*JSON{
  "type" : "class",
  "class" : "ArrayBufferView"
}
This is the built-in JavaScript class that is the prototype for:

* [Uint8Array](/Reference#Uint8Array)
* [UintClamped8Array](/Reference#UintClamped8Array)
* [Int8Array](/Reference#Int8Array)
* [Uint16Array](/Reference#Uint16Array)
* [Int16Array](/Reference#Int16Array)
* [Uint24Array](/Reference#Uint24Array) (Espruino-specific - not standard JS)
* [Uint32Array](/Reference#Uint32Array)
* [Int32Array](/Reference#Int32Array)
* [Float32Array](/Reference#Float32Array)
* [Float64Array](/Reference#Float64Array)

If you want to access arrays of differing types of data
you may also find `DataView` useful.
*/

/*JSON{
  "type" : "class",
  "class" : "Uint8Array",
  "prototype" : "ArrayBufferView",
  "check" : "jsvIsArrayBuffer(var) && var->varData.arraybuffer.type==ARRAYBUFFERVIEW_UINT8",
  "not_real_object" : "Don't treat this as a real object - it's handled differently internally"
}
This is the built-in JavaScript class for a typed array of 8 bit unsigned integers.

Instantiate this in order to efficiently store arrays of data (Espruino's normal arrays store data in a map, which is inefficient for non-sparse arrays).

Arrays of this type include all the methods from [ArrayBufferView](/Reference#ArrayBufferView)
 */
/*JSON{
  "type" : "class",
  "class" : "Uint8ClampedArray",
  "prototype" : "ArrayBufferView",
  "check" : "jsvIsArrayBuffer(var) && var->varData.arraybuffer.type==(ARRAYBUFFERVIEW_UINT8|ARRAYBUFFERVIEW_CLAMPED)",
  "not_real_object" : "Don't treat this as a real object - it's handled differently internally"
}
This is the built-in JavaScript class for a typed array of 8 bit unsigned integers that are automatically clamped to the range 0 to 255.

Instantiate this in order to efficiently store arrays of data (Espruino's normal arrays store data in a map, which is inefficient for non-sparse arrays).

Arrays of this type include all the methods from [ArrayBufferView](/Reference#ArrayBufferView)
 */
/*JSON{
  "type" : "class",
  "class" : "Int8Array",
  "prototype" : "ArrayBufferView",
  "check" : "jsvIsArrayBuffer(var) && var->varData.arraybuffer.type==ARRAYBUFFERVIEW_INT8",
  "not_real_object" : "Don't treat this as a real object - it's handled differently internally"
}
This is the built-in JavaScript class for a typed array of 8 bit signed integers.

Instantiate this in order to efficiently store arrays of data (Espruino's normal arrays store data in a map, which is inefficient for non-sparse arrays).

Arrays of this type include all the methods from [ArrayBufferView](/Reference#ArrayBufferView)
 */
/*JSON{
  "type" : "class",
  "class" : "Uint16Array",
  "prototype" : "ArrayBufferView",
  "check" : "jsvIsArrayBuffer(var) && var->varData.arraybuffer.type==ARRAYBUFFERVIEW_UINT16",
  "not_real_object" : "Don't treat this as a real object - it's handled differently internally"
}
This is the built-in JavaScript class for a typed array of 16 bit unsigned integers.

Instantiate this in order to efficiently store arrays of data (Espruino's normal arrays store data in a map, which is inefficient for non-sparse arrays).

Arrays of this type include all the methods from [ArrayBufferView](/Reference#ArrayBufferView)
 */
/*JSON{
  "type" : "class",
  "class" : "Int16Array",
  "prototype" : "ArrayBufferView",
  "check" : "jsvIsArrayBuffer(var) && var->varData.arraybuffer.type==ARRAYBUFFERVIEW_INT16",
  "not_real_object" : "Don't treat this as a real object - it's handled differently internally"
}
This is the built-in JavaScript class for a typed array of 16 bit signed integers.

Instantiate this in order to efficiently store arrays of data (Espruino's normal arrays store data in a map, which is inefficient for non-sparse arrays).

Arrays of this type include all the methods from [ArrayBufferView](/Reference#ArrayBufferView)
 */
/*JSON{
  "type" : "class",
  "class" : "Uint24Array",
  "ifndef" : "SAVE_ON_FLASH",
  "prototype" : "ArrayBufferView",
  "check" : "jsvIsArrayBuffer(var) && var->varData.arraybuffer.type==ARRAYBUFFERVIEW_UINT24",
  "not_real_object" : "Don't treat this as a real object - it's handled differently internally"
}
This is the built-in JavaScript class for a typed array of 24 bit unsigned integers.

Instantiate this in order to efficiently store arrays of data (Espruino's normal arrays store data in a map, which is inefficient for non-sparse arrays).

Arrays of this type include all the methods from [ArrayBufferView](/Reference#ArrayBufferView)
 */
/*JSON{
  "type" : "class",
  "class" : "Uint32Array",
  "prototype" : "ArrayBufferView",
  "check" : "jsvIsArrayBuffer(var) && var->varData.arraybuffer.type==ARRAYBUFFERVIEW_UINT32",
  "not_real_object" : "Don't treat this as a real object - it's handled differently internally"
}
This is the built-in JavaScript class for a typed array of 32 bit unsigned integers.

Instantiate this in order to efficiently store arrays of data (Espruino's normal arrays store data in a map, which is inefficient for non-sparse arrays).

Arrays of this type include all the methods from [ArrayBufferView](/Reference#ArrayBufferView)
 */
/*JSON{
  "type" : "class",
  "class" : "Int32Array",
  "prototype" : "ArrayBufferView",
  "check" : "jsvIsArrayBuffer(var) && var->varData.arraybuffer.type==ARRAYBUFFERVIEW_INT32",
  "not_real_object" : "Don't treat this as a real object - it's handled differently internally"
}
This is the built-in JavaScript class for a typed array of 32 bit signed integers.

Instantiate this in order to efficiently store arrays of data (Espruino's normal arrays store data in a map, which is inefficient for non-sparse arrays).

Arrays of this type include all the methods from [ArrayBufferView](/Reference#ArrayBufferView)
 */
/*JSON{
  "type" : "class",
  "class" : "Float32Array",
  "prototype" : "ArrayBufferView",
  "check" : "jsvIsArrayBuffer(var) && var->varData.arraybuffer.type==ARRAYBUFFERVIEW_FLOAT32",
  "not_real_object" : "Don't treat this as a real object - it's handled differently internally"
}
This is the built-in JavaScript class for a typed array of 32 bit floating point values.

Instantiate this in order to efficiently store arrays of data (Espruino's normal arrays store data in a map, which is inefficient for non-sparse arrays).

Arrays of this type include all the methods from [ArrayBufferView](/Reference#ArrayBufferView)
 */
/*JSON{
  "type" : "class",
  "class" : "Float64Array",
  "prototype" : "ArrayBufferView",
  "check" : "jsvIsArrayBuffer(var) && var->varData.arraybuffer.type==ARRAYBUFFERVIEW_FLOAT64",
  "not_real_object" : "Don't treat this as a real object - it's handled differently internally"
}
This is the built-in JavaScript class for a typed array of 64 bit floating point values.

Instantiate this in order to efficiently store arrays of data (Espruino's normal arrays store data in a map, which is inefficient for non-sparse arrays).

Arrays of this type include all the methods from [ArrayBufferView](/Reference#ArrayBufferView)
 */


/*JSON{
  "type" : "constructor",
  "class" : "ArrayBuffer",
  "name" : "ArrayBuffer",
  "generate" : "jswrap_arraybuffer_constructor",
  "params" : [
    ["byteLength","int","The length in Bytes"]
  ],
  "return" : ["JsVar","An ArrayBuffer object"]
}
Create an Array Buffer object
 */
JsVar *jswrap_arraybuffer_constructor(JsVarInt byteLength) {
  if (byteLength < 0 || byteLength>65535) {
    jsExceptionHere(JSET_ERROR, "Invalid length for ArrayBuffer\n");
    return 0;
  }
  if (byteLength > JSV_ARRAYBUFFER_MAX_LENGTH) {
    jsExceptionHere(JSET_ERROR, "ArrayBuffer too long\n");
    return 0;
  }
  // try and use a flat string - which will be faster
  JsVar *arrData = 0;
  /* if the bytes could fit into 1 or 2 normal string blocks, do that.
   * It's faster to allocate and can use less memory (if it fits into one block) */
  if (byteLength > JSV_FLAT_STRING_BREAK_EVEN)
    arrData = jsvNewFlatStringOfLength((unsigned int)byteLength);
  // if we haven't found one, spread it out
  if (!arrData)
    arrData = jsvNewStringOfLength((unsigned int)byteLength, NULL);
  if (!arrData) return 0;
  JsVar *v = jsvNewArrayBufferFromString(arrData, (unsigned int)byteLength);
  jsvUnLock(arrData);
  return v;
}

/*JSON{
  "type" : "property",
  "class" : "ArrayBuffer",
  "name" : "byteLength",
  "generate_full" : "(JsVarInt)(parent->varData.arraybuffer.length)",
  "return" : ["int","The Length in bytes"]
}
The length, in bytes, of the `ArrayBuffer`
 */


/*
 * Potential invocations:
 * Uint8Array Uint8Array(unsigned long length);
 * Uint8Array Uint8Array(TypedArray array);
 * Uint8Array Uint8Array(sequence<type> array);
 * Uint8Array Uint8Array(ArrayBuffer buffer, optional unsigned long byteOffset, optional unsigned long length);
 */

/*JSON{
  "type" : "constructor",
  "class" : "Uint8Array",
  "name" : "Uint8Array",
  "generate_full" : "jswrap_typedarray_constructor(ARRAYBUFFERVIEW_UINT8, arr, byteOffset, length)",
  "params" : [
    ["arr","JsVar","The array or typed array to base this off, or an integer which is the array length"],
    ["byteOffset","int","The byte offset in the ArrayBuffer  (ONLY IF the first argument was an ArrayBuffer)"],
    ["length","int","The length (ONLY IF the first argument was an ArrayBuffer)"]
  ],
  "return" : ["JsVar","A typed array"],
  "return_object" : "ArrayBufferView"
}
Create a typed array based on the given input. Either an existing Array Buffer, an Integer as a Length, or a simple array. If an `ArrayBufferView` (eg. `Uint8Array` rather than `ArrayBuffer`) is given, it will be completely copied rather than referenced.
 */
/*JSON{
  "type" : "constructor",
  "class" : "Uint8ClampedArray",
  "name" : "Uint8ClampedArray",
  "generate_full" : "jswrap_typedarray_constructor(ARRAYBUFFERVIEW_UINT8|ARRAYBUFFERVIEW_CLAMPED, arr, byteOffset, length)",
  "params" : [
    ["arr","JsVar","The array or typed array to base this off, or an integer which is the array length"],
    ["byteOffset","int","The byte offset in the ArrayBuffer  (ONLY IF the first argument was an ArrayBuffer)"],
    ["length","int","The length (ONLY IF the first argument was an ArrayBuffer)"]
  ],
  "return" : ["JsVar","A typed array"],
  "return_object" : "ArrayBufferView"
}
Create a typed array based on the given input. Either an existing Array Buffer, an Integer as a Length, or a simple array. If an `ArrayBufferView` (eg. `Uint8Array` rather than `ArrayBuffer`) is given, it will be completely copied rather than referenced.

Clamped arrays clamp their values to the allowed range, rather than 'wrapping'. e.g. after `a[0]=12345;`, `a[0]==255`.
 */
/*JSON{
  "type" : "constructor",
  "class" : "Int8Array",
  "name" : "Int8Array",
  "generate_full" : "jswrap_typedarray_constructor(ARRAYBUFFERVIEW_INT8, arr, byteOffset, length)",
  "params" : [
    ["arr","JsVar","The array or typed array to base this off, or an integer which is the array length"],
    ["byteOffset","int","The byte offset in the ArrayBuffer  (ONLY IF the first argument was an ArrayBuffer)"],
    ["length","int","The length (ONLY IF the first argument was an ArrayBuffer)"]
  ],
  "return" : ["JsVar","A typed array"],
  "return_object" : "ArrayBufferView"
}
Create a typed array based on the given input. Either an existing Array Buffer, an Integer as a Length, or a simple array. If an `ArrayBufferView` (eg. `Uint8Array` rather than `ArrayBuffer`) is given, it will be completely copied rather than referenced.
 */
/*JSON{
  "type" : "constructor",
  "class" : "Uint16Array",
  "name" : "Uint16Array",
  "generate_full" : "jswrap_typedarray_constructor(ARRAYBUFFERVIEW_UINT16, arr, byteOffset, length)",
  "params" : [
    ["arr","JsVar","The array or typed array to base this off, or an integer which is the array length"],
    ["byteOffset","int","The byte offset in the ArrayBuffer  (ONLY IF the first argument was an ArrayBuffer)"],
    ["length","int","The length (ONLY IF the first argument was an ArrayBuffer)"]
  ],
  "return" : ["JsVar","A typed array"],
  "return_object" : "ArrayBufferView"
}
Create a typed array based on the given input. Either an existing Array Buffer, an Integer as a Length, or a simple array. If an `ArrayBufferView` (eg. `Uint8Array` rather than `ArrayBuffer`) is given, it will be completely copied rather than referenced.
 */
/*JSON{
  "type" : "constructor",
  "class" : "Int16Array",
  "name" : "Int16Array",
  "generate_full" : "jswrap_typedarray_constructor(ARRAYBUFFERVIEW_INT16, arr, byteOffset, length)",
  "params" : [
    ["arr","JsVar","The array or typed array to base this off, or an integer which is the array length"],
    ["byteOffset","int","The byte offset in the ArrayBuffer  (ONLY IF the first argument was an ArrayBuffer)"],
    ["length","int","The length (ONLY IF the first argument was an ArrayBuffer)"]
  ],
  "return" : ["JsVar","A typed array"],
  "return_object" : "ArrayBufferView"
}
Create a typed array based on the given input. Either an existing Array Buffer, an Integer as a Length, or a simple array. If an `ArrayBufferView` (eg. `Uint8Array` rather than `ArrayBuffer`) is given, it will be completely copied rather than referenced.
 */
/*JSON{
  "type" : "constructor",
  "class" : "Uint24Array",
  "name" : "Uint24Array",
  "generate_full" : "jswrap_typedarray_constructor(ARRAYBUFFERVIEW_UINT24, arr, byteOffset, length)",
  "params" : [
    ["arr","JsVar","The array or typed array to base this off, or an integer which is the array length"],
    ["byteOffset","int","The byte offset in the ArrayBuffer  (ONLY IF the first argument was an ArrayBuffer)"],
    ["length","int","The length (ONLY IF the first argument was an ArrayBuffer)"]
  ],
  "return" : ["JsVar","A typed array"],
  "return_object" : "ArrayBufferView"
}
Create a typed array based on the given input. Either an existing Array Buffer, an Integer as a Length, or a simple array. If an `ArrayBufferView` (eg. `Uint8Array` rather than `ArrayBuffer`) is given, it will be completely copied rather than referenced.
 */
/*JSON{
  "type" : "constructor",
  "class" : "Uint32Array",
  "name" : "Uint32Array",
  "ifndef" : "SAVE_ON_FLASH",
  "generate_full" : "jswrap_typedarray_constructor(ARRAYBUFFERVIEW_UINT32, arr, byteOffset, length)",
  "params" : [
    ["arr","JsVar","The array or typed array to base this off, or an integer which is the array length"],
    ["byteOffset","int","The byte offset in the ArrayBuffer  (ONLY IF the first argument was an ArrayBuffer)"],
    ["length","int","The length (ONLY IF the first argument was an ArrayBuffer)"]
  ],
  "return" : ["JsVar","A typed array"],
  "return_object" : "ArrayBufferView"
}
Create a typed array based on the given input. Either an existing Array Buffer, an Integer as a Length, or a simple array. If an `ArrayBufferView` (eg. `Uint8Array` rather than `ArrayBuffer`) is given, it will be completely copied rather than referenced.
 */
/*JSON{
  "type" : "constructor",
  "class" : "Int32Array",
  "name" : "Int32Array",
  "generate_full" : "jswrap_typedarray_constructor(ARRAYBUFFERVIEW_INT32, arr, byteOffset, length)",
  "params" : [
    ["arr","JsVar","The array or typed array to base this off, or an integer which is the array length"],
    ["byteOffset","int","The byte offset in the ArrayBuffer  (ONLY IF the first argument was an ArrayBuffer)"],
    ["length","int","The length (ONLY IF the first argument was an ArrayBuffer)"]
  ],
  "return" : ["JsVar","A typed array"],
  "return_object" : "ArrayBufferView"
}
Create a typed array based on the given input. Either an existing Array Buffer, an Integer as a Length, or a simple array. If an `ArrayBufferView` (eg. `Uint8Array` rather than `ArrayBuffer`) is given, it will be completely copied rather than referenced.
 */
/*JSON{
  "type" : "constructor",
  "class" : "Float32Array",
  "name" : "Float32Array",
  "generate_full" : "jswrap_typedarray_constructor(ARRAYBUFFERVIEW_FLOAT32, arr, byteOffset, length)",
  "params" : [
    ["arr","JsVar","The array or typed array to base this off, or an integer which is the array length"],
    ["byteOffset","int","The byte offset in the ArrayBuffer  (ONLY IF the first argument was an ArrayBuffer)"],
    ["length","int","The length (ONLY IF the first argument was an ArrayBuffer)"]
  ],
  "return" : ["JsVar","A typed array"],
  "return_object" : "ArrayBufferView"
}
Create a typed array based on the given input. Either an existing Array Buffer, an Integer as a Length, or a simple array. If an `ArrayBufferView` (eg. `Uint8Array` rather than `ArrayBuffer`) is given, it will be completely copied rather than referenced.
 */
/*JSON{
  "type" : "constructor",
  "class" : "Float64Array",
  "name" : "Float64Array",
  "generate_full" : "jswrap_typedarray_constructor(ARRAYBUFFERVIEW_FLOAT64, arr, byteOffset, length)",
  "params" : [
    ["arr","JsVar","The array or typed array to base this off, or an integer which is the array length"],
    ["byteOffset","int","The byte offset in the ArrayBuffer  (ONLY IF the first argument was an ArrayBuffer)"],
    ["length","int","The length (ONLY IF the first argument was an ArrayBuffer)"]
  ],
  "return" : ["JsVar","A typed array"],
  "return_object" : "ArrayBufferView"
}
Create a typed array based on the given input. Either an existing Array Buffer, an Integer as a Length, or a simple array. If an `ArrayBufferView` (eg. `Uint8Array` rather than `ArrayBuffer`) is given, it will be completely copied rather than referenced.
 */

JsVar *jswrap_typedarray_constructor(JsVarDataArrayBufferViewType type, JsVar *arr, JsVarInt byteOffset, JsVarInt length) {
  JsVar *arrayBuffer = 0;
  // Only allow use of byteOffset/length if we're passing an ArrayBuffer - NOT A VIEW.
  bool copyData = false;
  if (jsvIsArrayBuffer(arr) && arr->varData.arraybuffer.type==ARRAYBUFFERVIEW_ARRAYBUFFER) {
    arrayBuffer = jsvLockAgain(arr);
  } else if (jsvIsNumeric(arr)) {
    length = jsvGetInteger(arr);
    byteOffset = 0;
    arrayBuffer = jswrap_arraybuffer_constructor((int)JSV_ARRAYBUFFER_GET_SIZE(type)*length);
  } else if (jsvIsArray(arr) || jsvIsArrayBuffer(arr)) {
    length = (JsVarInt)jsvGetLength(arr);
    byteOffset = 0;
    arrayBuffer = jswrap_arraybuffer_constructor((int)JSV_ARRAYBUFFER_GET_SIZE(type)*length);
    copyData = true; // so later on we'll populate this
  }
  if (!arrayBuffer) {
    jsExceptionHere(JSET_ERROR, "Unsupported first argument of type %t\n", arr);
    return 0;
  }
  if (length==0) {
    length = ((JsVarInt)jsvGetArrayBufferLength(arrayBuffer)-byteOffset) / (JsVarInt)JSV_ARRAYBUFFER_GET_SIZE(type); 
    if (length<0) length=0;
  }
  JsVar *typedArr = jsvNewWithFlags(JSV_ARRAYBUFFER);
  if (typedArr) {
    typedArr->varData.arraybuffer.type = type;
    typedArr->varData.arraybuffer.byteOffset = (unsigned short)byteOffset;
    typedArr->varData.arraybuffer.length = (unsigned short)length;
    jsvSetFirstChild(typedArr, jsvGetRef(jsvRef(arrayBuffer)));

    if (copyData) {
      // if we were given an array, populate this ArrayBuffer
      JsvIterator it;
      jsvIteratorNew(&it, arr, JSIF_DEFINED_ARRAY_ElEMENTS);
      while (jsvIteratorHasElement(&it)) {
        JsVar *idx = jsvIteratorGetKey(&it);
        if (jsvIsInt(idx)) {
          JsVar *val = jsvIteratorGetValue(&it);
          // TODO: This is horrible! We need to try and iterate properly...
          jsvArrayBufferSet(typedArr, (size_t)jsvGetInteger(idx), val);
          jsvUnLock(val);
        }
        jsvUnLock(idx);
        jsvIteratorNext(&it);
      }
      jsvIteratorFree(&it);
    }
  }
  jsvUnLock(arrayBuffer);
  return typedArr;
}


/*JSON{
  "type" : "property",
  "class" : "ArrayBufferView",
  "name" : "buffer",
  "generate_full" : "jsvLock(jsvGetFirstChild(parent))",
  "return" : ["JsVar","An ArrayBuffer object"]
}
The buffer this view references
 */
/*JSON{
  "type" : "property",
  "class" : "ArrayBufferView",
  "name" : "byteLength",
  "generate_full" : "(JsVarInt)(parent->varData.arraybuffer.length * JSV_ARRAYBUFFER_GET_SIZE(parent->varData.arraybuffer.type))",
  "return" : ["int","The Length"]
}
The length, in bytes, of the `ArrayBufferView`
 */
/*JSON{
  "type" : "property",
  "class" : "ArrayBufferView",
  "name" : "byteOffset",
  "generate_full" : "parent->varData.arraybuffer.byteOffset",
  "return" : ["int","The byte Offset"]
}
The offset, in bytes, to the first byte of the view within the backing `ArrayBuffer`
 */

/*JSON{
  "type" : "method",
  "class" : "ArrayBufferView",
  "name" : "set",
  "generate" : "jswrap_arraybufferview_set",
  "params" : [
    ["arr","JsVar","Floating point index to access"],
    ["offset","int32","The offset in this array at which to write the values (optional)"]
  ]
}
Copy the contents of `array` into this one, mapping `this[x+offset]=array[x];`
 */
void jswrap_arraybufferview_set(JsVar *parent, JsVar *arr, int offset) {
  if (!(jsvIsString(arr) || jsvIsArray(arr) || jsvIsArrayBuffer(arr))) {
    jsExceptionHere(JSET_ERROR, "Expecting first argument to be an array, not %t", arr);
    return;
  }
  JsvIterator itsrc;
  jsvIteratorNew(&itsrc, arr, JSIF_EVERY_ARRAY_ELEMENT);
  JsvArrayBufferIterator itdst;
  jsvArrayBufferIteratorNew(&itdst, parent, (size_t)offset);

  bool useInts = !JSV_ARRAYBUFFER_IS_FLOAT(itdst.type) || jsvIsString(arr);

  while (jsvIteratorHasElement(&itsrc) && jsvArrayBufferIteratorHasElement(&itdst)) {
    if (useInts) {
      jsvArrayBufferIteratorSetIntegerValue(&itdst, jsvIteratorGetIntegerValue(&itsrc));
    } else {
      JsVar *value = jsvIteratorGetValue(&itsrc);
      jsvArrayBufferIteratorSetValue(&itdst, value);
      jsvUnLock(value);
    }
    jsvArrayBufferIteratorNext(&itdst);
    jsvIteratorNext(&itsrc);
  }
  jsvArrayBufferIteratorFree(&itdst);
  jsvIteratorFree(&itsrc);
}


// 'special' ArrayBufferView.map as it needs to return an ArrayBuffer
/*JSON{
  "type" : "method",
  "class" : "ArrayBufferView",
  "name" : "map",
  "generate" : "jswrap_arraybufferview_map",
  "params" : [
    ["function","JsVar","Function used to map one item to another"],
    ["thisArg","JsVar","if specified, the function is called with 'this' set to thisArg (optional)"]
  ],
  "return" : ["JsVar","An array containing the results"],
  "return_object" : "ArrayBufferView"
}
Return an array which is made from the following: ```A.map(function) = [function(A[0]), function(A[1]), ...]```

 **Note:** This returns an `ArrayBuffer` of the same type it was called on. To get an `Array`, use `Array.map`, eg. `[].map.call(myArray, x=>x+1)`
 */
JsVar *jswrap_arraybufferview_map(JsVar *parent, JsVar *funcVar, JsVar *thisVar) {
  if (!jsvIsArrayBuffer(parent)) {
    jsExceptionHere(JSET_ERROR, "ArrayBufferView.map can only be called on an ArrayBufferView");
    return 0;
  }
  if (!jsvIsFunction(funcVar)) {
    jsExceptionHere(JSET_ERROR, "ArrayBufferView.map's first argument should be a function");
    return 0;
  }
  if (!jsvIsUndefined(thisVar) && !jsvIsObject(thisVar)) {
    jsExceptionHere(JSET_ERROR, "ArrayBufferView.map's second argument should be undefined, or an object");
    return 0;
  }

  // create ArrayBuffer result
  JsVarDataArrayBufferViewType arrayBufferType = parent->varData.arraybuffer.type;
  JsVar *array = jsvNewTypedArray(arrayBufferType, (JsVarInt)jsvGetArrayBufferLength(parent));
  if (!array) return 0;

  // now iterate
  JsvIterator it; // TODO: if we really are limited to ArrayBuffers, this could be an ArrayBufferIterator.
  jsvIteratorNew(&it, parent, JSIF_EVERY_ARRAY_ELEMENT);
  JsvArrayBufferIterator itdst;
  jsvArrayBufferIteratorNew(&itdst, array, 0);

  while (jsvIteratorHasElement(&it)) {
    JsVar *index = jsvIteratorGetKey(&it);
    if (jsvIsInt(index)) {
      JsVarInt idxValue = jsvGetInteger(index);

      JsVar *args[3], *mapped;
      args[0] = jsvIteratorGetValue(&it);
      args[1] = jsvNewFromInteger(idxValue); // child is a variable name, create a new variable for the index
      args[2] = parent;
      mapped = jspeFunctionCall(funcVar, 0, thisVar, false, 3, args);
      jsvUnLockMany(2,args);
      if (mapped) {
        jsvArrayBufferIteratorSetValue(&itdst, mapped);
        jsvUnLock(mapped);
      }
    }
    jsvUnLock(index);
    jsvIteratorNext(&it);
    jsvArrayBufferIteratorNext(&itdst);
  }
  jsvIteratorFree(&it);
  jsvArrayBufferIteratorFree(&itdst);

  return array;
}


// -----------------------------------------------------------------------------------------------------
//                                                                      Steal Array's methods for this
// -----------------------------------------------------------------------------------------------------

/*JSON{
  "type" : "method",
  "class" : "ArrayBufferView",
  "name" : "indexOf",
  "generate" : "jswrap_array_indexOf",
  "params" : [
    ["value","JsVar","The value to check for"],
    ["startIndex","int","(optional) the index to search from, or 0 if not specified"]
  ],
  "return" : ["JsVar","the index of the value in the array, or -1"]
}
Return the index of the value in the array, or `-1`
 */
/*JSON{
  "type" : "method",
  "class" : "ArrayBufferView",
  "name" : "includes",
  "ifndef" : "SAVE_ON_FLASH",
  "generate" : "jswrap_array_includes",
  "params" : [
    ["value","JsVar","The value to check for"],
    ["startIndex","int","(optional) the index to search from, or 0 if not specified"]
  ],
  "return" : ["bool","`true` if the array includes the value, `false` otherwise"]
}
Return `true` if the array includes the value, `false` otherwise
 */
/*JSON{
  "type" : "method",
  "class" : "ArrayBufferView",
  "name" : "join",
  "generate" : "jswrap_array_join",
  "params" : [
    ["separator","JsVar","The separator"]
  ],
  "return" : ["JsVar","A String representing the Joined array"]
}
Join all elements of this array together into one string, using 'separator' between them. eg. ```[1,2,3].join(' ')=='1 2 3'```
 */
/*JSON{
  "type" : "method",
  "class" : "ArrayBufferView",
  "name" : "sort",
  "ifndef" : "SAVE_ON_FLASH",
  "generate" : "jswrap_array_sort",
  "params" : [
    ["var","JsVar","A function to use to compare array elements (or undefined)"]
  ],
  "return" : ["JsVar","This array object"],
  "return_object" : "ArrayBufferView"
}
Do an in-place quicksort of the array
 */
/*JSON{
  "type" : "method",
  "class" : "ArrayBufferView",
  "name" : "forEach",
  "generate" : "jswrap_array_forEach",
  "params" : [
    ["function","JsVar","Function to be executed"],
    ["thisArg","JsVar","if specified, the function is called with 'this' set to thisArg (optional)"]
  ]
}
Executes a provided function once per array element.
 */
/*JSON{
  "type" : "method",
  "class" : "ArrayBufferView",
  "name" : "reduce",
  "ifndef" : "SAVE_ON_FLASH",
  "generate" : "jswrap_array_reduce",
  "params" : [
    ["callback","JsVar","Function used to reduce the array"],
    ["initialValue","JsVar","if specified, the initial value to pass to the function"]
  ],
  "return" : ["JsVar","The value returned by the last function called"]
}
Execute `previousValue=initialValue` and then `previousValue = callback(previousValue, currentValue, index, array)` for each element in the array, and finally return previousValue.
 */
/*JSON{
  "type" : "method",
  "class" : "ArrayBufferView",
  "name" : "fill",
  "ifndef" : "SAVE_ON_FLASH",
  "generate" : "jswrap_array_fill",
  "params" : [
    ["value","JsVar","The value to fill the array with"],
    ["start","int","Optional. The index to start from (or 0). If start is negative, it is treated as length+start where length is the length of the array"],
    ["end","JsVar","Optional. The index to end at (or the array length). If end is negative, it is treated as length+end."]
  ],
  "return" : ["JsVar","This array"],
  "return_object" : "ArrayBufferView"
}
Fill this array with the given value, for every index `>= start` and `< end`
 */
/*JSON{
  "type" : "method",
  "class" : "ArrayBufferView",
  "name" : "filter",
  "ifndef" : "SAVE_ON_FLASH",
  "generate" : "jswrap_array_filter",
  "params" : [
    ["function","JsVar","Function to be executed"],
    ["thisArg","JsVar","if specified, the function is called with 'this' set to thisArg (optional)"]
  ],
  "return" : ["JsVar","An array containing the results"]
}
Return an array which contains only those elements for which the callback function returns 'true'
 */
/*JSON{
  "type" : "method",
  "class" : "ArrayBufferView",
  "name" : "find",
  "ifndef" : "SAVE_ON_FLASH",
  "generate" : "jswrap_array_find",
  "params" : [
    ["function","JsVar","Function to be executed"]
  ],
  "return" : ["JsVar","The array element where `function` returns `true`, or `undefined`"]
}
Return the array element where `function` returns `true`, or `undefined` if it doesn't returns `true` for any element.
 */
/*JSON{
  "type" : "method",
  "class" : "ArrayBufferView",
  "name" : "findIndex",
  "ifndef" : "SAVE_ON_FLASH",
  "generate" : "jswrap_array_findIndex",
  "params" : [
    ["function","JsVar","Function to be executed"]
  ],
  "return" : ["JsVar","The array element's index where `function` returns `true`, or `-1`"]
}
Return the array element's index where `function` returns `true`, or `-1` if it doesn't returns `true` for any element.
 */
/*JSON{
  "type" : "method",
  "class" : "ArrayBufferView",
  "name" : "reverse",
  "ifndef" : "SAVE_ON_FLASH",
  "generate" : "jswrap_array_reverse",
  "return" : ["JsVar","This array"],
  "return_object" : "ArrayBufferView"
}
Reverse the contents of this `ArrayBufferView` in-place
 */
/*JSON{
  "type" : "method",
  "class" : "ArrayBufferView",
  "name" : "slice",
  "ifndef" : "SAVE_ON_FLASH",
  "generate" : "jswrap_array_slice",
  "params" : [
    ["start","int","Start index"],
    ["end","JsVar","End index (optional)"]
  ],
  "return" : ["JsVar","A new array"],
  "return_object" : "Array"
}
Return a copy of a portion of this array (in a new array).

 **Note:** This currently returns a normal `Array`, not an `ArrayBuffer`
 */
