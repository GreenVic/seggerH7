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
class cTarget {
//{{{  description
// Rendering buffer wrapper. This class does not know anything about
// memory organizations, all it does it keeps an array of pointers
// to each pixel row. The general rules of rendering are as follows.
//
// 1. Allocate or create somehow a rendering buffer itself. Since
//    the library does not depend on any particular platform or
//    architecture it was decided that it's your responsibility
//    to create and destroy rendering buffers properly. You can use
//    any available mechanism to create it - you can use a system API
//    function, simple memory allocation, or even statically defined array.
//    You also should know the memory organization (or possible variants)
//    in your system. For example, there's an R,G,B or B,G,R organizations
//    with one byte per component (three byter per pixel) is used very often.
//    So, if you intend to use class render_bgr24, for example, you should
//    allocate at least width*height*3 bytes of memory.
//
// 2. Create a cTarget object and then call method attach(). It requires
//    a pointer to the buffer itself, width and height of the buffer in
//    pixels, and the length of the row in bytes. All these values must
//    properly correspond to the memory organization. The argument stride
//    is used because in reality the row length in bytes does not obligatory
//    correspond with the width of the image in pixels, i.e. it cannot be
//    simply calculated as width_in_pixels * bytes_per_pixel. For example,
//    it must be aligned to 4 bytes in Windows bitmaps. Besides, the value
//    of stride can be negative - it depends on the order of displaying
//    the rendering buffer - from top to bottom or from bottom to top.
//    In other words, if stride > 0 the pointers to each row will start
//    from the beginning of the buffer and increase. If it < 0, the pointers
//    start from the end of the buffer and decrease. It gives you an
//    additional degree of freedom.
//    Method attach() can be called more than once. The execution time of it
//    is very little, still it allocates memory of heigh * sizeof(char*) bytes
//    and has a loop while(height--) {...}, so it's unreasonable to call it
//    every time before drawing any single pixel :-)
//
// 3. Create an object (or a number of objects) of a rendering class, such as
//    renderer_bgr24_solid, renderer_bgr24_image and so on. These classes
//    require a pointer to the renderer_buffer object, but they do not perform
//    any considerable operations except storing this pointer. So, rendering
//    objects can be created on demand almost any time. These objects know
//    about concrete memory organization (this knowledge is hardcoded), so
//    actually, the memory you allocated or created in clause 1 should
//    actually be in correspondence to the needs of the rendering class.
//
// 4. Rener your image using rendering classes, for example, rasterizer
//
// 5. Display the result, or store it, or whatever. It's also your
//    responsibility and depends on the platform.
//}}}
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

  uint8_t* row (uint16_t y) { return mRows[y];  }
  const uint8_t* row (uint16_t y) const { return mRows[y]; }

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
class cScanline {
 //{{{  description
 // This class is used to transfer data from class outline (or a similar one)
 // to the rendering buffer. It's organized very simple. The class stores
 // information of horizontal spans to render it into a pixel-map buffer.
 // Each span has initial X, length, and an array of bytes that determine the
 // alpha-values for each pixel. So, the restriction of using this class is 256
 // levels of Anti-Aliasing, which is quite enough for any practical purpose.
 // Before using this class you should know the minimal and maximal pixel
 // coordinates of your cScanline. The protocol of using is:
 // 1. reset(min_x, max_x)
 // 2. addCell() / addSpan() - accumulate cScanline. You pass Y-coordinate
 //    into these functions in order to make cScanline know the last Y. Before
 //    calling addCell() / addSpan() you should check with method is_ready(y)
 //    if the last Y has changed. It also checks if the scanline is not empty.
 //    When forming one cScanline the next X coordinate must be always greater
 //    than the last stored one, i.e. it works only with ordered coordinates.
 // 3. If the current cScanline is_ready() you should render it and then call
 //    resetSpans() before adding new cells/spans.
 //
 // 4. Rendering:
 //
 // cScanline provides an iterator class that allows you to extract
 // the spans and the cover values for each pixel. Be aware that clipping
 // has not been done yet, so you should perform it yourself.
 // Use cScanline::iterator to render spans:
 //
 // int base_x = sl.base_x();          // base X. Should be added to the span's X
 //                                    // "sl" is a const reference to the
 //                                    // cScanline passed in.
 //
 // int y = sl.y();                    // Y-coordinate of the cScanline
 //
 // ************************************
 // ...Perform vertical clipping here...
 // ************************************
 //
 // cScanline::iterator span(sl);
 //
 // uint8_t* row = m_rbuf->row(y); // The the address of the beginning
 //                                      // of the current row
 //
 // unsigned num_spans = sl.num_spans(); // Number of spans. It's guaranteed that
 //                                      // num_spans is always greater than 0.
 //
 // do
 // {
 //     int x = span.next() + base_x;        // The beginning X of the span
 //
 //     const uint8_t covers* = span.covers(); // The array of the cover values
 //
 //     int num_pix = span.num_pix();        // Number of pixels of the span.
 //                                          // Always greater than 0, still we
 //                                          // shoud use "int" instead of
 //                                          // "unsigned" because it's more
 //                                          // convenient for clipping
 //
 //     **************************************
 //     ...Perform horizontal clipping here...
 //     ...you have x, covers, and pix_count..
 //     **************************************
 //
 //     uint8_t* dst = row + x;  // Calculate the start address of the row.
 //                                    // In this case we assume a simple
 //                                    // grayscale image 1-byte per pixel.
 //     do
 //     {
 //         *dst++ = *covers++;        // Hypotetical rendering.
 //     }
 //     while(--num_pix);
 // }
 // while(--num_spans);  // num_spans cannot be 0, so this loop is quite safe
 //
 // The question is: why should we accumulate the whole cScanline when we
 // could render just separate spans when they're ready?
 // That's because using the scaline is in general faster. When is consists
 // of more than one span the conditions for the processor cash system
 // are better, because switching between two different areas of memory
 // (that can be large ones) occures less frequently.
 //}}}
public:
  //{{{
  class iterator {
  public:
    iterator(const cScanline& sl) : m_covers(sl.m_covers), mCurcount(sl.m_counts),
                                    mCurstart_ptr(sl.m_start_ptrs) {}
    //{{{
    int next() {
      ++mCurcount;
      ++mCurstart_ptr;
      return int(*mCurstart_ptr - m_covers);
      }
    //}}}

    int num_pix() const { return int(*mCurcount); }
    const uint8_t* covers() const { return *mCurstart_ptr; }

  private:
    const uint8_t* m_covers;
    const uint16_t* mCurcount;
    const uint8_t* const* mCurstart_ptr;
    };
  //}}}
  friend class iterator;

  //{{{
  cScanline() : mMinx(0), mMaxlen(0), m_dx(0), m_dy(0),
                m_last_x(0x7FFF), m_last_y(0x7FFF),
                m_covers(0), m_start_ptrs(0), m_counts(0),
                mNumspans(0), mCurstart_ptr(0), mCurcount(0) {}
  //}}}
  //{{{
  ~cScanline() {

    vPortFree (m_counts);
    vPortFree (m_start_ptrs);
    vPortFree (m_covers);
    }
  //}}}

  //{{{
  void reset (int min_x, int max_x, int dx, int dy) {

    unsigned max_len = max_x - min_x + 2;
    if (max_len > mMaxlen) {
      vPortFree (m_counts);
      vPortFree (m_start_ptrs);
      vPortFree (m_covers);
      m_covers = (uint8_t*)pvPortMalloc (max_len);
      m_start_ptrs = (uint8_t**)pvPortMalloc (max_len*4);
      m_counts = (uint16_t*)pvPortMalloc (max_len*2);
      mMaxlen = max_len;
      }

    m_dx = dx;
    m_dy = dy;
    m_last_x = 0x7FFF;
    m_last_y = 0x7FFF;
    mMinx = min_x;
    mCurcount = m_counts;
    mCurstart_ptr = m_start_ptrs;
    mNumspans = 0;
    }
  //}}}

  //{{{
  void resetSpans() {

    m_last_x        = 0x7FFF;
    m_last_y        = 0x7FFF;
    mCurcount     = m_counts;
    mCurstart_ptr = m_start_ptrs;
    mNumspans     = 0;
    }
  //}}}
  //{{{
  void addCell (int x, int y, unsigned cover) {

    x -= mMinx;
    m_covers[x] = (uint8_t)cover;

    if (x == m_last_x+1)
      (*mCurcount)++;
    else {
      *++mCurcount = 1;
      *++mCurstart_ptr = m_covers + x;
      mNumspans++;
      }

    m_last_x = x;
    m_last_y = y;
    }
  //}}}
  //{{{
  void addSpan (int x, int y, unsigned num, unsigned cover) {

    x -= mMinx;

    memset(m_covers + x, cover, num);
    if (x == m_last_x+1)
      (*mCurcount) += (uint16_t)num;
    else {
      *++mCurcount = (uint16_t)num;
      *++mCurstart_ptr = m_covers + x;
      mNumspans++;
      }

    m_last_x = x + num - 1;
    m_last_y = y;
    }
  //}}}

  //{{{
  int is_ready(int y) const {
    return mNumspans && (y ^ m_last_y);
    }
  //}}}
  int base_x() const { return mMinx + m_dx;  }
  int y() const { return m_last_y + m_dy; }
  unsigned num_spans() const { return mNumspans; }

private:
  cScanline (const cScanline&);
  const cScanline& operator = (const cScanline&);

private:
  int        mMinx;
  unsigned   mMaxlen;
  int        m_dx;
  int        m_dy;
  int        m_last_x;
  int        m_last_y;
  uint8_t*   m_covers;
  uint8_t**  m_start_ptrs;
  uint16_t*  m_counts;
  unsigned   mNumspans;
  uint8_t**  mCurstart_ptr;
  uint16_t*  mCurcount;
  };
//}}}

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
//{{{
class cPixelCell {
public:
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

  int16_t x;
  int16_t y;
  int packed_coord;
  int cover;
  int area;
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
      cPixelCell** ptr = mCells + mNumblocks - 1;
      while(mNumblocks--) {
        delete [] *ptr;
        ptr--;
        }
      delete [] mCells;
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
  const cPixelCell* const* cells() {

    if (m_flags & not_closed) {
      lineTo (m_close_x, m_close_y);
      m_flags &= ~not_closed;
      }

    // Perform sort only the first time.
    if(m_flags & sort_required) {
      add_cur_cell();
      if(mNumcells == 0)
        return 0;
      sort_cells();
      m_flags &= ~sort_required;
      }

    return mSortedcells;
    }
  //}}}

private:
  enum { qsort_threshold = 9 };
  enum { not_closed = 1, sort_required = 2 };
  enum { cell_block_shift = 12,
         cell_block_size = 1 << cell_block_shift,
         cell_block_mask = cell_block_size - 1,
         cell_block_pool = 256,
         cell_block_limit = 1024 };

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
      if ((mNumcells & cell_block_mask) == 0) {
        if (mNumblocks >= cell_block_limit)
          return;
        allocateBlock();
        }
      *mCurcell_ptr++ = mCurcell;
      mNumcells++;
      }
    }
  //}}}
  //{{{
  void sort_cells() {

    if (mNumcells == 0)
      return;

    if (mNumcells > mSortedsize) {
      vPortFree (mSortedcells);
      mSortedsize = mNumcells;
      mSortedcells = (cPixelCell**)pvPortMalloc ((mNumcells + 1) * 4);
      }

    cPixelCell** sorted_ptr = mSortedcells;
    cPixelCell** block_ptr = mCells;
    cPixelCell* cell_ptr;
    unsigned i;

    unsigned nb = mNumcells >> cell_block_shift;
    while (nb--) {
      cell_ptr = *block_ptr++;
      i = cell_block_size;
      while (i--)
        *sorted_ptr++ = cell_ptr++;
      }

    cell_ptr = *block_ptr++;
    i = mNumcells & cell_block_mask;
    while(i--)
      *sorted_ptr++ = cell_ptr++;
    mSortedcells[mNumcells] = 0;

    qsort_cells (mSortedcells, mNumcells);
    }
  //}}}

  //{{{
  void renderScanline (int ey, int x1, int y1, int x2, int y2) {

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
    //cScanline...
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

    // everything is on a single cScanline
    if (ey1 == ey2) {
      renderScanline (ey1, x1, fy1, x2, fy2);
      return;
      }

    // Vertical line - we have to calculate start and end cell
    // the common values of the area and coverage for all cells of the line.
    // We know exactly there's only one cell, so, we don't have to call renderScanline().
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

    // ok, we have to render several cScanlines
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
    renderScanline (ey1, x1, fy1, x_from, first);

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
        renderScanline (ey1, x_from, 0x100 - first, x_to, first);
        x_from = x_to;

        ey1 += incr;
        set_cur_cell (x_from >> 8, ey1);
        }
      }
    renderScanline (ey1, x_from, 0x100 - first, x2, fy2);
    }
  //}}}
  //{{{
  void allocateBlock() {

    if (mCurblock >= mNumblocks) {
      if (mNumblocks >= mMaxblocks) {
        cPixelCell** new_cells = new cPixelCell* [mMaxblocks + cell_block_pool];
        if (mCells) {
          memcpy (new_cells, mCells, mMaxblocks * sizeof(cPixelCell*));
          delete [] mCells;
          }
        mCells = new_cells;
        mMaxblocks += cell_block_pool;
        }
      mCells[mNumblocks++] = new cPixelCell [unsigned(cell_block_size)];
      }

    mCurcell_ptr = mCells[mCurblock++];
    }
  //}}}

  //{{{
  void qsort_cells (cPixelCell** start, unsigned num) {

    cPixelCell**  stack[80];
    cPixelCell*** top;
    cPixelCell**  limit;
    cPixelCell**  base;

    limit = start + num;
    base = start;
    top = stack;

    for (;;) {
      int len = int(limit - base);

      cPixelCell** i;
      cPixelCell** j;
      cPixelCell** pivot;

      if (len > qsort_threshold) {
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

  unsigned     mNumblocks;
  unsigned     mMaxblocks;
  unsigned     mCurblock;
  unsigned     mNumcells;
  cPixelCell** mCells;
  cPixelCell*  mCurcell_ptr;
  cPixelCell** mSortedcells;
  unsigned     mSortedsize;
  cPixelCell   mCurcell;
  int          mCurx;
  int          mCury;
  int          m_close_x;
  int          m_close_y;
  int          mMinx;
  int          mMiny;
  int          mMaxx;
  int          mMaxy;
  unsigned     m_flags;
  };
//}}}

//{{{
struct tRgba {
  enum order { rgb, bgr };

  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;

  tRgba() {}
  tRgba (uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_= 255) : r(r_), g(g_), b(b_), a(a_) {}

  //{{{
  //void opacity (double a_) {

    //if (a_ < 0.0)
      //a_ = 0.0;

    //if (a_ > 1.0)
      //a_ = 1.0;

    //a = uint8_t(a_ * 255.0);
    //}
  //}}}
  //double opacity() const { return double(a) / 255.0; }
  //{{{
  //tRgba gradient (tRgba c, double k) const {

    //tRgba ret;
    //int ik = int(k * 256);
    //ret.r = uint8_t (int(r) + (((int(c.r) - int(r)) * ik) >> 8));
    //ret.g = uint8_t (int(g) + (((int(c.g) - int(g)) * ik) >> 8));
    //ret.b = uint8_t (int(b) + (((int(c.b) - int(b)) * ik) >> 8));
    //ret.a = uint8_t (int(a) + (((int(c.a) - int(a)) * ik) >> 8));
    //return ret;
    //}
  //}}}
  //tRgba pre() const { return tRgba((r*a) >> 8, (g*a) >> 8, (b*a) >> 8, a); }
  };
//}}}
//{{{
struct tRgb565Span {
  //{{{
  static uint16_t rgb565 (unsigned r, unsigned g, unsigned b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }
  //}}}
  //{{{
  static tRgba getPixel (uint8_t* ptr, int x) {

    uint16_t rgb = ((uint16_t*)ptr)[x];

    tRgba c;
    c.r = (rgb >> 8) & 0xF8;
    c.g = (rgb >> 3) & 0xFC;
    c.b = (rgb << 3) & 0xF8;
    c.a = 255;

    return c;
    }
  //}}}
  //{{{
  static void hline (uint8_t* ptr, int x, unsigned count, const tRgba& c) {

    uint16_t* p = ((uint16_t*)ptr) + x;
    uint16_t v = rgb565 (c.r, c.g, c.b);
    do {
      *p++ = v;
      } while (--count);
    }
  //}}}
  //{{{
  static void render (uint8_t* ptr, int x, unsigned count, const uint8_t* covers, const tRgba& c) {

    uint16_t* p = ((uint16_t*)ptr) + x;
    do {
      int16_t rgb = *p;
      int alpha = (*covers++) * c.a;

      int r = (rgb >> 8) & 0xF8;
      int g = (rgb >> 3) & 0xFC;
      int b = (rgb << 3) & 0xF8;

      *p++ = (((((c.r - r) * alpha) + (r << 16)) >> 8) & 0xF800) |
             (((((c.g - g) * alpha) + (g << 16)) >> 13) & 0x7E0) |
              ((((c.b - b) * alpha) + (b << 16)) >> 19);
      } while (--count);
    }
  //}}}
  };
//}}}
//{{{
template <class cSpan> class cRenderer {
//{{{  description
// This class template is used basically for rendering cScanlines.
// The 'Span' argument is one of the span renderers, such as span_rgb24  and others.
//
// Usage:
//     // Creation
//     agg::cTarget rbuf(ptr, w, h, stride);
//     agg::renderer<agg::span_rgb24> ren(rbuf);
//     agg::cRasteriser ras;
//
//     // Clear the frame buffer
//     ren.clear(agg::tRgba(0,0,0));
//
//     // Making polygon
//     // ras.move_to(. . .);
//     // ras.line_to(. . .);
//     // . . .
//
//     // Rendering
//     ras.render(ren, agg::tRgba(200, 100, 80));
//}}}
public:
  cRenderer (cTarget& target) : mTarget (&target) {}

  //{{{
  tRgba getPixel (int x, int y) const {

   if (mTarget->inbox(x, y))
      return mSpan.getPixel (mTarget->row(y), x);

    return tRgba (0,0,0);
    }
  //}}}
  //{{{
  void render (const cScanline& sl, const tRgba& c) {

    if (sl.y() < 0 || sl.y() >= int(mTarget->height()))
      return;

    unsigned num_spans = sl.num_spans();
    int base_x = sl.base_x();
    uint8_t* row = mTarget->row (sl.y());
    cScanline::iterator span (sl);

    do {
      int x = span.next() + base_x;
      const uint8_t* covers = span.covers();
      int num_pix = span.num_pix();
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

      mSpan.render (row, x, num_pix, covers, c);
      } while (--num_spans);
    }
  //}}}
  //{{{
  void clear (const tRgba& c) {
    for (unsigned y = 0; y < mTarget->height(); y++)
      mSpan.hline (mTarget->row(y), 0, mTarget->width(), c);
    }
  //}}}
  //{{{
  void setPixel (int x, int y, const tRgba& c) {
    if (mTarget->inbox (x, y))
      mSpan.hline (mTarget->row(y), x, 1, c);
    }
  //}}}

private:
  cTarget* mTarget;
  cSpan    mSpan;
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

  cRasteriser() : mFilling (fill_even_odd) { gamma (1.2); }

  int getMinx() const { return mOutline.getMinx(); }
  int getMiny() const { return mOutline.getMiny(); }
  int getMaxx() const { return mOutline.getMaxx(); }
  int getMaxy() const { return mOutline.getMaxy(); }

  void reset() { mOutline.reset(); }
  void setFilling (eFilling filling) { mFilling = filling; }

  void moveTo (int x, int y) { mOutline.moveTo (x, y); }
  void lineTo (int x, int y) { mOutline.lineTo (x, y); }
  void moveTod (double x, double y) { mOutline.moveTo (int(x * 0x100), int(y * 0x100)); }
  void lineTod (double x, double y) { mOutline.lineTo (int(x * 0x100), int(y * 0x100)); }

  //{{{
  template<class cRenderer> void render (cRenderer& r, const tRgba& c, int dx = 0, int dy = 0) {

    const cPixelCell* const* cells = mOutline.cells();
    if (mOutline.numCells() == 0)
      return;

    int x, y;
    int cover;
    int alpha;
    int area;

    mScanline.reset (mOutline.getMinx(), mOutline.getMaxx(), dx, dy);

    cover = 0;
    const cPixelCell* cur_cell = *cells++;
    for(;;) {
      const cPixelCell* start_cell = cur_cell;

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
         if (mScanline.is_ready (y)) {
            r.render (mScanline, c);
            mScanline.resetSpans();
            }
          mScanline.addCell (x, y, mGamma[alpha]);
          }
        x++;
        }

      if (!cur_cell)
        break;

      if (cur_cell->x > x) {
        alpha = calcAlpha (cover << (8 + 1));
        if (alpha) {
          if (mScanline.is_ready (y)) {
            r.render (mScanline, c);
             mScanline.resetSpans();
             }
           mScanline.addSpan (x, y, cur_cell->x - x, mGamma[alpha]);
           }
        }
      }

    if (mScanline.num_spans())
      r.render (mScanline, c);
    }
  //}}}

  //{{{
  bool hit_test (int tx, int ty) {

    const cPixelCell* const* cells = mOutline.cells();
    if (mOutline.numCells() == 0)
      return false;

    int x, y;
    int cover;
    int alpha;
    int area;

    cover = 0;
    const cPixelCell* cur_cell = *cells++;
    for(;;) {
      const cPixelCell* start_cell = cur_cell;

      int coord  = cur_cell->packed_coord;
      x = cur_cell->x;
      y = cur_cell->y;

      if (y > ty)
        return false;

      area = start_cell->area;
      cover += start_cell->cover;
      while ((cur_cell = *cells++) != 0) {
        if (cur_cell->packed_coord != coord)
          break;
        area  += cur_cell->area;
        cover += cur_cell->cover;
        }

      if (area) {
        alpha = calcAlpha ((cover << (8 + 1)) - area);
        if (alpha)
          if (tx == x && ty == y)
            return true;
        x++;
        }

      if(!cur_cell)
        break;

      if (cur_cell->x > x) {
        alpha = calcAlpha (cover << (8 + 1));
        if (alpha)
         if (ty == y && tx >= x && tx <= cur_cell->x)
            return true;
        }
      }

    return false;
    }
  //}}}

private:
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

  cOutline  mOutline;
  cScanline mScanline;
  eFilling  mFilling;
  uint8_t  mGamma[256];
  };
//}}}
