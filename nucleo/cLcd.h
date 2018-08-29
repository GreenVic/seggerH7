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
#define COL_BLACK         0x0000
#define COL_GREY          0x7BEF
#define COL_WHITE         0xFFFF

#define COL_BLUE          0x001F
#define COL_GREEN         0x07E0
#define COL_RED           0xF800

#define COL_CYAN          0x07FF
#define COL_MAGENTA       0xF81F
#define COL_YELLOW        0xFFE0
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
struct sRgba {
  sRgba (uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_= 255) : r(r_), g(g_), b(b_), a(a_) {}

  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
  };
//}}}
//{{{
class cTile {
public:
  enum eFormat { eRgb565, eRgb888, eYuv422mcu };
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

//{{{
class cScanLine {
public:
  //{{{
  class iterator {
  public:
    iterator (const cScanLine& scanLine) :
      mCoverage(scanLine.mCoverage), mCurCount(scanLine.mCounts), mCurStartPtr(scanLine.mStartPtrs) {}

    int next() {
      ++mCurCount;
      ++mCurStartPtr;
      return int(*mCurStartPtr - mCoverage);
      }

    int getNumPix() const { return int(*mCurCount); }
    const uint8_t* getCoverage() const { return *mCurStartPtr; }

  private:
    const uint8_t* mCoverage;
    const uint16_t* mCurCount;
    const uint8_t* const* mCurStartPtr;
    };
  //}}}
  friend class iterator;

  cScanLine() {}
  //{{{
  ~cScanLine() {

    vPortFree (mCounts);
    vPortFree (mStartPtrs);
    vPortFree (mCoverage);
    }
  //}}}

  int16_t getY() const { return mLastY; }
  int16_t getBaseX() const { return mMinx;  }
  uint16_t getNumSpans() const { return mNumSpans; }
  int isReady (int16_t y) const { return mNumSpans && (y ^ mLastY); }

  //{{{
  void resetSpans() {

    mLastX = 0x7FFF;
    mLastY = 0x7FFF;
    mCurCount = mCounts;
    mCurStartPtr = mStartPtrs;
    mNumSpans = 0;
    }
  //}}}
  //{{{
  void reset (int16_t min_x, int16_t max_x) {

    unsigned max_len = max_x - min_x + 2;
    if (max_len > mMaxlen) {
      vPortFree (mCounts);
      vPortFree (mStartPtrs);
      vPortFree (mCoverage);
      mCoverage = (uint8_t*)pvPortMalloc (max_len);
      mStartPtrs = (uint8_t**)pvPortMalloc (max_len*4);
      mCounts = (uint16_t*)pvPortMalloc (max_len*2);
      mMaxlen = max_len;
      }

    mLastX = 0x7FFF;
    mLastY = 0x7FFF;
    mMinx = min_x;
    mCurCount = mCounts;
    mCurStartPtr = mStartPtrs;
    mNumSpans = 0;
    }
  //}}}

  //{{{
  void addSpan (int16_t x, int16_t y, uint16_t num, uint16_t coverage) {

    x -= mMinx;

    memset (mCoverage + x, coverage, num);
    if (x == mLastX+1)
      (*mCurCount) += (uint16_t)num;
    else {
      *++mCurCount = (uint16_t)num;
      *++mCurStartPtr = mCoverage + x;
      mNumSpans++;
      }

    mLastX = x + num - 1;
    mLastY = y;
    }
  //}}}

private:
  int16_t   mMinx = 0;
  uint16_t  mMaxlen = 0;
  int16_t   mLastX = 0x7FFF;
  int16_t   mLastY = 0x7FFF;

  uint8_t*  mCoverage = nullptr;

  uint8_t** mStartPtrs = nullptr;
  uint8_t** mCurStartPtr = nullptr;

  uint16_t* mCounts = nullptr;
  uint16_t* mCurCount = nullptr;

  uint16_t  mNumSpans = 0;
  };
//}}}
//{{{
class cRasteriser {
public:
  //{{{
  cRasteriser() {
    // set gamma 1.2 lut
    for (unsigned i = 0; i < 256; i++)
      mGamma[i] = (uint8_t)(pow(double(i) / 255.0, 1.2) * 255.0);
    }
  //}}}

  void moveTo (const cPointF& p) { mOutline.moveTo (int(p.x * 256.f), int(p.y * 256.f)); }
  void lineTo (const cPointF& p) { mOutline.lineTo (int(p.x * 256.f), int(p.y * 256.f)); }
  //{{{
  void thickLine (const cPointF& p1, const cPointF& p2, float width) {

    cPointF vec = p2 - p1;
    vec = vec * width / vec.magnitude();

    moveTo (cPointF (p1.x - vec.y, p1.y + vec.x));
    lineTo (cPointF (p2.x - vec.y, p2.y + vec.x));
    lineTo (cPointF (p2.x + vec.y, p2.y - vec.x));
    lineTo (cPointF (p1.x + vec.y, p1.y - vec.x));
    }
  //}}}
  //{{{
  void pointedLine (const cPointF& p1, const cPointF& p2, float width) {

    cPointF vec = p2 - p1;
    vec = vec * width / vec.magnitude();

    moveTo (cPointF (p1.x - vec.y, p1.y + vec.x));
    lineTo (p2);
    lineTo (cPointF (p1.x + vec.y, p1.y - vec.x));
    }
  //}}}
  //{{{
  void thickEllipse (cPointF centre, cPointF radius, float thick) {

    // clockwise ellipse
    ellipse (centre, radius);

    // anticlockwise ellipse
    moveTo (centre + cPointF(radius.x - thick, 0.f));
    for (int i = 1; i < 360; i += 6) {
      auto a = (360 - i) * 3.1415926f / 180.0f;
      lineTo (centre + cPointF (cos(a) * (radius.x - thick), sin(a) * (radius.y - thick)));
      }
    }
  //}}}
  void render (const sRgba& rgba, bool fillNonZero = true);

private:
  //{{{
  struct sCell {
  public:
    //{{{
    void set (int16_t x, int16_t y, int c, int a) {

      mPackedCoord = (y << 16) + x;
      mCoverage = c;
      mArea = a;
      }
    //}}}
    //{{{
    void set_coord (int16_t x, int16_t y) {
      mPackedCoord = (y << 16) + x;
      }
    //}}}
    //{{{
    void setCoverage (int c, int a) {

      mCoverage = c;
      mArea = a;
      }
    //}}}
    //{{{
    void addCoverage (int c, int a) {

      mCoverage += c;
      mArea += a;
      }
    //}}}

    int mPackedCoord;
    int mCoverage;
    int mArea;
    };
  //}}}
  //{{{
  class cOutline {
  public:
    cOutline() {
      mNumCellsInBlock = 2048;
      reset();
      }
    //{{{
    ~cOutline() {

      vPortFree (mSortedCells);

      if (mNumBlockOfCells) {
        sCell** ptr = mBlockOfCells + mNumBlockOfCells - 1;
        while (mNumBlockOfCells--) {
          // free a block of cells
          dtcmFree (*ptr);
          ptr--;
          }

        // free pointers to blockOfCells
        vPortFree (mBlockOfCells);
        }
      }
    //}}}

    //{{{
    void reset() {

      mNumCells = 0;
      mCurCell.set (0x7FFF, 0x7FFF, 0, 0);
      mSortRequired = true;
      mClosed = true;
      mMinx =  0x7FFFFFFF;
      mMiny =  0x7FFFFFFF;
      mMaxx = -0x7FFFFFFF;
      mMaxy = -0x7FFFFFFF;
      }
    //}}}
    //{{{
    void moveTo (int x, int y) {

      if (!mSortRequired)
        reset();

      if (!mClosed)
        lineTo (mClosex, mClosey);

      setCurCell (x >> 8, y >> 8);

      mCurx = x;
      mClosex = x;
      mCury = y;
      mClosey = y;
      }
    //}}}
    //{{{
    void lineTo (int x, int y) {

      if (mSortRequired && ((mCurx ^ x) | (mCury ^ y))) {
        int c = mCurx >> 8;
        if (c < mMinx)
          mMinx = c;
        ++c;
        if (c > mMaxx)
          mMaxx = c;

        c = x >> 8;
        if (c < mMinx)
          mMinx = c;
        ++c;
        if (c > mMaxx)
          mMaxx = c;

        renderLine (mCurx, mCury, x, y);
        mCurx = x;
        mCury = y;
        mClosed = false;
        }
      }
    //}}}

    int getMinx() const { return mMinx; }
    int getMiny() const { return mMiny; }
    int getMaxx() const { return mMaxx; }
    int getMaxy() const { return mMaxy; }

    //{{{
    const sCell* const* getSortedCells() {

      if (!mClosed) {
        lineTo (mClosex, mClosey);
        mClosed = true;
        }

      // Perform sort only the first time.
      if (mSortRequired) {
        addCurCell();
        if (mNumCells == 0)
          return 0;
        sortCells();
        mSortRequired = false;
        }

      return mSortedCells;
      }
    //}}}
    uint16_t getNumCells() const { return mNumCells; }

  private:
    //{{{
    void addCurCell() {

      if (mCurCell.mArea | mCurCell.mCoverage) {
        if ((mNumCells % mNumCellsInBlock) == 0) {
          // use next block of sCells
          uint32_t block = mNumCells / mNumCellsInBlock;
          if (block >= mNumBlockOfCells) {
            // allocate new block
            auto newCellPtrs = (sCell**)pvPortMalloc ((mNumBlockOfCells + 1) * sizeof(sCell*));
            if (mBlockOfCells && mNumBlockOfCells) {
              memcpy (newCellPtrs, mBlockOfCells, mNumBlockOfCells * sizeof(sCell*));
              vPortFree (mBlockOfCells);
              }
            mBlockOfCells = newCellPtrs;
            mBlockOfCells[mNumBlockOfCells] = (sCell*)dtcmAlloc (mNumCellsInBlock * sizeof(sCell));
            mNumBlockOfCells++;
            printf ("allocated new blockOfCells %d of %d\n", block, mNumBlockOfCells);
            }
          mCurCellPtr = mBlockOfCells[block];
          }

        *mCurCellPtr++ = mCurCell;
        mNumCells++;
        }
      }
    //}}}
    //{{{
    void setCurCell (int16_t x, int16_t y) {

      if (mCurCell.mPackedCoord != (y << 16) + x) {
        addCurCell();
        mCurCell.set (x, y, 0, 0);
        }
     }
    //}}}
    //{{{
    void swapCells (sCell** a, sCell** b) {
      sCell* temp = *a;
      *a = *b;
      *b = temp;
      }
    //}}}
    //{{{
    void sortCells() {

      if (mNumCells == 0)
        return;

      // allocate mSortedCells, a contiguous vector of sCell pointers
      if (mNumCells > mNumSortedCells) {
        vPortFree (mSortedCells);
        mSortedCells = (sCell**)pvPortMalloc ((mNumCells + 1) * 4);
        mNumSortedCells = mNumCells;
        }

      // point mSortedCells at sCells
      sCell** blockPtr = mBlockOfCells;
      sCell** sortedPtr = mSortedCells;
      uint16_t numBlocks = mNumCells / mNumCellsInBlock;
      while (numBlocks--) {
        sCell* cellPtr = *blockPtr++;
        unsigned cellInBlock = mNumCellsInBlock;
        while (cellInBlock--)
          *sortedPtr++ = cellPtr++;
        }

      sCell* cellPtr = *blockPtr++;
      unsigned cellInBlock = mNumCells % mNumCellsInBlock;
      while (cellInBlock--)
        *sortedPtr++ = cellPtr++;

      // terminate mSortedCells with nullptr
      mSortedCells[mNumCells] = nullptr;

      // sort it
      qsortCells (mSortedCells, mNumCells);
      }
    //}}}

    //{{{
    void renderScanLine (int32_t ey, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {

      int ex1 = x1 >> 8;
      int ex2 = x2 >> 8;
      int fx1 = x1 & 0xFF;
      int fx2 = x2 & 0xFF;

      // trivial case. Happens often
      if (y1 == y2) {
        setCurCell (ex2, ey);
        return;
        }

      //everything is located in a single cell.  That is easy!
      if (ex1 == ex2) {
        int delta = y2 - y1;
        mCurCell.addCoverage (delta, (fx1 + fx2) * delta);
        return;
        }

      //ok, we'll have to render a run of adjacent cells on the same
      //cScanLine...
      int p = (0x100 - fx1) * (y2 - y1);
      int first = 0x100;
      int incr = 1;
      int dx = x2 - x1;
      if (dx < 0) {
        p     = fx1 * (y2 - y1);
        first = 0;
        incr  = -1;
        dx    = -dx;
        }

      int delta = p / dx;
      int mod = p % dx;
      if (mod < 0) {
        delta--;
        mod += dx;
        }

      mCurCell.addCoverage (delta, (fx1 + first) * delta);

      ex1 += incr;
      setCurCell (ex1, ey);
      y1  += delta;
      if (ex1 != ex2) {
        p = 0x100 * (y2 - y1 + delta);
        int lift = p / dx;
        int rem = p % dx;
        if (rem < 0) {
          lift--;
          rem += dx;
          }

        mod -= dx;
        while (ex1 != ex2) {
          delta = lift;
          mod  += rem;
          if (mod >= 0) {
            mod -= dx;
            delta++;
            }

          mCurCell.addCoverage (delta, (0x100) * delta);
          y1  += delta;
          ex1 += incr;
          setCurCell (ex1, ey);
          }
        }
      delta = y2 - y1;
      mCurCell.addCoverage (delta, (fx2 + 0x100 - first) * delta);
      }
    //}}}
    //{{{
    void renderLine (int32_t x1, int32_t y1, int32_t x2, int32_t y2) {

      int ey1 = y1 >> 8;
      int ey2 = y2 >> 8;
      int fy1 = y1 & 0xFF;
      int fy2 = y2 & 0xFF;

      int x_from, x_to;
      int p, rem, mod, lift, delta, first;

      if (ey1   < mMiny)
        mMiny = ey1;
      if (ey1+1 > mMaxy)
        mMaxy = ey1+1;
      if (ey2   < mMiny)
        mMiny = ey2;
      if (ey2+1 > mMaxy)
        mMaxy = ey2+1;

      int dx = x2 - x1;
      int dy = y2 - y1;

      // everything is on a single cScanLine
      if (ey1 == ey2) {
        renderScanLine (ey1, x1, fy1, x2, fy2);
        return;
        }

      // Vertical line - we have to calculate start and end cell
      // the common values of the area and coverage for all cells of the line.
      // We know exactly there's only one cell, so, we don't have to call renderScanLine().
      int incr  = 1;
      if (dx == 0) {
        int ex = x1 >> 8;
        int two_fx = (x1 - (ex << 8)) << 1;
        first = 0x100;
        if (dy < 0) {
          first = 0;
          incr  = -1;
          }

        x_from = x1;
        delta = first - fy1;
        mCurCell.addCoverage (delta, two_fx * delta);

        ey1 += incr;
        setCurCell (ex, ey1);

        delta = first + first - 0x100;
        int area = two_fx * delta;
        while (ey1 != ey2) {
          mCurCell.setCoverage (delta, area);
          ey1 += incr;
          setCurCell (ex, ey1);
          }

        delta = fy2 - 0x100 + first;
        mCurCell.addCoverage (delta, two_fx * delta);
        return;
        }

      // ok, we have to render several cScanLines
      p  = (0x100 - fy1) * dx;
      first = 0x100;
      if (dy < 0) {
        p     = fy1 * dx;
        first = 0;
        incr  = -1;
        dy    = -dy;
        }

      delta = p / dy;
      mod = p % dy;
      if (mod < 0) {
        delta--;
          mod += dy;
        }

      x_from = x1 + delta;
      renderScanLine (ey1, x1, fy1, x_from, first);

      ey1 += incr;
      setCurCell (x_from >> 8, ey1);

      if (ey1 != ey2) {
        p = 0x100 * dx;
        lift  = p / dy;
        rem   = p % dy;
        if (rem < 0) {
          lift--;
          rem += dy;
          }
        mod -= dy;
        while (ey1 != ey2) {
          delta = lift;
          mod  += rem;
          if (mod >= 0) {
            mod -= dy;
            delta++;
            }

          x_to = x_from + delta;
          renderScanLine (ey1, x_from, 0x100 - first, x_to, first);
          x_from = x_to;

          ey1 += incr;
          setCurCell (x_from >> 8, ey1);
          }
        }

      renderScanLine (ey1, x_from, 0x100 - first, x2, fy2);
      }
    //}}}

    //{{{
    void qsortCells (sCell** start, unsigned numCells) {

      sCell**  stack[80];
      sCell*** top;
      sCell**  limit;
      sCell**  base;

      limit = start + numCells;
      base = start;
      top = stack;

      while (true) {
        int len = int(limit - base);

        sCell** i;
        sCell** j;
        sCell** pivot;

        if (len > 9) { // qsort_threshold)
          // we use base + len/2 as the pivot
          pivot = base + len / 2;
          swapCells (base, pivot);

          i = base + 1;
          j = limit - 1;
          // now ensure that *i <= *base <= *j
          if ((*j)->mPackedCoord < (*i)->mPackedCoord)
            swapCells (i, j);
          if ((*base)->mPackedCoord < (*i)->mPackedCoord)
            swapCells (base, i);
          if ((*j)->mPackedCoord < (*base)->mPackedCoord)
            swapCells (base, j);

          while (true) {
            do {
              i++;
              } while ((*i)->mPackedCoord < (*base)->mPackedCoord);
            do {
              j--;
              } while ((*base)->mPackedCoord < (*j)->mPackedCoord);
            if ( i > j )
              break;
            swapCells (i, j);
            }
          swapCells (base, j);

          // now, push the largest sub-array
          if(j - base > limit - i) {
            top[0] = base;
            top[1] = j;
            base   = i;
            }
          else {
            top[0] = i;
            top[1] = limit;
            limit  = j;
            }
          top += 2;
          }
        else {
          // the sub-array is small, perform insertion sort
          j = base;
          i = j + 1;

          for (; i < limit; j = i, i++) {
            for (; (*(j+1))->mPackedCoord < (*j)->mPackedCoord; j--) {
              swapCells (j + 1, j);
              if (j == base)
                break;
              }
            }

          if (top > stack) {
            top  -= 2;
            base  = top[0];
            limit = top[1];
            }
          else
            break;
          }
        }
      }
    //}}}

    uint16_t mNumCellsInBlock = 0;
    uint16_t mNumBlockOfCells = 0;
    uint16_t mNumSortedCells = 0;
    sCell** mBlockOfCells = nullptr;
    sCell** mSortedCells = nullptr;

    uint16_t mNumCells;
    sCell mCurCell;
    sCell* mCurCellPtr = nullptr;

    int mCurx = 0;
    int mCury = 0;
    int mClosex = 0;
    int mClosey = 0;

    int mMinx;
    int mMiny;
    int mMaxx;
    int mMaxy;
    bool mClosed;
    bool mSortRequired;
    };
  //}}}
  //{{{
  unsigned calcAlpha (int area) const {

    int coverage = area >> (8*2 + 1 - 8);
    if (coverage < 0)
      coverage = -coverage;

    if (!mFillNonZero) {
      coverage &= 0x1FF;
      if (coverage > 0x100)
        coverage = 0x200 - coverage;
      }

    if (coverage > 0xFF)
      coverage = 0xFF;

    return coverage;
    }
  //}}}
  //{{{
  void ellipse (cPointF centre, cPointF radius) {

    moveTo (centre + cPointF (radius.x, 0.f));
    for (int i = 1; i < 360; i += 6) {
      auto a = i * 3.1415926f / 180.0f;
      lineTo (centre + cPointF (cos(a) * radius.x, sin(a) * radius.y));
      }
    }
  //}}}

  cOutline mOutline;
  cScanLine mScanLine;
  bool mFillNonZero = true;
  uint8_t mGamma[256];
  };
//}}}

class cLcd {
public:
  enum eDma2dWait { eWaitNone, eWaitDone, eWaitIrq };
  cLcd();
  ~cLcd();

  void init (const std::string& title);
  static uint16_t getWidth() { return LCD_WIDTH; }
  static uint16_t getHeight() { return LCD_HEIGHT; }
  static cPoint getSize() { return cPoint (getWidth(), getHeight()); }

  static uint16_t getBoxHeight() { return BOX_HEIGHT; }
  static uint16_t getSmallFontHeight() { return SMALL_FONT_HEIGHT; }
  static uint16_t getFontHeight() { return FONT_HEIGHT; }
  static uint16_t getBigFontHeight() { return BIG_FONT_HEIGHT; }

  uint16_t getBrightness() { return mBrightness; }
  uint8_t* getDrawBuf() { return (uint8_t*)mBuffer[mDrawBuffer]; }

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
  //{{{
  void toggle() {
    mShowInfo = !mShowInfo;
    change();
    }
  //}}}

  void info (uint16_t colour, const std::string str);
  void info (const std::string str) { info (COL_WHITE, str); }

  void clear (uint16_t colour);
  void rect (uint16_t colour, const cRect& r);
  void rectClipped (uint16_t colour, cRect r);
  void rectOutline (uint16_t colour, const cRect& r, uint8_t thickness);
  void ellipse (uint16_t colour, cPoint centre, cPoint radius);

  void stamp (uint16_t colour, uint8_t* src, const cRect& r, uint8_t alpha = 255);
  void stampClipped (uint16_t colour, uint8_t* src, cRect r, uint8_t alpha = 255);
  int text (uint16_t colour, uint16_t fontHeight, const std::string& str, cRect r, uint8_t alpha = 255);

  void copy (cTile* tile, cPoint p);
  void copy90 (cTile* tile, cPoint p);
  void size (cTile* tile, const cRect& r);

  inline void pixel (uint16_t colour, cPoint p) { *(mBuffer[mDrawBuffer] + p.y * getWidth() + p.x) = colour; }
  void grad (uint16_t colTL, uint16_t colTR, uint16_t colBL, uint16_t colBR, const cRect& r);
  void line (uint16_t colour, cPoint p1, cPoint p2);
  void ellipseOutline (uint16_t colour, cPoint centre, cPoint radius);

  static void rgb888toRgb565 (uint8_t* src, uint8_t* dst, uint16_t xsize, uint16_t ysize);
  static void yuvMcuToRgb565 (uint8_t* src, uint8_t* dst, uint16_t xsize, uint16_t ysize, uint32_t chromaSampling);

  void moveTo (const cPointF& p);
  void lineTo (const cPointF& p);
  void thickLine (const cPointF& p1, const cPointF& p2, float width);
  void pointedLine (const cPointF& p1, const cPointF& p2, float width);
  void thickEllipse (cPointF centre, cPointF radius, float thick);
  void render (const sRgba& rgba, bool fillNonZero = true);
  void render (const cScanLine& scanLine, const sRgba& rgba);

  void start();
  void drawInfo();
  void present();

  void display (int brightness);

  static cLcd* mLcd;
  static uint32_t mShowBuffer;

  static eDma2dWait mDma2dWait;
  static SemaphoreHandle_t mDma2dSem;
  static SemaphoreHandle_t mFrameSem;

  void* operator new (std::size_t size) { return pvPortMalloc (size); }
  void operator delete (void* ptr) { vPortFree (ptr); }

private:
  void ltdcInit (uint16_t* frameBufferAddress);
  cFontChar* loadChar (uint16_t fontHeight, char ch);

  static void ready();
  void reset();

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

  std::map<uint16_t, cFontChar*> mFontCharMap;

  FT_Library FTlibrary;
  FT_Face FTface;
  FT_GlyphSlot FTglyphSlot;

  bool mShowInfo = true;
  std::string mTitle;

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
    int mColour = COL_WHITE;
    std::string mString;
    };
  //}}}
  cLine mLines[kMaxLines];
  int mCurLine = 0;
  //}}}
  };
