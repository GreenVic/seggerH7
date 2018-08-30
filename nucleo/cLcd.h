#pragma once
//{{{  includes
#include "cmsis_os.h"
#include "semphr.h"

#include "../common/utils.h"
#include "../common/cPointRect.h"
#include <map>
#include <vector>

#include "../system/stm32h7xx.h"
#include "heap.h"

#include <ft2build.h>
#include FT_FREETYPE_H
//}}}
//{{{  colour defines
#define COL_BLACK         sRgba565 (0,0,0)
#define COL_GREY          sRgba565 (128,128,128)
#define COL_WHITE         sRgba565 (255,255,255)

#define COL_BLUE          sRgba565 (0,0,255)
#define COL_GREEN         sRgba565 (0,255,0)
#define COL_RED           sRgba565 (255,0,0)

#define COL_CYAN          sRgba565 (0,255,255)
#define COL_MAGENTA       sRgba565 (255,0,255)
#define COL_YELLOW        sRgba565 (255,255,0)
//}}}
//{{{  screen resolution defines
#ifdef NEXXY_SCREEN
  // NEXXY 7 inch
  #define LCD_WIDTH         800
  #define LCD_HEIGHT       1280

  #define BOX_HEIGHT         30
  #define SMALL_FONT_HEIGHT  12
  #define FONT_HEIGHT        26
  #define BIG_FONT_HEIGHT    40

#else
  // ASUS eee 10 inch
  #define LCD_WIDTH        1024  // min 39Mhz typ 45Mhz max 51.42Mhz
  #define LCD_HEIGHT        600

  #define BOX_HEIGHT         20
  #define SMALL_FONT_HEIGHT  10
  #define FONT_HEIGHT        16
  #define BIG_FONT_HEIGHT    32
#endif
//}}}

//{{{
struct sRgba565 {
  sRgba565 (uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) :
    rgb565 (((r >> 3) << 11) | ((g >> 2) << 5) | (b>> 3)), alpha(a) {}

  uint8_t getR() { return (rgb565 >> 11) << 3; }
  uint8_t getG() { return (rgb565 & 0x07E0) >> 3; }
  uint8_t getB() { return (rgb565 & 0x001F) << 3; }
  uint8_t getA() { return alpha; }

  uint16_t rgb565;
  uint8_t alpha;
  };
//}}}
//{{{
class cTile {
public:
  enum eFormat { eRgb565, eRgb888, eYuvMcu422 };

  cTile() {};
  cTile (uint8_t* piccy, eFormat format, uint16_t pitch, uint16_t x, uint16_t y, uint16_t width, uint16_t height)
     : mPiccy(piccy), mFormat(format), mPitch(pitch), mX(x), mY(y), mWidth(width), mHeight(height) {}

  ~cTile () {
    sdRamFree (mPiccy);
    mPiccy = nullptr;
    };

  void* operator new (std::size_t size) { return pvPortMalloc (size); }
  void operator delete (void* ptr) { vPortFree (ptr); }

  uint8_t* mPiccy = nullptr;
  uint16_t mComponents = 0;
  uint16_t mPitch = 0;
  uint16_t mX = 0;
  uint16_t mY = 0;
  uint16_t mWidth = 0;
  uint16_t mHeight = 0;
  eFormat mFormat = eRgb565;
  };
//}}}
//{{{
class cFontChar {
public:
  void* operator new (std::size_t size) { return pvPortMalloc (size); }
  void operator delete (void* ptr) { vPortFree (ptr); }

  uint8_t* bitmap;
  int16_t left;
  int16_t top;
  int16_t pitch;
  int16_t rows;
  int16_t advance;
  };
//}}}

class cScanLine;
class cLcd {
public:
  enum eDma2dWait { eWaitNone, eWaitDone, eWaitIrq };
  cLcd();
  ~cLcd();
  void* operator new (std::size_t size) { return pvPortMalloc (size); }
  void operator delete (void* ptr) { vPortFree (ptr); }

  void init (const std::string& title);
  static uint16_t getWidth() { return LCD_WIDTH; }
  static uint16_t getHeight() { return LCD_HEIGHT; }
  static cPoint getSize() { return cPoint (getWidth(), getHeight()); }

  static uint16_t getBoxHeight() { return BOX_HEIGHT; }
  static uint16_t getSmallFontHeight() { return SMALL_FONT_HEIGHT; }
  static uint16_t getFontHeight() { return FONT_HEIGHT; }
  static uint16_t getBigFontHeight() { return BIG_FONT_HEIGHT; }

  uint16_t getBrightness() { return mBrightness; }

  void setShowInfo (bool show);
  void setTitle (const std::string& str) { mTitle = str; mChanged = true; }
  void change() { mChanged = true; }
  //{{{
  bool isChanged() {
    bool wasChanged = mChanged;
    mChanged = false;
    return wasChanged;
    }
  //}}}

  void info (sRgba565 colour, const std::string& str);
  void info (const std::string& str) { info (COL_WHITE, str); }

  void clear (sRgba565 colour);
  void rect (sRgba565 colour, const cRect& r);
  void rectClipped (sRgba565 colour, cRect r);
  void rectOutline (sRgba565 colour, const cRect& r, uint8_t thickness);
  void ellipse (sRgba565 colour, cPoint centre, cPoint radius);
  int text (sRgba565 colour, uint16_t fontHeight, const std::string& str, cRect r);

  void copy (cTile* tile, cPoint p);
  void copy90 (cTile* tile, cPoint p);
  void size (cTile* tile, const cRect& r);

  inline void pixel (sRgba565 colour, cPoint p) { *(mBuffer[mDrawBuffer] + p.y * getWidth() + p.x) = colour.rgb565; }
  void grad (sRgba565 colTL, sRgba565 colTR, sRgba565 colBL, sRgba565 colBR, const cRect& r);
  void line (sRgba565 colour, cPoint p1, cPoint p2);
  void ellipseOutline (sRgba565 colour, cPoint centre, cPoint radius);

  // agg anti aliased
  void aMoveTo (const cPointF& p);
  void aLineTo (const cPointF& p);
  void aWideLine (const cPointF& p1, const cPointF& p2, float width);
  void aPointedLine (const cPointF& p1, const cPointF& p2, float width);
  void aEllipseOutline (const cPointF& centre, const cPointF& radius, float width, int steps);
  void aEllipse (const cPointF& centre, const cPointF& radius, int steps);
  void aRender (sRgba565 colour, bool fillNonZero = true);

  void start();
  void drawInfo();
  void present();

  void display (int brightness);

  static cLcd* mLcd;

private:
  void ready();

  void ltdcInit (uint16_t* frameBufferAddress);
  cFontChar* loadChar (uint16_t fontHeight, char ch);

  void reset();

  uint8_t calcAlpha (int area, bool fillNonZero) const;
  void renderScanLine (cScanLine* scanLine, sRgba565 colour);

  //{{{  vars
  LTDC_HandleTypeDef mLtdcHandle;
  TIM_HandleTypeDef mTimHandle;
  int mBrightness = 50;

  bool mChanged = true;
  bool mDrawBuffer = false;
  uint16_t* mBuffer[2] = {nullptr, nullptr};

  uint32_t mBaseTime = 0;
  uint32_t mStartTime = 0;
  uint32_t mDrawTime = 0;
  uint32_t mWaitTime = 0;
  uint32_t mNumPresents = 0;

  // truetype
  std::map<uint16_t, cFontChar*> mFontCharMap;

  FT_Library FTlibrary;
  FT_Face FTface;
  FT_GlyphSlot FTglyphSlot;

  // info
  bool mShowInfo = true;
  std::string mTitle;

  // log
  static const int kMaxLines = 40;
  //{{{
  class cLine {
  public:
    cLine() {}
    ~cLine() {}

    //{{{
    void clear() {
      mTime = 0;
      mColour = COL_WHITE;
      mString = "";
      }
    //}}}

    int mTime = 0;
    sRgba565 mColour = COL_WHITE;
    std::string mString;
    };
  //}}}
  cLine mLines[kMaxLines];
  int mCurLine = 0;
  //}}}
  };
