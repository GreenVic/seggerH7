// agg.h
#pragma once
//{{{  includes
#include <stdint.h>
#include <string.h>
#include <math.h>
//}}}

//{{{
struct sRgb888a {
  sRgb888a (uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_= 255) : r(r_), g(g_), b(b_), a(a_) {}

  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
  };
//}}}
//{{{
//struct sRgb565a {
  //sRgb565a (uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_= 255) : r(r_), g(g_), b(b_), a(a_) {}

  //unsigned int r:5;
  //unsigned int g:6;
  //unsigned int b:5;
  //unsigned int a:8;
  //};
//}}}
//{{{
//struct sRgb565 {
  //sRgb565 (uint8_t r_, uint8_t g_, uint8_t b_) : r(r_), g(g_), b(b_) {}

  //unsigned int r:5;
  //unsigned int g:6;
  //unsigned int b:5;
  //};
//}}}

//{{{
struct sCell {
public:
  int mPackedCoord;
  int mCoverage;
  int mArea;

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
  };
//}}}
//{{{
class cOutline {
public:
  //{{{
  cOutline() : mMinx(0x7FFFFFFF), mMiny(0x7FFFFFFF),
               mMaxx(-0x7FFFFFFF), mMaxy(-0x7FFFFFFF), mFlags(sort_required) {

    mCurCell.set (0x7FFF, 0x7FFF, 0, 0);
    }
  //}}}
  //{{{
  ~cOutline() {

    vPortFree (mSortedCells);

    if (mNumBlockCells) {
      sCell** ptr = mCells + mNumBlockCells - 1;
      while (mNumBlockCells--) {
        vPortFree (*ptr);
        ptr--;
        }
      vPortFree (mCells);
      }
    }
  //}}}

  //{{{
  void reset() {

    mNumCells = 0;
    mCurCell.set (0x7FFF, 0x7FFF, 0, 0);
    mFlags |= sort_required;
    mFlags &= ~not_closed;
    mMinx =  0x7FFFFFFF;
    mMiny =  0x7FFFFFFF;
    mMaxx = -0x7FFFFFFF;
    mMaxy = -0x7FFFFFFF;
    }
  //}}}
  //{{{
  void moveTo (int x, int y) {

    if ((mFlags & sort_required) == 0)
      reset();

    if (mFlags & not_closed)
      lineTo (m_close_x, m_close_y);

    setCurCell (x >> 8, y >> 8);
    m_close_x = mCurx = x;
    m_close_y = mCury = y;
    }
  //}}}
  //{{{
  void lineTo (int x, int y) {

    if ((mFlags & sort_required) && ((mCurx ^ x) | (mCury ^ y))) {
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
      mFlags |= not_closed;
      }
    }
  //}}}

  int getMinx() const { return mMinx; }
  int getMiny() const { return mMiny; }
  int getMaxx() const { return mMaxx; }
  int getMaxy() const { return mMaxy; }

  //{{{
  const sCell* const* getCells() {

    if (mFlags & not_closed) {
      lineTo (m_close_x, m_close_y);
      mFlags &= ~not_closed;
      }

    // Perform sort only the first time.
    if (mFlags & sort_required) {
      addCurCell();
      if (mNumCells == 0)
        return 0;
      sortCells();
      mFlags &= ~sort_required;
      }

    return mSortedCells;
    }
  //}}}
  uint16_t getNumCells() const { return mNumCells; }

private:
  //{{{
  template <class T> static inline void swapCells (T* a, T* b) {
    T temp = *a;
    *a = *b;
    *b = temp;
    }
  //}}}
  //{{{
  template <class T> static inline bool lessThan (T* a, T* b) {
    return (*a)->mPackedCoord < (*b)->mPackedCoord;
    }
  //}}}

  enum { not_closed = 1, sort_required = 2 };
  static const uint16_t kBlockCells = 1024;

  //{{{
  void addCurCell() {

    if (mCurCell.mArea | mCurCell.mCoverage) {
      if ((mNumCells % kBlockCells) == 0) {
        // use next block of sCells
        uint32_t block = mNumCells / kBlockCells;
        if (block >= mNumBlockCells) {
          // allocate new block
          auto newCellPtrs = (sCell**)pvPortMalloc ((mNumBlockCells + 1) * sizeof(sCell*));
          if (mCells && mNumBlockCells) {
            memcpy (newCellPtrs, mCells, mNumBlockCells * sizeof(sCell*));
            vPortFree (mCells);
            }
          mCells = newCellPtrs;
          mCells[mNumBlockCells] = (sCell*)pvPortMalloc (kBlockCells * sizeof(sCell));
          mNumBlockCells++;
          printf ("allocated new blockOfCells %d of %d\n", block, mNumBlockCells);
          }
        mCurCellPtr = mCells[block];
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
  void sortCells() {

    if (mNumCells == 0)
      return;

    // allocate mSortedCells, contiguous vector of sCell pointers
    if (mNumCells > mNumSortedCells) {
      vPortFree (mSortedCells);
      mSortedCells = (sCell**)pvPortMalloc ((mNumCells + 1) * 4);
      mNumSortedCells = mNumCells;
      }

    // point mSortedCells at sCells
    sCell** blockPtr = mCells;
    sCell** sortedPtr = mSortedCells;
    uint16_t numBlocks = mNumCells / kBlockCells;
    while (numBlocks--) {
      sCell* cellPtr = *blockPtr++;
      unsigned cellInBlock = kBlockCells;
      while (cellInBlock--)
        *sortedPtr++ = cellPtr++;
      }

    sCell* cellPtr = *blockPtr++;
    unsigned cellInBlock = mNumCells % kBlockCells;
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
        if (lessThan (j, i))
          swapCells (i, j);
        if (lessThan (base, i))
          swapCells (base, i);
        if (lessThan (j, base))
          swapCells (base, j);

        while (true) {
          do {
            i++;
            } while (lessThan (i, base) );
          do {
            j--;
            } while (lessThan (base, j) );
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
          for (; lessThan(j + 1, j); j--) {
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

  uint16_t mNumCells = 0;
  uint16_t mNumBlockCells = 0;
  uint16_t mNumSortedCells = 0;

  sCell mCurCell;
  sCell* mCurCellPtr = nullptr;
  sCell** mCells = nullptr;
  sCell** mSortedCells = nullptr;

  int mCurx = 0;
  int mCury = 0;
  int m_close_x = 0;
  int m_close_y = 0;

  int mMinx;
  int mMiny;
  int mMaxx;
  int mMaxy;
  unsigned mFlags = 0;
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
class cRenderer {
public:
  cRenderer (cLcd* lcd) : mLcd(lcd) {}

  void render (const cScanLine& scanLine, const sRgb888a& rgba) {

    uint16_t col = ((rgba.r >> 3) << 11) | ((rgba.g >> 2) << 5) | (rgba.b >> 3);

    auto y = scanLine.getY();
    if (y < 0)
      return;
    if (y >= mLcd->getHeight())
      return;

    int baseX = scanLine.getBaseX();
    uint16_t numSpans = scanLine.getNumSpans();
    cScanLine::iterator span (scanLine);
    do {
      auto x = baseX + span.next() ;
      uint8_t* coverage = (uint8_t*)span.getCoverage();
      int16_t numPix = span.getNumPix();
      if (x < 0) {
        numPix += x;
        if (numPix <= 0)
          continue;
        coverage -= x;
        x = 0;
        }
      if (x + numPix >= mLcd->getWidth()) {
        numPix = mLcd->getWidth() - x;
        if (numPix <= 0)
          continue;
        }

      if (rgba.a < 255) {
        //{{{  apply alpha to coverage
        auto numPixa = numPix;
        uint8_t* coveragea = coverage;
        do {
          uint16_t value = *coveragea * rgba.a;
          *coveragea++ = value >> 8;
          } while (--numPixa);
        }
        //}}}
      mLcd->stamp (col, coverage, cRect (x, y, x+numPix, scanLine.getY()+1));
      } while (--numSpans);
    }

private:
  cLcd* mLcd = nullptr;
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

  void moveTo (int x, int y) { mOutline.moveTo (x, y); }
  void lineTo (int x, int y) { mOutline.lineTo (x, y); }
  void moveTod (float x, float y) { mOutline.moveTo (int(x * 0x100), int(y * 0x100)); }
  void lineTod (float x, float y) { mOutline.lineTo (int(x * 0x100), int(y * 0x100)); }

  //{{{
  void line (float x1, float y1, float x2, float y2, float width) {

    float dx = x2 - x1;
    float dy = y2 - y1;
    float d = sqrt(dx*dx + dy*dy);

    dx = width * (y2 - y1) / d;
    dy = width * (x2 - x1) / d;

    moveTod (x1 - dx,  y1 + dy);
    lineTod (x2 - dx,  y2 + dy);
    lineTod (x2 + dx,  y2 - dy);
    lineTod (x1 + dx,  y1 - dy);
    }
  //}}}
  //{{{
  void pointedLine (float x1, float y1, float x2, float y2, float width) {

    float dx = x2 - x1;
    float dy = y2 - y1;
    float d = sqrt(dx*dx + dy*dy);

    dx = width * (y2 - y1) / d;
    dy = width * (x2 - x1) / d;

    moveTod (x1 - dx,  y1 + dy);
    lineTod (x2,  y2);
    lineTod (x1 + dx,  y1 - dy);
    }
  //}}}
  //{{{
  void ellipse (float x, float y, float rx, float ry) {

    moveTod (x + rx, y);
    for (int i = 1; i < 360; i += 6) {
      auto a = i * 3.1415926f / 180.0f;
      lineTod (x + cos(a) * rx, y + sin(a) * ry);
      }
    }
  //}}}
  //{{{
  void outEllipse (float x, float y, float rx, float ry, float thick) {

    moveTod (x + rx, y);
    for (int i = 1; i < 360; i += 6) {
      auto a = i * 3.1415926f / 180.0f;
      lineTod (x + cos(a) * rx, y + sin(a) * ry);
      }

    moveTod (x + rx - thick, y);
    for (int i = 1; i < 360; i += 6) {
      auto a = (360 - i) * 3.1415926f / 180.0f;
      lineTod (x + cos(a) * (rx-thick), y + sin(a) * (ry-thick));
      }
    }
  //}}}

  //{{{
  void render (cRenderer& renderer, const sRgb888a& rgba, bool fillNonZero = true) {

    mFillNonZero = fillNonZero;

    const sCell* const* cells = mOutline.getCells();
    printf ("render %d cells\n", mOutline.getNumCells());
    if (mOutline.getNumCells() == 0)
      return;

    mScanLine.reset (mOutline.getMinx(), mOutline.getMaxx());

    int coverage = 0;
    const sCell* curCell = *cells++;
    while (true) {
      int x = curCell->mPackedCoord & 0xFFFF;
      int y = curCell->mPackedCoord >> 16;
      int packedCoord = curCell->mPackedCoord;
      int area = curCell->mArea;
      coverage += curCell->mCoverage;

      // accumulate all start cells
      while ((curCell = *cells++) != 0) {
        if (curCell->mPackedCoord != packedCoord)
          break;
        area += curCell->mArea;
        coverage += curCell->mCoverage;
        }

      if (area) {
        int alpha = calcAlpha ((coverage << (8 + 1)) - area);
        if (alpha) {
          if (mScanLine.isReady (y)) {
            renderer.render (mScanLine, rgba);
            mScanLine.resetSpans();
            }
          mScanLine.addSpan (x, y, 1, mGamma[alpha]);
          }
        x++;
        }

      if (!curCell)
        break;

      if (int16_t(curCell->mPackedCoord & 0xFFFF) > x) {
        int alpha = calcAlpha (coverage << (8 + 1));
        if (alpha) {
          if (mScanLine.isReady (y)) {
             renderer.render (mScanLine, rgba);
             mScanLine.resetSpans();
             }
           mScanLine.addSpan (x, y, int16_t(curCell->mPackedCoord & 0xFFFF) - x, mGamma[alpha]);
           }
        }
      }

    if (mScanLine.getNumSpans())
      renderer.render (mScanLine, rgba);
    }
  //}}}

private:
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

  cOutline mOutline;
  cScanLine mScanLine;
  bool mFillNonZero = true;
  uint8_t mGamma[256];
  };
//}}}
