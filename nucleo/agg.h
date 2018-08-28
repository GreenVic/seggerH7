// agg.h
//{{{  description
//----------------------------------------------------------------------------
// Anti-Grain Geometry - Version 2.1 Lite
// Copyright (C) 2002-2003 Maxim Shemanarev (McSeem)
//
// Permission to copy, use, modify, sell and distribute this software
// is granted provided this copyright notice appears in all copies.
// This software is provided "as is" without express or implied
// warranty, and with no claim as to its suitability for any purpose.
//
// The author gratefully acknowleges the support of David Turner,
// Robert Wilhelm, and Werner Lemberg - the authors of the FreeType
// libray - in producing this work. See http://www.freetype.org for details.
//
//----------------------------------------------------------------------------
// Contact: mcseem@antigrain.com
//          mcseemagg@yahoo.com
//          http://www.antigrain.com
//----------------------------------------------------------------------------
//}}}
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
struct sRgb565a {
  sRgb565a (uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_= 255) : r(r_), g(g_), b(b_), a(a_) {}

  unsigned int r:5;
  unsigned int g:6;
  unsigned int b:5;
  unsigned int a:8;
  };
//}}}
//{{{
struct sRgb565 {
  sRgb565 (uint8_t r_, uint8_t g_, uint8_t b_) : r(r_), g(g_), b(b_) {}

  unsigned int r:5;
  unsigned int g:6;
  unsigned int b:5;
  };
//}}}

//{{{
class cScanLine {
public:
  //{{{
  class iterator {
  public:
    iterator (const cScanLine& scanLine) :
      mCover(scanLine.mCover), mCurcount(scanLine.mCounts), mCurstart_ptr(scanLine.mStartPtrs) {}
    //{{{
    int next() {
      ++mCurcount;
      ++mCurstart_ptr;
      return int(*mCurstart_ptr - mCover);
      }
    //}}}

    int num_pix() const { return int(*mCurcount); }
    const uint8_t* covers() const { return *mCurstart_ptr; }

  private:
    const uint8_t* mCover;
    const uint16_t* mCurcount;
    const uint8_t* const* mCurstart_ptr;
    };
  //}}}
  friend class iterator;

  //{{{
  cScanLine() : mMinx(0), mMaxlen(0), mLastX(0x7FFF), mLastY(0x7FFF),
                mCover(0), mStartPtrs(0), mCounts(0), mNumSpans(0), mCurstart_ptr(0), mCurcount(0) {}
  //}}}
  //{{{
  ~cScanLine() {

    vPortFree (mCounts);
    vPortFree (mStartPtrs);
    vPortFree (mCover);
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
    mCurcount = mCounts;
    mCurstart_ptr = mStartPtrs;
    mNumSpans = 0;
    }
  //}}}
  //{{{
  void reset (int16_t min_x, int16_t max_x) {

    unsigned max_len = max_x - min_x + 2;
    if (max_len > mMaxlen) {
      vPortFree (mCounts);
      vPortFree (mStartPtrs);
      vPortFree (mCover);
      mCover = (uint8_t*)pvPortMalloc (max_len);
      mStartPtrs = (uint8_t**)pvPortMalloc (max_len*4);
      mCounts = (uint16_t*)pvPortMalloc (max_len*2);
      mMaxlen = max_len;
      }

    mLastX = 0x7FFF;
    mLastY = 0x7FFF;
    mMinx = min_x;
    mCurcount = mCounts;
    mCurstart_ptr = mStartPtrs;
    mNumSpans = 0;
    }
  //}}}

  //{{{
  void addSpan (int16_t x, int16_t y, uint16_t num, uint16_t cover) {

    x -= mMinx;

    memset (mCover + x, cover, num);
    if (x == mLastX+1)
      (*mCurcount) += (uint16_t)num;
    else {
      *++mCurcount = (uint16_t)num;
      *++mCurstart_ptr = mCover + x;
      mNumSpans++;
      }

    mLastX = x + num - 1;
    mLastY = y;
    }
  //}}}

private:
  cScanLine (const cScanLine&);
  const cScanLine& operator = (const cScanLine&);

  int16_t    mMinx = 0;
  uint16_t   mMaxlen = 0;
  int16_t    mLastX = 0x7FFF;
  int16_t    mLastY = 0x7FFF;
  uint8_t*   mCover = nullptr;
  uint8_t**  mStartPtrs = nullptr;
  uint16_t*  mCounts = nullptr;
  uint16_t   mNumSpans = 0;
  uint8_t**  mCurstart_ptr = nullptr;
  uint16_t*  mCurcount = nullptr;
  };
//}}}

//{{{
class cTarget {
public:
  //{{{
  cTarget (uint8_t* buf, uint16_t width, uint16_t height) :
      mBuf(buf), mRows(nullptr), mWidth(width), mHeight(height), mMaxHeight(0) {

    if (height > mMaxHeight) {
      vPortFree (mRows);
      mMaxHeight = height;
      mRows = (uint8_t**)pvPortMalloc (mMaxHeight * 4);
      }

    setBuffer (buf);
    }
  //}}}
  ~cTarget() { vPortFree (mRows); }

  uint16_t width() const { return mWidth;  }
  uint16_t height() const { return mHeight; }
  uint8_t* row (uint16_t y) { return mRows[y]; }

  //{{{
  void setBuffer (uint8_t* buf) {

    mBuf = buf;
    uint8_t* row_ptr = mBuf;
    uint8_t** rows = mRows;
    int height = mHeight;
    while (height--) {
      *rows++ = row_ptr;
      row_ptr += mWidth*2;
      }
    }
  //}}}
  bool inbox (int x, int y) const { return x >= 0 && y >= 0 && x < int(mWidth) && y < int(mHeight); }

private:
  cTarget (const cTarget&);
  const cTarget& operator = (const cTarget&);

private:
  uint8_t*  mBuf;        // Pointer to renrdering buffer
  uint8_t** mRows;       // Pointers to each row of the buffer
  uint16_t  mWidth;      // Width in pixels
  uint16_t  mHeight;     // Height in pixels
  uint16_t  mMaxHeight;  // Maximal current height
  };
//}}}
//{{{
class cRenderer {
public:
  cRenderer (cTarget& target) : mTarget (&target) {}
  //{{{
  void render (const cScanLine& scanLine, const sRgb888a& rgba) {

    if ((scanLine.getY() < 0) || (scanLine.getY() >= int(mTarget->height())))
      return;

    uint16_t numSpans = scanLine.getNumSpans();

    int base_x = scanLine.getBaseX();
    auto row = mTarget->row (scanLine.getY());

    cScanLine::iterator span (scanLine);
    do {
      auto x = span.next() + base_x;
      const uint8_t* covers = span.covers();
      auto num_pix = span.num_pix();
      if (x < 0) {
        num_pix += x;
        if (num_pix <= 0)
          continue;
        covers -= x;
        x = 0;
        }
      if (x + num_pix >= int(mTarget->width())) {
        num_pix = mTarget->width() - x;
        if (num_pix <= 0)
          continue;
        }

      uint16_t* p = ((uint16_t*)row) + x;
      do {
        uint16_t alpha = (*covers++) * (rgba.a);
        if (alpha >= 0xFE00)
          *p++ = ((rgba.r >> 3) << 11) | ((rgba.g >> 2) << 5) | (rgba.b >> 3);
        else {
          // blend
          uint16_t rgb = *p;
          uint8_t r = (rgb >> 8) & 0xF8;
          uint8_t g = (rgb >> 3) & 0xFC;
          uint8_t b = (rgb << 3) & 0xF8;

          *p++ = (((((rgba.r - r) * alpha) + (r << 16)) >> 8) & 0xF800) |
                 (((((rgba.g - g) * alpha) + (g << 16)) >> 13) & 0x7E0) |
                  ((((rgba.b - b) * alpha) + (b << 16)) >> 19);
          }
        } while (--num_pix);
      } while (--numSpans);
    }
  //}}}

private:
  cTarget* mTarget;
  };
//}}}
//{{{
class cRasteriser {
//{{{  description
// Polygon cRasteriser that is used to render filled polygons with
// high-quality Anti-Aliasing. Internally, by default, the class uses
// integer coordinates in format 24.8, i.e. 24 bits for integer part
// and 8 bits for fractional - see 8. This class can be
// used in the following  way:
//
// 1. filling_rule(filling_rule_e ft) - optional.
// 2. gamma() - optional.
// 3. reset()
// 4. move_to(x, y) / line_to(x, y) - make the polygon. One can create
//    more than one contour, but each contour must consist of at least 3
//    vertices, i.e. move_to(x1, y1); line_to(x2, y2); line_to(x3, y3);
//    is the absolute minimum of vertices that define a triangle.
//    The algorithm does not check either the number of vertices nor
//    coincidence of their coordinates, but in the worst case it just
//    won't draw anything.
//    The orger of the vertices (clockwise or counterclockwise)
//    is important when using the non-zero filling rule (fill_non_zero).
//    In this case the vertex order of all the contours must be the same
//    if you want your intersecting polygons to be without "holes".
//    You actually can use different vertices order. If the contours do not
//    intersect each other the order is not important anyway. If they do,
//    contours with the same vertex order will be rendered without "holes"
//    while the intersecting contours with different orders will have "holes".
//
// filling_rule() and gamma() can be called anytime before "sweeping".
//}}}
public:
  enum eFilling { fill_non_zero, fill_even_odd };

  cRasteriser() : mFilling (fill_non_zero) { gamma (1.2); }

  int getMinx() const { return mOutline.getMinx(); }
  int getMiny() const { return mOutline.getMiny(); }
  int getMaxx() const { return mOutline.getMaxx(); }
  int getMaxy() const { return mOutline.getMaxy(); }

  void reset() { mOutline.reset(); }
  void setFilling (eFilling filling) { mFilling = filling; }

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
  void ellipse (float x, float y, float rx, float ry) {

    moveTod (x + rx, y);
    for (int i = 1; i < 360; i += 6) {
      auto a = i * 3.1415926f / 180.0f;
      lineTod (x + cos(a) * rx, y + sin(a) * ry);
      }
    }
  //}}}

  //{{{
  void render (cRenderer& r, const sRgb888a& rgba) {

    const sPixelCell* const* cells = mOutline.cells();
    if (mOutline.numCells() == 0)
      return;

    int x, y;
    int cover;
    int alpha;
    int area;

    mScanLine.reset (mOutline.getMinx(), mOutline.getMaxx());

    cover = 0;
    const sPixelCell* cur_cell = *cells++;
    for(;;) {
      const sPixelCell* start_cell = cur_cell;

      int coord  = cur_cell->packed_coord;
      x = cur_cell->x;
      y = cur_cell->y;

      area   = start_cell->area;
      cover += start_cell->cover;

      // accumulate all start cells
      while ((cur_cell = *cells++) != 0) {
        if (cur_cell->packed_coord != coord)
          break;
        area  += cur_cell->area;
        cover += cur_cell->cover;
        }

      if (area) {
        alpha = calcAlpha ((cover << (8 + 1)) - area);
        if (alpha) {
          if (mScanLine.isReady (y)) {
            r.render (mScanLine, rgba);
            mScanLine.resetSpans();
            }
          mScanLine.addSpan (x, y, 1, mGamma[alpha]);
          }
        x++;
        }

      if (!cur_cell)
        break;

      if (cur_cell->x > x) {
        alpha = calcAlpha (cover << (8 + 1));
        if (alpha) {
          if (mScanLine.isReady (y)) {
             r.render (mScanLine, rgba);
             mScanLine.resetSpans();
             }
           mScanLine.addSpan (x, y, cur_cell->x - x, mGamma[alpha]);
           }
        }
      }

    if (mScanLine.getNumSpans())
      r.render (mScanLine, rgba);
    }
  //}}}

private:
  //{{{
  struct sPixelCell {
  public:
    int16_t x;
    int16_t y;
    int packed_coord;
    int cover;
    int area;

    //{{{
    void set_cover (int c, int a) {

      cover = c;
      area = a;
      }
    //}}}
    //{{{
    void add_cover (int c, int a) {

      cover += c;
      area += a;
      }
    //}}}
    //{{{
    void set_coord (int cx, int cy) {
      x = int16_t (cx);
      y = int16_t (cy);
      packed_coord = (cy << 16) + cx;
      }
    //}}}
    //{{{
    void set (int cx, int cy, int c, int a) {

      x = int16_t(cx);
      y = int16_t(cy);
      packed_coord = (cy << 16) + cx;
      cover = c;
      area = a;
      }
    //}}}
    };
  //}}}
  //{{{
  class cOutline {
  public:
    //{{{
    cOutline() : mNumblocks(0), mMaxblocks(0), mCurblock(0), mNumcells(0), mCells(0),
        mCurcell_ptr(0), mSortedcells(0), mSortedsize(0), mCurx(0), mCury(0),
        m_close_x(0), m_close_y(0),
        mMinx(0x7FFFFFFF), mMiny(0x7FFFFFFF), mMaxx(-0x7FFFFFFF), mMaxy(-0x7FFFFFFF),
        m_flags(sort_required) {

      mCurcell.set(0x7FFF, 0x7FFF, 0, 0);
      }
    //}}}
    //{{{
    ~cOutline() {

      vPortFree (mSortedcells);

      if (mNumblocks) {
        sPixelCell** ptr = mCells + mNumblocks - 1;
        while(mNumblocks--) {
          vPortFree (*ptr);
          ptr--;
          }
        vPortFree (mCells);
        }
      }
    //}}}

    //{{{
    void reset() {

      mNumcells = 0;
      mCurblock = 0;
      mCurcell.set (0x7FFF, 0x7FFF, 0, 0);
      m_flags |= sort_required;
      m_flags &= ~not_closed;
      mMinx =  0x7FFFFFFF;
      mMiny =  0x7FFFFFFF;
      mMaxx = -0x7FFFFFFF;
      mMaxy = -0x7FFFFFFF;
      }
    //}}}
    //{{{
    void moveTo (int x, int y) {

      if ((m_flags & sort_required) == 0)
        reset();

      if (m_flags & not_closed)
        lineTo (m_close_x, m_close_y);

      set_cur_cell (x >> 8, y >> 8);
      m_close_x = mCurx = x;
      m_close_y = mCury = y;
      }
    //}}}
    //{{{
    void lineTo (int x, int y) {

      if ((m_flags & sort_required) && ((mCurx ^ x) | (mCury ^ y))) {
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
        m_flags |= not_closed;
        }
      }
    //}}}

    int getMinx() const { return mMinx; }
    int getMiny() const { return mMiny; }
    int getMaxx() const { return mMaxx; }
    int getMaxy() const { return mMaxy; }

    unsigned numCells() const {return mNumcells; }
    //{{{
    const sPixelCell* const* cells() {

      if (m_flags & not_closed) {
        lineTo (m_close_x, m_close_y);
        m_flags &= ~not_closed;
        }

      // Perform sort only the first time.
      if(m_flags & sort_required) {
        add_cur_cell();
        if(mNumcells == 0)
          return 0;
        sortCells();
        m_flags &= ~sort_required;
        }

      return mSortedcells;
      }
    //}}}

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
      return (*a)->packed_coord < (*b)->packed_coord;
      }
    //}}}

    enum { not_closed = 1, sort_required = 2 };
    static const int kCellBlockPool = 256;
    static const int kCellBlockLimit = 1024;

    cOutline (const cOutline&);
    const cOutline& operator = (const cOutline&);

    //{{{
    void set_cur_cell (int x, int y) {

      if (mCurcell.packed_coord != (y << 16) + x) {
        add_cur_cell();
        mCurcell.set (x, y, 0, 0);
        }
     }
    //}}}
    //{{{
    void add_cur_cell() {

      if (mCurcell.area | mCurcell.cover) {
        if ((mNumcells & 0xFFF) == 0) {
          if (mNumblocks >= kCellBlockLimit)
            return;
          allocateBlock();
          }
        *mCurcell_ptr++ = mCurcell;
        mNumcells++;
        }
      }
    //}}}
    //{{{
    void sortCells() {

      if (mNumcells == 0)
        return;

      if (mNumcells > mSortedsize) {
        vPortFree (mSortedcells);
        mSortedsize = mNumcells;
        mSortedcells = (sPixelCell**)pvPortMalloc ((mNumcells + 1) * 4);
        }

      sPixelCell** sorted_ptr = mSortedcells;
      sPixelCell** block_ptr = mCells;
      sPixelCell* cell_ptr;
      unsigned i;
      unsigned nb = mNumcells >> 12;
      while (nb--) {
        cell_ptr = *block_ptr++;
        i = 0x1000;
        while (i--)
          *sorted_ptr++ = cell_ptr++;
        }

      cell_ptr = *block_ptr++;
      i = mNumcells & 0xFFF;
      while (i--)
        *sorted_ptr++ = cell_ptr++;
      mSortedcells[mNumcells] = 0;

      qsortCells (mSortedcells, mNumcells);
      }
    //}}}

    //{{{
    void renderScanLine (int ey, int x1, int y1, int x2, int y2) {

      int ex1 = x1 >> 8;
      int ex2 = x2 >> 8;
      int fx1 = x1 & 0xFF;
      int fx2 = x2 & 0xFF;

      // trivial case. Happens often
      if (y1 == y2) {
        set_cur_cell (ex2, ey);
        return;
        }

      //everything is located in a single cell.  That is easy!
      if (ex1 == ex2) {
        int delta = y2 - y1;
        mCurcell.add_cover (delta, (fx1 + fx2) * delta);
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

      mCurcell.add_cover (delta, (fx1 + first) * delta);

      ex1 += incr;
      set_cur_cell(ex1, ey);
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

          mCurcell.add_cover (delta, (0x100) * delta);
          y1  += delta;
          ex1 += incr;
          set_cur_cell (ex1, ey);
          }
        }
      delta = y2 - y1;
      mCurcell.add_cover (delta, (fx2 + 0x100 - first) * delta);
      }
    //}}}
    //{{{
    void renderLine (int x1, int y1, int x2, int y2) {

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
        mCurcell.add_cover (delta, two_fx * delta);

        ey1 += incr;
        set_cur_cell (ex, ey1);

        delta = first + first - 0x100;
        int area = two_fx * delta;
        while (ey1 != ey2) {
          mCurcell.set_cover (delta, area);
          ey1 += incr;
          set_cur_cell (ex, ey1);
          }

        delta = fy2 - 0x100 + first;
        mCurcell.add_cover (delta, two_fx * delta);
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
      set_cur_cell (x_from >> 8, ey1);

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
          set_cur_cell (x_from >> 8, ey1);
          }
        }
      renderScanLine (ey1, x_from, 0x100 - first, x2, fy2);
      }
    //}}}
    //{{{
    void allocateBlock() {

      if (mCurblock >= mNumblocks) {
        if (mNumblocks >= mMaxblocks) {
          auto newCells = (sPixelCell**)pvPortMalloc ((mMaxblocks + kCellBlockPool) * 4);
          if (mCells) {
            memcpy (newCells, mCells, mMaxblocks * sizeof(sPixelCell*));
            vPortFree (mCells);
            }
          mCells = newCells;
          mMaxblocks += kCellBlockPool;
          }
        mCells[mNumblocks++] = (sPixelCell*)pvPortMalloc (0x1000*4);
        }

      mCurcell_ptr = mCells[mCurblock++];
      }
    //}}}

    //{{{
    void qsortCells (sPixelCell** start, unsigned num) {

      sPixelCell**  stack[80];
      sPixelCell*** top;
      sPixelCell**  limit;
      sPixelCell**  base;

      limit = start + num;
      base = start;
      top = stack;

      for (;;) {
        int len = int(limit - base);

        sPixelCell** i;
        sPixelCell** j;
        sPixelCell** pivot;

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

          for(;;) {
            do
              i++;
              while (lessThan (i, base) );
            do
              j--;
              while (lessThan (base, j) );
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

    unsigned mNumblocks;
    unsigned mMaxblocks;
    unsigned mCurblock;
    unsigned mNumcells;

    sPixelCell** mCells;
    sPixelCell* mCurcell_ptr;
    sPixelCell** mSortedcells;
    unsigned mSortedsize;
    sPixelCell mCurcell;

    int mCurx;
    int mCury;
    int m_close_x;
    int m_close_y;

    int mMinx;
    int mMiny;
    int mMaxx;
    int mMaxy;
    unsigned m_flags;
    };
  //}}}

  cRasteriser (const cRasteriser&);
  const cRasteriser& operator = (const cRasteriser&);

  //{{{
  void gamma (double g) {

    for (unsigned i = 0; i < 256; i++)
      mGamma[i] = (uint8_t)(pow(double(i) / 255.0, g) * 255.0);
    }
  //}}}
  //{{{
  unsigned calcAlpha (int area) const {

    int cover = area >> (8*2 + 1 - 8);
    if (cover < 0)
      cover = -cover;

    if (mFilling == fill_even_odd) {
      cover &= 0x1FF;
      if (cover > 0x100)
        cover = 0x200 - cover;
      }

    if (cover > 0xFF)
      cover = 0xFF;

    return cover;
    }
  //}}}

  cOutline mOutline;
  cScanLine mScanLine;
  eFilling mFilling;
  uint8_t mGamma[256];
  };
//}}}
