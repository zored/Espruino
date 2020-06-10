/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2018 Gordon Williams <gw@pur3.co.uk>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * ----------------------------------------------------------------------------
 * JavaScript Flash IO functions
 * ----------------------------------------------------------------------------
 */
#include "jsflash.h"
#include "jshardware.h"
#include "jsvariterator.h"
#include "jsinteractive.h"
#include "jswrap_string.h" //jswrap_string_match

#define SAVED_CODE_BOOTCODE_RESET ".bootrst" // bootcode that runs even after reset
#define SAVED_CODE_BOOTCODE ".bootcde" // bootcode that doesn't run after reset
#define SAVED_CODE_VARIMAGE ".varimg" // Image of all JsVars written to flash

#ifdef DEBUG
#define DBG(...) jsiConsolePrintf("[Flash] "__VA_ARGS__)
#else
#define DBG(...)
#endif

#define JSF_START_ADDRESS FLASH_SAVED_CODE_START
#define JSF_END_ADDRESS (FLASH_SAVED_CODE_START+FLASH_SAVED_CODE_LENGTH)

#ifdef USE_HEATSHRINK
  #include "compress_heatshrink.h"
  #define COMPRESS heatshrink_encode
  #define DECOMPRESS heatshrink_decode
#else
  #include "compress_rle.h"
  #define COMPRESS rle_encode
  #define DECOMPRESS rle_decode
#endif

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------ Flash Storage Functionality
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

static uint32_t jsfCreateFile(JsfFileName name, uint32_t size, JsfFileFlags flags, JsfFileHeader *returnedHeader);

/// Aligns a block, pushing it along in memory until it reaches the required alignment
static uint32_t jsfAlignAddress(uint32_t addr) {
  return (addr + (JSF_ALIGNMENT-1)) & (uint32_t)~(JSF_ALIGNMENT-1);
}

JsfFileName jsfNameFromString(const char *name) {
  assert(strlen(name)<=sizeof(JsfFileName));
  char nameBuf[sizeof(JsfFileName)+1];
  memset(nameBuf,0,sizeof(nameBuf));
  strcpy(nameBuf,name);
  return *(JsfFileName*)nameBuf;
}

JsfFileName jsfNameFromVar(JsVar *name) {
  char nameBuf[sizeof(JsfFileName)+1];
  memset(nameBuf,0,sizeof(nameBuf));
  jsvGetString(name, nameBuf, sizeof(nameBuf));
  return *(JsfFileName*)nameBuf;
}

JsfFileName jsfNameFromVarAndUnLock(JsVar *name) {
  JsfFileName n = jsfNameFromVar(name);
  jsvUnLock(name);
  return n;
}

/// Return the size in bytes of a file based on the header
uint32_t jsfGetFileSize(JsfFileHeader *header) {
  return (uint32_t)(header->size & 0x00FFFFFF);
}

/// Return the flags for this file based on the header
JsfFileFlags jsfGetFileFlags(JsfFileHeader *header) {
  return (JsfFileFlags)((uint32_t)header->size >> 24);
}

/** Load a file header from flash, return true if it is valid.
 * If readFullName==false, only the first 4 bytes of the name are loaded */
static bool jsfGetFileHeader(uint32_t addr, JsfFileHeader *header, bool readFullName) {
  assert(header);
  if (!addr) return false;
  jshFlashRead(header, addr, readFullName ? sizeof(JsfFileHeader) : 8/* size + name.firstChars */);
  return (header->size != JSF_WORD_UNSET) &&
    (addr+(uint32_t)sizeof(JsfFileHeader)+jsfGetFileSize(header) < JSF_END_ADDRESS);
}

/// Is an area of flash completely erased?
static bool jsfIsErased(uint32_t addr, uint32_t len) {
  /* Read whole blocks at the alignment size and check
   * everything (even slightly past the length) */
  unsigned char buf[128];
  assert((sizeof(buf)&(JSF_ALIGNMENT-1))==0);
  while (len) {
    uint32_t l = len;
    if (l>sizeof(buf)) l=sizeof(buf);
    jshFlashRead(&buf, addr, l);
    for (uint32_t i=0;i<l;i++)
      if (buf[i]!=0xFF) return false;
    addr += l;
    len -= l;
  }
  return true;
}

/// Is an area of flash equal to something that's in RAM?
static bool jsfIsEqual(uint32_t addr, const unsigned char *data, uint32_t len) {
  unsigned char buf[128];
  assert((sizeof(buf)&(JSF_ALIGNMENT-1))==0);
  uint32_t x=0;
  while (len) {
    uint32_t l = len;
    if (l>sizeof(buf)) l=sizeof(buf);
    jshFlashRead(&buf, addr+x, l);
    if (memcmp(buf, &data[x], l)) return false;
    x += l;
    len -= l;
  }
  return true;
}

/// Erase the entire contents of the memory store
static bool jsfEraseFrom(uint32_t startAddr) {
  uint32_t addr, len;
  if (!jshFlashGetPage(startAddr, &addr, &len))
    return false;
  while (addr<JSF_END_ADDRESS && !jspIsInterrupted()) {
    if (!jsfIsErased(addr,len))
      jshFlashErasePage(addr);
    if (!jshFlashGetPage(addr+len, &addr, &len))
      return true;
    // Erasing can take a while, so kick the watchdog throughout
    jshKickWatchDog();
  }
  return !jspIsInterrupted();
}

/// Erase the entire contents of the memory store
bool jsfEraseAll() {
  DBG("EraseAll\n");
  return jsfEraseFrom(JSF_START_ADDRESS);
}

/// When a file is found in memory, erase it (by setting first bytes of name to 0). addr=ptr to data, NOT header
static void jsfEraseFileInternal(uint32_t addr, JsfFileHeader *header) {
  DBG("EraseFile 0x%08x\n", addr);

  addr -= (uint32_t)sizeof(JsfFileHeader);
  addr += (uint32_t)((char*)&header->name.firstChars - (char*)header);
  header->name.firstChars = 0;
  jshFlashWrite(&header->name.firstChars,addr,(uint32_t)sizeof(header->name.firstChars));
}

bool jsfEraseFile(JsfFileName name) {
  JsfFileHeader header;
  uint32_t addr = jsfFindFile(name, &header);
  if (!addr) return false;
  jsfEraseFileInternal(addr, &header);
  return true;
}

// Get the address of the page after the current one, or 0. THE NEXT PAGE MAY HAVE A PREVIOUS PAGE'S DATA SPANNING OVER IT
static uint32_t jsfGetAddressOfNextPage(uint32_t addr) {
  uint32_t pageAddr,pageLen;
  if (!jshFlashGetPage(addr, &pageAddr, &pageLen))
    return 0;
  addr = pageAddr+pageLen;
  if (addr>=JSF_END_ADDRESS) return 0; // no pages in range
  return addr;
}

/* Get the space left for a file (including header) between the address and the next page.
 * If the next page is empty, return the space in that as well (and so on) */
static uint32_t jsfGetSpaceLeftInPage(uint32_t addr) {
  uint32_t pageAddr,pageLen;
  if (!jshFlashGetPage(addr, &pageAddr, &pageLen))
    return 0;
  uint32_t nextPageStart = pageAddr+pageLen;
  // if the next page is empty, assume it's empty until the end of flash
  JsfFileHeader header;
  if (nextPageStart<JSF_END_ADDRESS &&
      !jsfGetFileHeader(nextPageStart, &header, false)) {
    nextPageStart = JSF_END_ADDRESS;
  }
  return nextPageStart - addr;
}

typedef enum {
  GNFH_GET_EMPTY = 0,    ///< stop on an empty header even if there are pages after
  GNFH_GET_ALL   = 1,      ///< get all headers
  GNFH_READ_ONLY_FILENAME_START = 2 ///< Get size and the first 4 chars of the filename
} jsfGetNextFileHeaderType;

/** Given the address and a header, work out where the next one should be and load it.
 Both addr and header are updated. Returns true if the header is valid, false if not.
 If skipPages==true, if a header isn't valid but there's another page, jump to that.
 */
static bool jsfGetNextFileHeader(uint32_t *addr, JsfFileHeader *header, jsfGetNextFileHeaderType type) {
  assert(addr && header);
  uint32_t oldAddr = *addr;
  *addr = 0;
  // Work out roughly where the start is
  uint32_t newAddr = oldAddr + jsfGetFileSize(header) + (uint32_t)sizeof(JsfFileHeader);
  // pad out to flash write boundaries
  newAddr = jsfAlignAddress(newAddr);
  // sanity check for bad data
  if (newAddr<oldAddr) return 0; // corrupt!
  if (newAddr+sizeof(JsfFileHeader)>JSF_END_ADDRESS) return 0; // not enough space
  *addr = newAddr;
  bool valid = jsfGetFileHeader(newAddr, header, !(type&GNFH_READ_ONLY_FILENAME_START));
  if ((type&GNFH_GET_ALL) && !valid) {
    // there wasn't another header in this page - check the next page
    newAddr = jsfGetAddressOfNextPage(newAddr);
    *addr = newAddr;
    if (!newAddr) return false; // no valid address
    valid = jsfGetFileHeader(newAddr, header, !(type&GNFH_READ_ONLY_FILENAME_START));
    // we can't have a blank page and then a header, so stop our search
  }
  return valid;
}

// Get the address of the page that starts with a header (or is clear) after the current one, or 0
static uint32_t jsfGetAddressOfNextStartPage(uint32_t addr) {
  uint32_t next = jsfGetAddressOfNextPage(addr);
  if (next==0) return 0; // no next page
  JsfFileHeader header;
  if (jsfGetFileHeader(addr, &header, false)) do {
    if (addr>next) {
      next = jsfGetAddressOfNextPage(addr);
      if (next==0) return 0;
    }
    if (addr==next) return addr; // we stumbled on a header that was right on the boundary
  } while (jsfGetNextFileHeader(&addr, &header, GNFH_GET_EMPTY|GNFH_READ_ONLY_FILENAME_START));
  return next;
}

// Get the amount of space free in this page (or all pages). addr=0 uses start page
uint32_t jsfGetFreeSpace(uint32_t addr, bool allPages) {
  if (!addr) addr=JSF_START_ADDRESS;
  uint32_t pageEndAddr = JSF_END_ADDRESS;
  if (!allPages) {
    uint32_t pageAddr,pageLen;
    if (!jshFlashGetPage(addr, &pageAddr, &pageLen))
      return 0;
    assert(addr==pageAddr);
    pageEndAddr = pageAddr+pageLen;
  }
  JsfFileHeader header;
  memset(&header,0,sizeof(JsfFileHeader));
  uint32_t lastAddr = addr;
  if (jsfGetFileHeader(addr, &header, false)) do {
    lastAddr = jsfAlignAddress(addr + (uint32_t)sizeof(JsfFileHeader) + jsfGetFileSize(&header));
  } while (jsfGetNextFileHeader(&addr, &header, (allPages ? GNFH_GET_ALL : GNFH_GET_EMPTY)|GNFH_READ_ONLY_FILENAME_START));
  return pageEndAddr-lastAddr;
}

// Get the amount of space needed to mirror this page elsewhere (including padding for alignment)
static uint32_t jsfGetAllocatedSpace(uint32_t addr, bool allPages, uint32_t *uncompactedSpace) {
  uint32_t allocated = 0;
  if (uncompactedSpace) *uncompactedSpace=0;
  JsfFileHeader header;
  memset(&header,0,sizeof(JsfFileHeader));
  if (jsfGetFileHeader(addr, &header, false)) do {
    uint32_t fileSize = jsfAlignAddress(jsfGetFileSize(&header)) + (uint32_t)sizeof(JsfFileHeader);
    if (header.name.firstChars != 0) { // if not replaced
      allocated += fileSize;
    } else { // replaced
      if (uncompactedSpace) *uncompactedSpace += fileSize;
    }
  } while (jsfGetNextFileHeader(&addr, &header, (allPages ? GNFH_GET_ALL : GNFH_GET_EMPTY)|GNFH_READ_ONLY_FILENAME_START));
  return allocated;
}

/* Try and compact saved data so it'll fit in Flash again.
 * TO RUN THIS, 'ALLOCATED' MUST BE CORRECT, AND THERE MUST BE ENOUGH STACK FREE
 */
static bool jsfCompactInternal(uint32_t startAddress, uint32_t allocated) {
  DBG("Compacting from 0x%08x\n", startAddress);
  if (allocated>0) {
    // allocated = amount of RAM needed to compact all pages
    // All our allocated data will fit in RAM - awesome!
    // Allocate RAM
    char *swapBuffer = alloca(allocated);
    char *swapBufferPtr = swapBuffer;
    // Copy all data to swap
    DBG("Compacting - copy data to swap\n");
    JsfFileHeader header;
    memset(&header,0,sizeof(JsfFileHeader));
    uint32_t addr = startAddress;
    if (jsfGetFileHeader(addr, &header, true)) do {
      if (header.name.firstChars != 0) { // if not replaced
        memcpy(swapBufferPtr, &header, sizeof(JsfFileHeader));
        swapBufferPtr += sizeof(JsfFileHeader);
        uint32_t alignedSize = jsfAlignAddress(jsfGetFileSize(&header));
        jshFlashRead(swapBufferPtr, addr+(uint32_t)sizeof(JsfFileHeader), alignedSize);
        memset(&swapBufferPtr[jsfGetFileSize(&header)], 0xFF, alignedSize-jsfGetFileSize(&header));
        swapBufferPtr += alignedSize;
      }
    } while (jsfGetNextFileHeader(&addr, &header, GNFH_GET_ALL));
    // Erase everything
    DBG("Compacting - erasing pages\n");
    jsfEraseFrom(startAddress);
    // Copy data back
    DBG("Compacting - copy data from swap\n");
    char *swapBufferEnd = swapBufferPtr;
    swapBufferPtr = swapBuffer;
    while (swapBufferPtr < swapBufferEnd) {
      memcpy(&header, swapBufferPtr, sizeof(JsfFileHeader));
      swapBufferPtr += sizeof(JsfFileHeader);
      JsfFileHeader newHeader;
      uint32_t newFile = jsfCreateFile(header.name, jsfGetFileSize(&header), jsfGetFileFlags(&header), &newHeader);
      uint32_t alignedSize = jsfAlignAddress(jsfGetFileSize(&header));
      if (newFile) jshFlashWrite(swapBufferPtr, newFile, alignedSize);
      swapBufferPtr += alignedSize;
    }
  } else {
    DBG("Compacting - no data, just erasing page\n");
    jsfEraseFrom(startAddress);
  }
  DBG("Compaction Complete\n");
  return true;
}


// Try and compact saved data so it'll fit in Flash again
bool jsfCompact() {
  DBG("Compacting\n");
  uint32_t addr = JSF_START_ADDRESS;

  /* Try and compact the whole area first, but if that
   * fails, keep skipping forward pages until we have
   * enough RAM free that it's ok. */
  while (addr) {
    uint32_t uncompacted = 0;
    uint32_t allocated = jsfGetAllocatedSpace(addr, true, &uncompacted);
    if (!uncompacted) {
      DBG("Already fully compacted\n");
      return true;
    }
    if (allocated+1024 < jsuGetFreeStack()) {
      DBG("Compacting - all data fits in RAM for 0x%08x (%d bytes)\n", addr, allocated);
      return jsfCompactInternal(addr, allocated);
    } else {
      DBG("Compacting - Not enough memory for 0x%08x (%d bytes)\n", addr, allocated);
    }
    // move to the next available page and try from there
    addr = jsfGetAddressOfNextStartPage(addr);
  }
  DBG("Compacting - not enough memory to compact anything\n");
  /* TODO: we should try and compact pages at the start as well.
   * Currently we just skip pages from the front until we have
   * enough RAM. By doing both we could work either side of a
   * massive saved code area. */

  return false;
}

/// Create a new 'file' in the memory store - DOES NOT remove existing files with same name. Return the address of data start, or 0 on error
static uint32_t jsfCreateFile(JsfFileName name, uint32_t size, JsfFileFlags flags, JsfFileHeader *returnedHeader) {
  DBG("CreateFile (%d bytes)\n", size);
  uint32_t requiredSize = jsfAlignAddress(size)+(uint32_t)sizeof(JsfFileHeader);
  bool compacted = false;
  uint32_t addr = 0;
  JsfFileHeader header;
  uint32_t freeAddr = 0;
  while (!freeAddr) {
    addr = JSF_START_ADDRESS;
    freeAddr = 0;
    // Find a hole that's big enough for our file
    do {
      if (jsfGetFileHeader(addr, &header, false)) do {
      } while (jsfGetNextFileHeader(&addr, &header, GNFH_GET_EMPTY));
      // If not enough space, skip to next page
      if (jsfGetSpaceLeftInPage(addr)<requiredSize) {
        addr = jsfGetAddressOfNextPage(addr);
      } else { // if enough space, we can write a file!
        freeAddr = addr;
      }
    } while (addr && !freeAddr);
    // If we don't have space, compact
    if (!freeAddr) {
      // check this for sanity - in future we might compact forward into other pages, and don't compact if so
      if (!compacted) {
        compacted = true;
        if (!jsfCompact()) {
          DBG("CreateFile - Compact failed\n");
          return 0;
        }
        addr = JSF_START_ADDRESS; // addr->startAddr = restart
      } else {
        DBG("CreateFile - Not enough space\n");
        return 0;
      }
    }
  };
  // If we were going to straddle the next page and there's enough space,
  // push this file forwards so it starts on a clean page boundary
  addr = freeAddr;
  uint32_t spaceAvailable = jsfGetSpaceLeftInPage(addr);
  uint32_t nextPage = jsfGetAddressOfNextPage(addr);
  if (nextPage && // there is a next page
      ((nextPage - addr) < requiredSize) && // it would straddle pages
      (spaceAvailable > (size + nextPage - addr)) && // there is space
      (requiredSize < 512) && // it's not too big. We should always try and put big files as near the start as possible. See note in jsfCompact
      !jsfGetFileHeader(nextPage, &header, false)) { // the next page is free
    DBG("CreateFile straddles page boundary, pushed to next page (0x%08x -> 0x%08x)\n", addr, nextPage);
    addr = nextPage;
  }
  // write out the header
  DBG("CreateFile new 0x%08x\n", addr+(uint32_t)sizeof(JsfFileHeader));
  header.size = size | (flags<<24);
  header.name = name;
  DBG("CreateFile write header\n");
  jshFlashWrite(&header,addr,(uint32_t)sizeof(JsfFileHeader));
  DBG("CreateFile written header\n");
  if (returnedHeader) *returnedHeader = header;
  return addr+(uint32_t)sizeof(JsfFileHeader);
}

/// Find a 'file' in the memory store. Return the address of data start (and header if returnedHeader!=0). Returns 0 if not found
uint32_t jsfFindFile(JsfFileName name, JsfFileHeader *returnedHeader) {
  uint32_t addr = JSF_START_ADDRESS;
  JsfFileHeader header;
  memset(&header,0,sizeof(JsfFileHeader));
  if (jsfGetFileHeader(addr, &header, false)) do {
    // check for something with the same first 4 chars of name that hasn't been replaced.
    if (header.name.firstChars == name.firstChars) {
      // Now load the whole header (with name) and check properly
      jsfGetFileHeader(addr, &header, true);
      if (memcmp(header.name.c, name.c, sizeof(name.c))==0) {
        uint32_t endOfFile = addr + (uint32_t)sizeof(JsfFileHeader) + jsfGetFileSize(&header);
        if (endOfFile<addr || endOfFile>JSF_END_ADDRESS)
          return 0; // corrupt - file too long
        if (returnedHeader)
          *returnedHeader = header;
        return addr+(uint32_t)sizeof(JsfFileHeader);
      }
    }
  } while (jsfGetNextFileHeader(&addr, &header, GNFH_GET_ALL|GNFH_READ_ONLY_FILENAME_START)); // still only get first 4 chars of name
  return 0;
}


/// Output debug info for files stored in flash storage
void jsfDebugFiles() {
  uint32_t addr = JSF_START_ADDRESS;
  uint32_t pageAddr = 0, pageLen = 0, pageEndAddr = 0;

  jsiConsolePrintf("DEBUG FILES %d live\n", jsfGetAllocatedSpace(addr,true,0));

  JsfFileHeader header;
  memset(&header,0,sizeof(JsfFileHeader));
  if (jsfGetFileHeader(addr, &header, true)) do {
    if (addr>=pageEndAddr) {
      if (!jshFlashGetPage(addr, &pageAddr, &pageLen)) {
        jsiConsolePrintf("Page not found!\n");
        return;
      }
      pageEndAddr = pageAddr+pageLen;
      uint32_t nextStartPage = jsfGetAddressOfNextStartPage(addr);
      uint32_t alloced = jsfGetAllocatedSpace(pageAddr,false,0);
      if (nextStartPage==pageEndAddr) {
        jsiConsolePrintf("PAGE 0x%08x (%d bytes) - %d live %d free\n",
            pageAddr,pageLen,alloced,pageLen - alloced);
      } else {
        pageLen = nextStartPage-pageAddr;
        jsiConsolePrintf("PAGES 0x%08x -> 0x%08x (%d bytes) - %d live %d free\n",
            pageAddr,nextStartPage,pageLen,
            alloced,pageLen-alloced);
      }
    }

    char nameBuf[sizeof(JsfFileName)+1];
    memset(nameBuf,0,sizeof(nameBuf));
    memcpy(nameBuf,&header.name,sizeof(JsfFileName));
    jsiConsolePrintf("0x%08x\t%s\t(%d bytes)\n", addr+(uint32_t)sizeof(JsfFileHeader), nameBuf[0]?nameBuf:"DELETED", jsfGetFileSize(&header));
    // TODO: print page boundaries
  } while (jsfGetNextFileHeader(&addr, &header, GNFH_GET_ALL));
}

/** Return false if the current storage is not valid
 * or is corrupt somehow. Basically that means if
 * jsfGet[Next]FileHeader returns false but the header isn't all FF
 */
bool jsfIsStorageValid() {
  uint32_t addr = JSF_START_ADDRESS;
  JsfFileHeader header;
  unsigned char *headerPtr = (unsigned char *)&header;

  bool valid = jsfGetFileHeader(addr, &header, true);
  if (valid) while (jsfGetNextFileHeader(&addr, &header, GNFH_GET_ALL)) {};
  bool allFF = true;
  for (size_t i=0;i<sizeof(JsfFileHeader);i++)
    if (headerPtr[i]!=0xFF) allFF=false;
  return allFF;
}

JsVar *jsfReadFile(JsfFileName name, int offset, int length) {
  JsfFileHeader header;
  uint32_t addr = jsfFindFile(name, &header);
  if (!addr) return 0;
  // clip requested read lengths
  if (offset<0) offset=0;
  int fileLen = (int)jsfGetFileSize(&header);
  if (length<=0) length=fileLen;
  if (offset>fileLen) offset=fileLen;
  if (offset+length>fileLen) length=fileLen-offset;
  if (length<=0) return jsvNewFromEmptyString();
  // now increment address by offset
  addr += (uint32_t)offset;

  size_t mappedAddr = jshFlashGetMemMapAddress((size_t)addr);
#ifdef SPIFLASH_BASE // if using SPI flash it can't be memory-mapped
  if (!mappedAddr) {
    /*JsVar *v = jsvNewStringOfLength(length, NULL);
    if (v) {
      JsvStringIterator it;
      jsvStringIteratorNew(&it, v, 0);
      while (length && jsvStringIteratorHasChar(&it)) {
        unsigned char *data;
        unsigned int l = 0;
        jsvStringIteratorGetPtrAndNext(&it, &data, &l);
        jshFlashRead(data, addr, l);
        addr += l;
        length -= l;
      }
      jsvStringIteratorFree(&it);
    }
    return v;*/
    return jsvNewFlashString((char*)(size_t)addr, (size_t)length);
  }
#endif
#ifdef LINUX
  // linux fakes flash with a file, so we can't just return a pointer to it!
  uint32_t alignedSize = jsfAlignAddress((uint32_t)length);
  char *d = (char*)malloc(alignedSize);
  jshFlashRead(d, addr, alignedSize);
  JsVar *v = jsvNewStringOfLength((uint32_t)length, d);
  free(d);
  return v;
#else
  return jsvNewNativeString((char*)mappedAddr, length);
#endif
}

bool jsfWriteFile(JsfFileName name, JsVar *data, JsfFileFlags flags, JsVarInt offset, JsVarInt _size) {
  if (offset<0 || _size<0) return false;
  uint32_t size = (uint32_t)_size;
  // Data length
  JSV_GET_AS_CHAR_ARRAY(dPtr, dLen, data);
  if (!dPtr) return false;
  if (size==0) size=(uint32_t)dLen;
  // Lookup file
  JsfFileHeader header;
  uint32_t addr = jsfFindFile(name, &header);
  if ((!addr && offset==0) || // No file
      // we have a file, but it's wrong - remove it
      (addr && offset==0 && (
          flags!=jsfGetFileFlags(&header) ||
          size!=jsfGetFileSize(&header) ||
          !jsfIsErased(addr, size)))) {
    if (addr && offset==0 &&
        size==jsfGetFileSize(&header) &&
        flags==jsfGetFileFlags(&header) &&
        dLen==size && // setting all in one go
        jsfIsEqual(addr, (unsigned char*)dPtr, (uint32_t)dLen)) {
      DBG("jsfWriteFile files Equal\n");
      return true;
    }
    if (addr) { // file exists, remove it!
      DBG("jsfWriteFile remove existing file\n");
      jsfEraseFileInternal(addr, &header);
    }
    DBG("jsfWriteFile create file\n");
    addr = jsfCreateFile(name, (uint32_t)size, flags, &header);
  }
  if (!addr) {
    jsExceptionHere(JSET_ERROR, "Unable to find or create file");
    return false;
  }
  if ((uint32_t)offset+(uint32_t)dLen > jsfGetFileSize(&header)) {
    jsExceptionHere(JSET_ERROR, "Too much data for file size");
    return false;
  }
  addr += (uint32_t)offset;
  if (!jsfIsErased(addr, (uint32_t)dLen)) {
    jsExceptionHere(JSET_ERROR, "File already written with different data");
    return false;
  }
  DBG("jsfWriteFile write contents\n");
  jshFlashWriteAligned(dPtr, addr, (uint32_t)dLen);
  DBG("jsfWriteFile written contents\n");
  return true;
}

/// Return all files in flash as a JsVar array of names. If regex is supplied, it is used to filter the filenames using String.match(regexp)
JsVar *jsfListFiles(JsVar *regex) {
  JsVar *files = jsvNewEmptyArray();
  if (!files) return 0;

  char nameBuf[sizeof(JsfFileName)+1];
  uint32_t addr = JSF_START_ADDRESS;
  JsfFileHeader header;
  memset(&header,0,sizeof(JsfFileHeader));
  if (jsfGetFileHeader(addr, &header, true)) do {
    if (header.name.firstChars != 0) { // if not replaced
      memcpy(nameBuf, &header.name, sizeof(JsfFileName));
      nameBuf[sizeof(JsfFileName)]=0;
      JsVar *v = jsvNewFromString(nameBuf);
      bool match = true;
      if (regex) {
        JsVar *m = jswrap_string_match(v,regex);
        match = !(jsvIsUndefined(m) || jsvIsNull(m));
        jsvUnLock(m);
      }
      if (match) jsvArrayPushAndUnLock(files, v);
      else jsvUnLock(v);
    }
  } while (jsfGetNextFileHeader(&addr, &header, GNFH_GET_ALL));
  return files;
}


// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------ For loading/saving code to flash
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------

// Get a hash of the current Git commit, so new builds won't load saved code
static uint32_t getBuildHash() {
#ifdef GIT_COMMIT
  const unsigned char *s = (unsigned char*)STRINGIFY(GIT_COMMIT);
  uint32_t hash = 0;
  while (*s)
    hash = (hash<<1) ^ *(s++);
  return hash;
#else
  return 0;
#endif
}

typedef struct {
  uint32_t address;          // current address in memory
  uint32_t endAddress;       // address at which to end
  uint32_t byteCount;
  unsigned char buffer[128]; // buffer for read/written data
  uint32_t bufferCnt;        // where are we in the buffer?
} jsfcbData;
// cbdata = struct jsfcbData
void jsfSaveToFlash_writecb(unsigned char ch, uint32_t *cbdata) {
  jsfcbData *data = (jsfcbData*)cbdata;
  data->buffer[data->bufferCnt++] = ch;
  if (data->bufferCnt>=(uint32_t)sizeof(data->buffer)) {
    jshFlashWrite(data->buffer, data->address, data->bufferCnt);
    data->address += data->bufferCnt;
    data->bufferCnt = 0;
    if ((data->address&1023)==0) jsiConsolePrint(".");
  }
}
void jsfSaveToFlash_finish(jsfcbData *data) {
  // pad to alignment
  while (data->bufferCnt & (JSF_ALIGNMENT-1))
    data->buffer[data->bufferCnt++] = 0xFF;
  // write
  jshFlashWrite(data->buffer, data->address, data->bufferCnt);
}

// cbdata = struct jsfcbData
int jsfLoadFromFlash_readcb(uint32_t *cbdata) {
  jsfcbData *data = (jsfcbData*)cbdata;

  if (data->address >= data->endAddress) return -1; // at end
  if (data->byteCount==0 || data->bufferCnt>=data->byteCount) {
    data->byteCount = data->endAddress - data->address;
    if (data->byteCount > sizeof(data->buffer))
      data->byteCount = sizeof(data->buffer);
    jshFlashRead(data->buffer, data->address, data->byteCount);
    data->bufferCnt = 0;
  }
  data->address++;
  return data->buffer[data->bufferCnt++];
}

/// Save the RAM image to flash (this is the actual interpreter state)
void jsfSaveToFlash() {
  unsigned int varSize = jsvGetMemoryTotal() * (unsigned int)sizeof(JsVar);
  unsigned char* varPtr = (unsigned char *)_jsvGetAddressOf(1);

  jsiConsolePrint("Compacting Flash...\n");
  JsfFileName name = jsfNameFromString(SAVED_CODE_VARIMAGE);
  // Ensure we get rid of any saved code we had before
  jsfEraseFile(name);
  // Try and compact, just to ensure we get the maximum amount saved
  jsfCompact();
  jsiConsolePrint("Calculating Size...\n");
  // Work out how much data this'll take, plus 4 bytes for build hash
  uint32_t compressedSize = 4 + COMPRESS(varPtr, varSize, NULL, NULL);
  // How much data do we have?
  uint32_t savedCodeAddr = jsfCreateFile(name, compressedSize, JSFF_COMPRESSED, NULL);
  if (!savedCodeAddr) {
    jsiConsolePrintf("ERROR: Too big to save to flash (%d vs %d bytes)\n", compressedSize, jsfGetFreeSpace(0,true));
    jsvSoftInit();
    jspSoftInit();
    jsiConsolePrint("Deleting command history and trying again...\n");
    while (jsiFreeMoreMemory());
    jspSoftKill();
    jsvSoftKill();
    savedCodeAddr = jsfCreateFile(name, compressedSize, JSFF_COMPRESSED, NULL);
  }
  if (!savedCodeAddr) {
    if (jsfGetAllocatedSpace(JSF_START_ADDRESS, true, 0))
      jsiConsolePrint("Not enough free space to save. Try require('Storage').eraseAll()\n");
    else
      jsiConsolePrint("Code is too big to save to Flash.\n");
    return;
  }
  // Ok, we have space!
  // Now start writing
  jsfcbData cbData;
  memset(&cbData, 0, sizeof(cbData));
  cbData.address = savedCodeAddr;
  cbData.endAddress = jsfAlignAddress(savedCodeAddr+compressedSize);
  jsiConsolePrint("Writing..");
  // write the hash
  uint32_t hash = getBuildHash();
  int i;
  for (i=0;i<4;i++)
    jsfSaveToFlash_writecb(((unsigned char*)&hash)[i], (uint32_t*)&cbData);
  // write compressed data
  COMPRESS(varPtr, varSize, jsfSaveToFlash_writecb, (uint32_t*)&cbData);
  jsfSaveToFlash_finish(&cbData);
  jsiConsolePrintf("\nCompressed %d bytes to %d\n", varSize, compressedSize);
}


/// Load the RAM image from flash (this is the actual interpreter state)
void jsfLoadStateFromFlash() {
  JsfFileHeader header;
  uint32_t savedCode = jsfFindFile(jsfNameFromString(SAVED_CODE_VARIMAGE),&header);
  if (!savedCode) {
    return;
  }

  //  unsigned int dataSize = jsvGetMemoryTotal() * sizeof(JsVar);
  unsigned char* varPtr = (unsigned char *)_jsvGetAddressOf(1);

  jsfcbData cbData;
  memset(&cbData, 0, sizeof(cbData));
  cbData.address = savedCode;
  cbData.endAddress = savedCode+jsfGetFileSize(&header);

  uint32_t hash;
  int i;
  for (i=0;i<4;i++)
    ((char*)&hash)[i] = (char)jsfLoadFromFlash_readcb((uint32_t*)&cbData);
  if (hash != getBuildHash()) {
    jsiConsolePrintf("Not loading saved code from different Espruino firmware.\n");
    return;
  }
  jsiConsolePrintf("Loading %d bytes from flash...\n", jsfGetFileSize(&header));
  DECOMPRESS(jsfLoadFromFlash_readcb, (uint32_t*)&cbData, varPtr);
}

void jsfSaveBootCodeToFlash(JsVar *code, bool runAfterReset) {
  jsfEraseFile(jsfNameFromString(SAVED_CODE_BOOTCODE));
  jsfEraseFile(jsfNameFromString(SAVED_CODE_BOOTCODE_RESET));
  if (jsvIsUndefined(code) || jsvGetLength(code)==0)
    return;
  jsfWriteFile(jsfNameFromString(runAfterReset ? SAVED_CODE_BOOTCODE_RESET : SAVED_CODE_BOOTCODE), code, JSFF_NONE, 0, 0);
}

JsVar *jsfGetBootCodeFromFlash(bool isReset) {
  JsVar *resetCode = jsfReadFile(jsfNameFromString(SAVED_CODE_BOOTCODE_RESET),0,0);
  if (isReset || resetCode) return resetCode;
  return jsfReadFile(jsfNameFromString(SAVED_CODE_BOOTCODE),0,0);
}

bool jsfLoadBootCodeFromFlash(bool isReset) {
  // Load code in .boot0/1/2/3 UNLESS BTN1 is HELD DOWN
#ifdef BANGLEJS
  if (jshPinGetValue(BTN1_PININDEX)!=BTN1_ONSTATE)
#endif
  {
    char filename[7] = ".bootX";
    for (int i=0;i<4;i++) {
      filename[5] = (char)('0'+i);
      JsVar *code = jsfReadFile(jsfNameFromString(filename),0,0);
      if (code)
        jsvUnLock2(jspEvaluateVar(code,0,0), code);
    }
  }
  // Load normal boot code
  JsVar *code = jsfGetBootCodeFromFlash(isReset);
  if (!code) return false;
  jsvUnLock2(jspEvaluateVar(code,0,0), code);
  return true;
}

bool jsfFlashContainsCode() {
  return
      jsfFindFile(jsfNameFromString(SAVED_CODE_VARIMAGE),0) ||
      jsfFindFile(jsfNameFromString(SAVED_CODE_BOOTCODE),0) ||
      jsfFindFile(jsfNameFromString(SAVED_CODE_BOOTCODE_RESET),0);
}

/** Completely clear any saved code from flash. */
void jsfRemoveCodeFromFlash() {
  jsiConsolePrint("Erasing saved code.");
  jsfEraseFile(jsfNameFromString(SAVED_CODE_VARIMAGE));
  jsfEraseFile(jsfNameFromString(SAVED_CODE_BOOTCODE));
  jsfEraseFile(jsfNameFromString(SAVED_CODE_BOOTCODE_RESET));
  jsiConsolePrint("\nDone!\n");
}

