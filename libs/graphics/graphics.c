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
 * Graphics Draw Functions
 * ----------------------------------------------------------------------------
 */

#include "graphics.h"
#include "bitmap_font_4x6.h"


#ifndef NO_VECTOR_FONT
#include "vector_font.h"
#endif
#include "jsutils.h"
#include "jsvar.h"
#include "jsparse.h"

#include "lcd_arraybuffer.h"
#include "lcd_js.h"
#ifdef USE_LCD_SDL
#include "lcd_sdl.h"
#endif
#ifdef USE_LCD_FSMC
#include "lcd_fsmc.h"
#endif
#ifdef USE_LCD_SPI
#include "lcd_spilcd.h"
#endif
#ifdef USE_LCD_ST7789_8BIT
#include "lcd_st7789_8bit.h"
#endif

// ----------------------------------------------------------------------------------------------

static void graphicsSetPixelDevice(JsGraphics *gfx, int x, int y, unsigned int col);

void graphicsFallbackSetPixel(JsGraphics *gfx, int x, int y, unsigned int col) {
  NOT_USED(gfx);
  NOT_USED(x);
  NOT_USED(y);
  NOT_USED(col);
}

unsigned int graphicsFallbackGetPixel(JsGraphics *gfx, int x, int y) {
  NOT_USED(gfx);
  NOT_USED(x);
  NOT_USED(y);
  return 0;
}

void graphicsFallbackFillRect(JsGraphics *gfx, int x1, int y1, int x2, int y2, unsigned int col) {
  int x,y;
  for (y=y1;y<=y2;y++)
    for (x=x1;x<=x2;x++)
      graphicsSetPixelDevice(gfx,x,y, col);
}

void graphicsFallbackScrollX(JsGraphics *gfx, int xdir, int yfrom, int yto) {
  int x;
  if (xdir<=0) {
    int w = gfx->data.width+xdir;
    for (x=0;x<w;x++)
      gfx->setPixel(gfx, (int)x,(int)yto,
          gfx->getPixel(gfx, (int)(x-xdir),(int)yfrom));
  } else { // >0
    for (x=gfx->data.width-xdir-1;x>=0;x--)
      gfx->setPixel(gfx, (int)(x+xdir),(int)yto,
          gfx->getPixel(gfx, (int)x,(int)yfrom));
  }
}

void graphicsFallbackScroll(JsGraphics *gfx, int xdir, int ydir) {
  if (xdir==0 && ydir==0) return;
  int y;
  if (ydir<=0) {
    int h = gfx->data.height+ydir;
    for (y=0;y<h;y++)
      graphicsFallbackScrollX(gfx, xdir, y-ydir, y);
  } else { // >0
    for (y=gfx->data.height-ydir-1;y>=0;y--)
      graphicsFallbackScrollX(gfx, xdir, y, y+ydir);
  }
#ifndef SAVE_ON_FLASH
  gfx->data.modMinX=0;
  gfx->data.modMinY=0;
  gfx->data.modMaxX=(short)(gfx->data.width-1);
  gfx->data.modMaxY=(short)(gfx->data.height-1);
#endif
}

// ----------------------------------------------------------------------------------------------

void graphicsStructResetState(JsGraphics *gfx) {
  gfx->data.fgColor = 0xFFFFFFFF;
  gfx->data.bgColor = 0;
  gfx->data.fontSize = 1+JSGRAPHICS_FONTSIZE_4X6;
#ifndef SAVE_ON_FLASH
  gfx->data.fontAlignX = 3;
  gfx->data.fontAlignY = 3;
  gfx->data.fontRotate = 0;
  gfx->data.clipRect.x1 = 0;
  gfx->data.clipRect.y1 = 0;
  gfx->data.clipRect.x2 = (unsigned short)(gfx->data.width-1);
  gfx->data.clipRect.y2 = (unsigned short)(gfx->data.height-1);
#endif
  gfx->data.cursorX = 0;
  gfx->data.cursorY = 0;
}

void graphicsStructInit(JsGraphics *gfx, int width, int height, int bpp) {
  // type/width/height/bpp should be set elsewhere...
  gfx->data.flags = JSGRAPHICSFLAGS_NONE;
  gfx->data.width = (unsigned short)width;
  gfx->data.height = (unsigned short)height;
  gfx->data.bpp = (unsigned char)bpp;
  graphicsStructResetState(gfx);
#ifndef SAVE_ON_FLASH
  gfx->data.modMaxX = -32768;
  gfx->data.modMaxY = -32768;
  gfx->data.modMinX = 32767;
  gfx->data.modMinY = 32767;
#endif

}

bool graphicsGetFromVar(JsGraphics *gfx, JsVar *parent) {
  gfx->graphicsVar = parent;
  JsVar *data = jsvObjectGetChild(parent, JS_HIDDEN_CHAR_STR"gfx", 0);
  assert(data);
  if (data) {
    jsvGetString(data, (char*)&gfx->data, sizeof(JsGraphicsData)+1/*trailing zero*/);
    jsvUnLock(data);
    gfx->setPixel = graphicsFallbackSetPixel;
    gfx->getPixel = graphicsFallbackGetPixel;
    gfx->fillRect = graphicsFallbackFillRect;
    gfx->scroll = graphicsFallbackScroll;
#ifdef USE_LCD_SDL
    if (gfx->data.type == JSGRAPHICSTYPE_SDL) {
      lcdSetCallbacks_SDL(gfx);
    } else
#endif
#ifdef USE_LCD_FSMC
    if (gfx->data.type == JSGRAPHICSTYPE_FSMC) {
      lcdSetCallbacks_FSMC(gfx);
    } else
#endif
    if (gfx->data.type == JSGRAPHICSTYPE_ARRAYBUFFER) {
      lcdSetCallbacks_ArrayBuffer(gfx);
#ifndef SAVE_ON_FLASH
    } else if (gfx->data.type == JSGRAPHICSTYPE_JS) {
      lcdSetCallbacks_JS(gfx);
#endif
#ifdef USE_LCD_SPI
    } else if (gfx->data.type == JSGRAPHICSTYPE_SPILCD) {
      lcdSetCallbacks_SPILCD(gfx);
#endif
#ifdef USE_LCD_ST7789_8BIT
    } else if (gfx->data.type == JSGRAPHICSTYPE_ST7789_8BIT) {
      lcdST7789_setCallbacks(gfx);
#endif
    } else {
      jsExceptionHere(JSET_INTERNALERROR, "Unknown graphics type\n");
      assert(0);
    }

    return true;
  } else
    return false;
}

void graphicsSetVar(JsGraphics *gfx) {
  JsVar *dataname = jsvFindChildFromString(gfx->graphicsVar, JS_HIDDEN_CHAR_STR"gfx", true);
  JsVar *data = jsvSkipName(dataname);
  if (!data) {
    data = jsvNewStringOfLength(sizeof(JsGraphicsData), NULL);
    jsvSetValueOfName(dataname, data);
  }
  jsvUnLock(dataname);
  assert(data);
  jsvSetString(data, (char*)&gfx->data, sizeof(JsGraphicsData));
  jsvUnLock(data);
}

/// Get the memory requires for this graphics's pixels if everything was packed as densely as possible
size_t graphicsGetMemoryRequired(const JsGraphics *gfx) {
  return (size_t)(gfx->data.width * gfx->data.height * gfx->data.bpp + 7) >> 3;
};

// ----------------------------------------------------------------------------------------------

// If graphics is flipped or rotated then the coordinates need modifying
void graphicsToDeviceCoordinates(const JsGraphics *gfx, int *x, int *y) {
  if (gfx->data.flags & JSGRAPHICSFLAGS_SWAP_XY) {
    int t = *x;
    *x = *y;
    *y = t;
  }
  if (gfx->data.flags & JSGRAPHICSFLAGS_INVERT_X) *x = (int)(gfx->data.width - (*x+1));
  if (gfx->data.flags & JSGRAPHICSFLAGS_INVERT_Y) *y = (int)(gfx->data.height - (*y+1));
}

// ----------------------------------------------------------------------------------------------

static void graphicsSetPixelDevice(JsGraphics *gfx, int x, int y, unsigned int col) {
#ifdef SAVE_ON_FLASH
  if (x<0 || y<0 || x>=gfx->data.width || y>=gfx->data.height) return;
#else
  if (x<gfx->data.clipRect.x1 ||
      y<gfx->data.clipRect.y1 ||
      x>gfx->data.clipRect.x2 ||
      y>gfx->data.clipRect.y2) return;
#endif
#ifndef SAVE_ON_FLASH
  if (x < gfx->data.modMinX) gfx->data.modMinX=(short)x;
  if (x > gfx->data.modMaxX) gfx->data.modMaxX=(short)x;
  if (y < gfx->data.modMinY) gfx->data.modMinY=(short)y;
  if (y > gfx->data.modMaxY) gfx->data.modMaxY=(short)y;
#endif
  gfx->setPixel(gfx,(int)x,(int)y,col & (unsigned int)((1L<<gfx->data.bpp)-1));
}

static unsigned int graphicsGetPixelDevice(JsGraphics *gfx, int x, int y) {
  if (x<0 || y<0 || x>=gfx->data.width || y>=gfx->data.height) return 0;
  return gfx->getPixel(gfx, x, y);
}

static void graphicsFillRectDevice(JsGraphics *gfx, int x1, int y1, int x2, int y2, unsigned int col) {
  if (x1>x2) {
    int t = x1;
    x1 = x2;
    x2 = t;
  }
  if (y1>y2) {
    int t = y1;
    y1 = y2;
    y2 = t;
  }
#ifdef SAVE_ON_FLASH
  if (x1<0) x1 = 0;
  if (y1<0) y1 = 0;
  if (x2>=gfx->data.width) x2 = gfx->data.width - 1;
  if (y2>=gfx->data.height) y2 = gfx->data.height - 1;
#else
  if (x1<gfx->data.clipRect.x1) x1 = gfx->data.clipRect.x1;
  if (y1<gfx->data.clipRect.y1) y1 = gfx->data.clipRect.y1;
  if (x2>gfx->data.clipRect.x2) x2 = gfx->data.clipRect.x2;
  if (y2>gfx->data.clipRect.y2) y2 = gfx->data.clipRect.y2;
#endif
  if (x2<x1 || y2<y1) return; // nope
#ifndef SAVE_ON_FLASH
  if (x1 < gfx->data.modMinX) gfx->data.modMinX=(short)x1;
  if (x2 > gfx->data.modMaxX) gfx->data.modMaxX=(short)x2;
  if (y1 < gfx->data.modMinY) gfx->data.modMinY=(short)y1;
  if (y2 > gfx->data.modMaxY) gfx->data.modMaxY=(short)y2;
#endif
  if (x1==x2 && y1==y2) {
    gfx->setPixel(gfx,(int)x1,(int)y1,col);
    return;
  }

  return gfx->fillRect(gfx, (int)x1, (int)y1, (int)x2, (int)y2, col);
}

// ----------------------------------------------------------------------------------------------

void graphicsSetPixel(JsGraphics *gfx, int x, int y, unsigned int col) {
  graphicsToDeviceCoordinates(gfx, &x, &y);
  graphicsSetPixelDevice(gfx, x, y, col);
}

unsigned int graphicsGetPixel(JsGraphics *gfx, int x, int y) {
  graphicsToDeviceCoordinates(gfx, &x, &y);
  return graphicsGetPixelDevice(gfx, x, y);
}

void graphicsFillRect(JsGraphics *gfx, int x1, int y1, int x2, int y2, unsigned int col) {
  graphicsToDeviceCoordinates(gfx, &x1, &y1);
  graphicsToDeviceCoordinates(gfx, &x2, &y2);
  graphicsFillRectDevice(gfx, x1, y1, x2, y2, col);
}

void graphicsClear(JsGraphics *gfx) {
  graphicsFillRectDevice(gfx,0,0,(int)(gfx->data.width-1),(int)(gfx->data.height-1), gfx->data.bgColor);
}

// ----------------------------------------------------------------------------------------------


void graphicsDrawRect(JsGraphics *gfx, int x1, int y1, int x2, int y2) {
  graphicsToDeviceCoordinates(gfx, &x1, &y1);
  graphicsToDeviceCoordinates(gfx, &x2, &y2);
  // rather than writing pixels, we use fillrect - as it is faster
  graphicsFillRectDevice(gfx,x1,y1,x2,y1,gfx->data.fgColor);
  graphicsFillRectDevice(gfx,x2,y1,x2,y2,gfx->data.fgColor);
  graphicsFillRectDevice(gfx,x1,y2,x2,y2,gfx->data.fgColor);
  graphicsFillRectDevice(gfx,x1,y2,x1,y1,gfx->data.fgColor);
}

void graphicsDrawEllipse(JsGraphics *gfx, int posX1, int posY1, int posX2, int posY2){
  graphicsToDeviceCoordinates(gfx, &posX1, &posY1);
  graphicsToDeviceCoordinates(gfx, &posX2, &posY2);

  int posX =  (posX1+posX2)/2;
  int posY =  (posY1+posY2)/2;
  int a = (posX2-posX1)/2;
  int b = (posY2-posY1)/2;
  int dx = 0;
  int dy = b;
  int a2 = a*a;
  int b2 = b*b;
  int err = b2-(2*b-1)*a2;
  int e2; 
  bool changed = false;

  do {
    changed = false;
    graphicsSetPixelDevice(gfx,posX+dx,posY+dy,gfx->data.fgColor);
    graphicsSetPixelDevice(gfx,posX-dx,posY+dy,gfx->data.fgColor);
    graphicsSetPixelDevice(gfx,posX+dx,posY-dy,gfx->data.fgColor);
    graphicsSetPixelDevice(gfx,posX-dx,posY-dy,gfx->data.fgColor);
    e2 = 2*err;
    if (e2 <  (2*dx+1)*b2) { dx++; err += (2*dx+1)*b2; changed=true; }
    if (e2 > -(2*dy-1)*a2) { dy--; err -= (2*dy-1)*a2; changed=true; }
  } while (changed && dy >= 0);

  while (dx++ < a) { /* erroneous termination in flat ellipses (b=1) */
       graphicsSetPixelDevice(gfx,posX+dx,posY,gfx->data.fgColor);
       graphicsSetPixelDevice(gfx,posX-dx,posY,gfx->data.fgColor);
  }
}

void graphicsFillEllipse(JsGraphics *gfx, int posX1, int posY1, int posX2, int posY2){
  graphicsToDeviceCoordinates(gfx, &posX1, &posY1);
  graphicsToDeviceCoordinates(gfx, &posX2, &posY2);

  int posX =  (posX1+posX2)/2;
  int posY =  (posY1+posY2)/2;
  int a = (posX2-posX1)/2;
  int b = (posY2-posY1)/2;
  int dx = 0;
  int dy = b;
  int a2 = a*a;
  int b2 = b*b;
  int err = b2-(2*b-1)*a2;
  int e2; 
  bool changed = false;

  do {
    changed = false;
    graphicsFillRectDevice(gfx,posX+dx,posY+dy,posX-dx,posY+dy,gfx->data.fgColor);
    graphicsFillRectDevice(gfx,posX+dx,posY-dy,posX-dx,posY-dy,gfx->data.fgColor);
    e2 = 2*err;
    if (e2 <  (2*dx+1)*b2) { dx++; err += (2*dx+1)*b2; changed=true; }
    if (e2 > -(2*dy-1)*a2) { dy--; err -= (2*dy-1)*a2; changed=true; }
  } while (changed && dy >= 0);

  while (dx++ < a) { /* erroneous termination in flat ellipses(b=1) */
       graphicsFillRectDevice(gfx,posX+dx,posY,posX-dx,posY,gfx->data.fgColor );
  }
}

void graphicsDrawLine(JsGraphics *gfx, int x1, int y1, int x2, int y2) {
  graphicsToDeviceCoordinates(gfx, &x1, &y1);
  graphicsToDeviceCoordinates(gfx, &x2, &y2);

  int xl = x2-x1;
  int yl = y2-y1;
  if (xl<0) xl=-xl; else if (xl==0) xl=1;
  if (yl<0) yl=-yl; else if (yl==0) yl=1;
  if (xl > yl) { // longer in X - scan in X
    if (x1>x2) {
      int t;
      t = x1; x1 = x2; x2 = t;
      t = y1; y1 = y2; y2 = t;
    }
    int pos = (y1<<8) + 128; // rounding!
    int step = ((y2-y1)<<8) / xl;
    int x;
    for (x=x1;x<=x2;x++) {
      graphicsSetPixelDevice(gfx, x, pos>>8, gfx->data.fgColor);
      pos += step;
    }
  } else {
    if (y1>y2) {
      int t;
      t = x1; x1 = x2; x2 = t;
      t = y1; y1 = y2; y2 = t;
    }
    int pos = (x1<<8) + 128; // rounding!
    int step = ((x2-x1)<<8) / yl;
    int y;
    for (y=y1;y<=y2;y++) {
      graphicsSetPixelDevice(gfx, pos>>8, y, gfx->data.fgColor);
      pos += step;
    }
  }
}



void graphicsFillPoly(JsGraphics *gfx, int points, short *vertices) {
  typedef struct {
    short x,y;
  } VertXY;
  VertXY *v = (VertXY*)vertices;

  int i,j,y;
  int miny = (int)(gfx->data.height-1);
  int maxy = 0;
  for (i=0;i<points;i++) {
    // convert into device coordinates...
    int vx = v[i].x;
    int vy = v[i].y;
    graphicsToDeviceCoordinates(gfx, &vx, &vy);
    v[i].x = (short)vx;
    v[i].y = (short)vy;
    // work out min and max
    short y = v[i].y;
    if (y<miny) miny=y;
    if (y>maxy) maxy=y;
  }
#ifndef SAVE_ON_FLASH
  if (miny < gfx->data.clipRect.y1) miny=gfx->data.clipRect.y1;
  if (maxy > gfx->data.clipRect.y2) maxy=gfx->data.clipRect.y2;
#else
  if (miny<0) miny=0;
  if (maxy>=gfx->data.height) maxy=(int)(gfx->data.height-1);
#endif

  const int MAX_CROSSES = 64;

  // for each scanline
  for (y=miny;y<=maxy;y++) {
    short cross[MAX_CROSSES];
    bool slopes[MAX_CROSSES];
    int crosscnt = 0;
    // work out all the times lines cross the scanline
    j = points-1;
    for (i=0;i<points;i++) {
      if ((y==miny && (v[i].y==y || v[j].y==y)) || // special-case top line
          (v[i].y<y && v[j].y>=y) ||
          (v[j].y<y && v[i].y>=y)) {
        if (crosscnt < MAX_CROSSES) {
          int l = v[j].y - v[i].y;
          if (l) { // don't do horiz lines - rely on the ends of the lines that join onto them
            cross[crosscnt] = (short)(v[i].x + ((y - v[i].y) * (v[j].x-v[i].x)) / l);
            slopes[crosscnt] = (l>1)?1:0;
            crosscnt++;
          }
        }
      }
      j = i;
    }

    // bubble sort
    for (i=0;i<crosscnt-1;) {
      if (cross[i]>cross[i+1]) {
        short t=cross[i];
        cross[i]=cross[i+1];
        cross[i+1]=t;
        bool ts=slopes[i];
        slopes[i]=slopes[i+1];
        slopes[i+1]=ts;
        if (i) i--;
      } else i++;
    }

    //  Fill the pixels between node pairs.
    int x = 0,s = 0;
    for (i=0;i<crosscnt;i++) {
      if (s==0) x=cross[i];
      if (slopes[i]) s++; else s--;
      if (!s || i==crosscnt-1) graphicsFillRectDevice(gfx,x,y,cross[i],y,gfx->data.fgColor);
      if (jspIsInterrupted()) break;
    }
  }
}

#ifndef NO_VECTOR_FONT
// prints character, returns width
unsigned int graphicsFillVectorChar(JsGraphics *gfx, int x1, int y1, int size, char ch) {
  // no need to modify coordinates as graphicsFillPoly does that
  if (size<0) return 0;
  if (ch<vectorFontOffset || ch-vectorFontOffset>=vectorFontCount) return 0;
  int vertOffset = 0;
  int i;
  /* compute offset (I figure a ~50 iteration FOR loop is preferable to
   * a 200 byte array) */
  int fontOffset = ch-vectorFontOffset;
  for (i=0;i<fontOffset;i++)
    vertOffset += READ_FLASH_UINT8(&vectorFonts[i].vertCount);
  VectorFontChar vector;
  vector.vertCount = READ_FLASH_UINT8(&vectorFonts[fontOffset].vertCount);
  vector.width = READ_FLASH_UINT8(&vectorFonts[fontOffset].width);
  short verts[VECTOR_FONT_MAX_POLY_SIZE*2];
  int idx=0;
  for (i=0;i<vector.vertCount;i+=2) {
    verts[idx+0] = (short)(x1 + (((READ_FLASH_UINT8(&vectorFontPolys[vertOffset+i+0])&0x7F)*size + (VECTOR_FONT_POLY_SIZE/2)) / VECTOR_FONT_POLY_SIZE));
    verts[idx+1] = (short)(y1 + (((READ_FLASH_UINT8(&vectorFontPolys[vertOffset+i+1])&0x7F)*size + (VECTOR_FONT_POLY_SIZE/2)) / VECTOR_FONT_POLY_SIZE));
    idx+=2;
    if (READ_FLASH_UINT8(&vectorFontPolys[vertOffset+i+1]) & VECTOR_FONT_POLY_SEPARATOR) {
      graphicsFillPoly(gfx,idx/2, verts);
      if (jspIsInterrupted()) break;
      idx=0;
    }
  }
  return (vector.width * (unsigned int)size)/(VECTOR_FONT_POLY_SIZE*2);
}

// returns the width of a character
unsigned int graphicsVectorCharWidth(JsGraphics *gfx, unsigned int size, char ch) {
  NOT_USED(gfx);
  if (ch<vectorFontOffset || ch-vectorFontOffset>=vectorFontCount) return 0;
  unsigned char width = READ_FLASH_UINT8(&vectorFonts[ch-vectorFontOffset].width);
  return (width * (unsigned int)size)/(VECTOR_FONT_POLY_SIZE*2);
}
#endif

/// Draw a simple 1bpp image in foreground colour
void graphicsDrawImage1bpp(JsGraphics *gfx, int x1, int y1, int width, int height, const unsigned char *pixelData) {
  int pixel = 256|*(pixelData++);
  int x,y;
  for (y=y1;y<y1+height;y++) {
    for (x=x1;x<x1+width;x++) {
      if (pixel&128) graphicsSetPixelDevice(gfx, x, y, gfx->data.fgColor);
      pixel = pixel<<1;
      if (pixel&65536) pixel = 256|*(pixelData++);
    }
  }
}

/// Scroll the graphics device (in user coords). X>0 = to right, Y >0 = down
void graphicsScroll(JsGraphics *gfx, int xdir, int ydir) {
  // Ensure we flip coordinate system if needed
  int x1 = 0, y1 = 0;
  int x2 = xdir, y2 = ydir;
  graphicsToDeviceCoordinates(gfx, &x1, &y1);
  graphicsToDeviceCoordinates(gfx, &x2, &y2);
  xdir = x2-x1;
  ydir = y2-y1;
  // do the scrolling
  gfx->scroll(gfx, xdir, ydir);
  // fill the new area
  if (xdir>0) gfx->fillRect(gfx,0,0,xdir-1,gfx->data.height-1, gfx->data.bgColor);
  else if (xdir<0) gfx->fillRect(gfx,gfx->data.width+xdir,0,gfx->data.width-1,gfx->data.height-1, gfx->data.bgColor);
  if (ydir>0) gfx->fillRect(gfx,0,0,gfx->data.width-1,ydir-1, gfx->data.bgColor);
  else if (ydir<0) gfx->fillRect(gfx,0,gfx->data.height+ydir,gfx->data.width-1,gfx->data.height-1, gfx->data.bgColor);
}

static void graphicsDrawString(JsGraphics *gfx, int x1, int y1, const char *str) {
  // no need to modify coordinates as setPixel does that
  while (*str) {
    graphicsDrawChar4x6(gfx,x1,y1,*(str++),1,false);
    x1 = (int)(x1 + 4);
  }
}

// Splash screen
void graphicsSplash(JsGraphics *gfx) {
  graphicsClear(gfx);
  graphicsDrawString(gfx,0,0,"Espruino "JS_VERSION);
  graphicsDrawString(gfx,0,6,"  Embedded JavaScript");
  graphicsDrawString(gfx,0,12,"  www.espruino.com");
}

void graphicsIdle() {
#ifdef USE_LCD_SDL
  lcdIdle_SDL();
#endif
}

