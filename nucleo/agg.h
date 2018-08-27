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
      mBuf(buf), mRows(0), mWidth(width), mHeight(height), mMaxHeight(0) {

    if (height > mMaxHeight) {
      delete [] mRows;
      mRows = new uint8_t* [mMaxHeight = height];
      }

    uint8_t* row_ptr = mBuf;
    uint8_t** rows = mRows;
    while (height--) {
      *rows++ = row_ptr;
      row_ptr += width*2;
      }
    }
  //}}}
  ~cTarget() { delete [] mRows; }

  const uint8_t* buf()  const { return mBuf; }
  uint16_t width() const { return mWidth;  }
  uint16_t height() const { return mHeight; }

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
 //========================================================================
 //
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
 //-------------------------------------------------------------------------
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
 //------------------------------------------------------------------------
 //
 // The question is: why should we accumulate the whole cScanline when we
 // could render just separate spans when they're ready?
 // That's because using the scaline is in general faster. When is consists
 // of more than one span the conditions for the processor cash system
 // are better, because switching between two different areas of memory
 // (that can be large ones) occures less frequently.
 //------------------------------------------------------------------------
 //}}}
public:
  //{{{
  class iterator {
  public:
    iterator(const cScanline& sl) : m_covers(sl.m_covers), m_cur_count(sl.m_counts),
                                   m_cur_start_ptr(sl.m_start_ptrs) {}
    //{{{
    int next() {
      ++m_cur_count;
      ++m_cur_start_ptr;
      return int(*m_cur_start_ptr - m_covers);
      }
    //}}}

    int num_pix() const { return int(*m_cur_count); }
    const uint8_t* covers() const { return *m_cur_start_ptr; }

  private:
    const uint8_t*        m_covers;
    const uint16_t*       m_cur_count;
    const uint8_t* const* m_cur_start_ptr;
    };
  //}}}
  friend class iterator;

  //{{{
  cScanline() : m_min_x(0), m_max_len(0), m_dx(0), m_dy(0),
                m_last_x(0x7FFF), m_last_y(0x7FFF),
                m_covers(0), m_start_ptrs(0), m_counts(0),
                m_num_spans(0), m_cur_start_ptr(0), m_cur_count(0) {}
  //}}}
  //{{{
  ~cScanline() {

    delete [] m_counts;
    delete [] m_start_ptrs;
    delete [] m_covers;
    }
  //}}}

  //{{{
  void reset (int min_x, int max_x, int dx, int dy) {

    unsigned max_len = max_x - min_x + 2;
    if (max_len > m_max_len) {
      delete [] m_counts;
      delete [] m_start_ptrs;
      delete [] m_covers;
      m_covers = new uint8_t [max_len];
      m_start_ptrs = new uint8_t* [max_len];
      m_counts = new uint16_t[max_len];
      m_max_len = max_len;
      }

    m_dx = dx;
    m_dy = dy;
    m_last_x = 0x7FFF;
    m_last_y = 0x7FFF;
    m_min_x = min_x;
    m_cur_count = m_counts;
    m_cur_start_ptr = m_start_ptrs;
    m_num_spans = 0;
    }
  //}}}

  //{{{
  void resetSpans() {

    m_last_x        = 0x7FFF;
    m_last_y        = 0x7FFF;
    m_cur_count     = m_counts;
    m_cur_start_ptr = m_start_ptrs;
    m_num_spans     = 0;
    }
  //}}}
  //{{{
  void addCell (int x, int y, unsigned cover) {

    x -= m_min_x;
    m_covers[x] = (uint8_t)cover;

    if (x == m_last_x+1)
      (*m_cur_count)++;
    else {
      *++m_cur_count = 1;
      *++m_cur_start_ptr = m_covers + x;
      m_num_spans++;
      }

    m_last_x = x;
    m_last_y = y;
    }
  //}}}
  //{{{
  void addSpan (int x, int y, unsigned num, unsigned cover) {

    x -= m_min_x;

    memset(m_covers + x, cover, num);
    if (x == m_last_x+1)
      (*m_cur_count) += (uint16_t)num;
    else {
      *++m_cur_count = (uint16_t)num;
      *++m_cur_start_ptr = m_covers + x;
      m_num_spans++;
      }

    m_last_x = x + num - 1;
    m_last_y = y;
    }
  //}}}

  //{{{
  int is_ready(int y) const {
    return m_num_spans && (y ^ m_last_y);
    }
  //}}}
  int base_x() const { return m_min_x + m_dx;  }
  int y() const { return m_last_y + m_dy; }
  unsigned num_spans() const { return m_num_spans; }

private:
  cScanline (const cScanline&);
  const cScanline& operator = (const cScanline&);

private:
  int        m_min_x;
  unsigned   m_max_len;
  int        m_dx;
  int        m_dy;
  int        m_last_x;
  int        m_last_y;
  uint8_t*   m_covers;
  uint8_t**  m_start_ptrs;
  uint16_t*  m_counts;
  unsigned   m_num_spans;
  uint8_t**  m_cur_start_ptr;
  uint16_t*  m_cur_count;
  };
//}}}

//{{{
// These constants determine the subpixel accuracy, to be more precise,
// the number of bits of the fractional part of the coordinates.
// The possible coordinate capacity in bits can be calculated by formula:
// sizeof(int) * 8 - poly_base_shift * 2, i.e, for 32-bit integers and
// 8-bits fractional part the capacity is 16 bits or [-32768...32767].
enum { poly_base_shift = 8,
       poly_base_size = 1 << poly_base_shift,
       poly_base_mask = poly_base_size - 1
     };
//}}}
inline int poly_coord (double c) { return int(c * poly_base_size); }

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
struct tCell {
// A pixel cell. There're no constructors defined and it was done
// intentionally in order to avoid extra overhead when allocating an array of cells.
  int16_t x;
  int16_t y;
  int   packed_coord;
  int   cover;
  int   area;

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
  cOutline() : m_num_blocks(0), m_max_blocks(0), m_cur_block(0), m_num_cells(0), m_cells(0),
      m_cur_cell_ptr(0), m_sorted_cells(0), m_sorted_size(0), m_cur_x(0), m_cur_y(0),
      m_close_x(0), m_close_y(0), m_min_x(0x7FFFFFFF), m_min_y(0x7FFFFFFF), m_max_x(-0x7FFFFFFF),
      m_max_y(-0x7FFFFFFF), m_flags(sort_required) {

    m_cur_cell.set(0x7FFF, 0x7FFF, 0, 0);
    }
  //}}}
  //{{{
  ~cOutline() {

    delete [] m_sorted_cells;

    if (m_num_blocks) {
      tCell** ptr = m_cells + m_num_blocks - 1;
      while(m_num_blocks--) {
        delete [] *ptr;
        ptr--;
        }
      delete [] m_cells;
      }
    }
  //}}}

  //{{{
  void reset() {

    m_num_cells = 0;
    m_cur_block = 0;
    m_cur_cell.set (0x7FFF, 0x7FFF, 0, 0);
    m_flags |= sort_required;
    m_flags &= ~not_closed;
    m_min_x =  0x7FFFFFFF;
    m_min_y =  0x7FFFFFFF;
    m_max_x = -0x7FFFFFFF;
    m_max_y = -0x7FFFFFFF;
    }
  //}}}

  //{{{
  void moveTo (int x, int y) {

    if ((m_flags & sort_required) == 0)
      reset();

    if (m_flags & not_closed)
      lineTo (m_close_x, m_close_y);

    set_cur_cell (x >> poly_base_shift, y >> poly_base_shift);
    m_close_x = m_cur_x = x;
    m_close_y = m_cur_y = y;
    }
  //}}}
  //{{{
  void lineTo (int x, int y) {

    if((m_flags & sort_required) && ((m_cur_x ^ x) | (m_cur_y ^ y))) {
      int c = m_cur_x >> poly_base_shift;
      if (c < m_min_x)
        m_min_x = c;
      ++c;
      if (c > m_max_x)
        m_max_x = c;

      c = x >> poly_base_shift;
      if (c < m_min_x)
        m_min_x = c;
      ++c;
      if (c > m_max_x)
        m_max_x = c;

      renderLine (m_cur_x, m_cur_y, x, y);
      m_cur_x = x;
      m_cur_y = y;
      m_flags |= not_closed;
      }
    }
  //}}}

  int min_x() const { return m_min_x; }
  int min_y() const { return m_min_y; }
  int max_x() const { return m_max_x; }
  int max_y() const { return m_max_y; }

  unsigned num_cells() const {return m_num_cells; }
  //{{{
  const tCell* const* cells() {

    if (m_flags & not_closed) {
      lineTo (m_close_x, m_close_y);
      m_flags &= ~not_closed;
      }

    // Perform sort only the first time.
    if(m_flags & sort_required) {
      add_cur_cell();
      if(m_num_cells == 0)
        return 0;
      sort_cells();
      m_flags &= ~sort_required;
      }

    return m_sorted_cells;
    }
  //}}}

private:
  enum { qsort_threshold = 9 };
  enum { not_closed = 1, sort_required = 2 };
  enum { cell_block_shift = 12,
         cell_block_size  = 1 << cell_block_shift,
         cell_block_mask  = cell_block_size - 1,
         cell_block_pool  = 256,
         cell_block_limit = 1024 };

  cOutline (const cOutline&);
  const cOutline& operator = (const cOutline&);

  //{{{
  void set_cur_cell (int x, int y) {

    if (m_cur_cell.packed_coord != (y << 16) + x) {
      add_cur_cell();
      m_cur_cell.set (x, y, 0, 0);
      }
   }
  //}}}
  //{{{
  void add_cur_cell() {

    if (m_cur_cell.area | m_cur_cell.cover) {
      if ((m_num_cells & cell_block_mask) == 0) {
        if (m_num_blocks >= cell_block_limit)
          return;
        allocateBlock();
        }
      *m_cur_cell_ptr++ = m_cur_cell;
      m_num_cells++;
      }
    }
  //}}}
  //{{{
  void sort_cells() {

    if (m_num_cells == 0)
      return;

    if (m_num_cells > m_sorted_size) {
      delete [] m_sorted_cells;
      m_sorted_size = m_num_cells;
      m_sorted_cells = new tCell* [m_num_cells + 1];
      }

    tCell** sorted_ptr = m_sorted_cells;
    tCell** block_ptr = m_cells;
    tCell*  cell_ptr;

    unsigned nb = m_num_cells >> cell_block_shift;
    unsigned i;

    while (nb--) {
      cell_ptr = *block_ptr++;
      i = cell_block_size;
      while (i--)
        *sorted_ptr++ = cell_ptr++;
      }

    cell_ptr = *block_ptr++;
    i = m_num_cells & cell_block_mask;
    while(i--)
      *sorted_ptr++ = cell_ptr++;
    m_sorted_cells[m_num_cells] = 0;

    qsort_cells (m_sorted_cells, m_num_cells);
    }
  //}}}

  //{{{
  void renderScanline (int ey, int x1, int y1, int x2, int y2) {

    int ex1 = x1 >> poly_base_shift;
    int ex2 = x2 >> poly_base_shift;
    int fx1 = x1 & poly_base_mask;
    int fx2 = x2 & poly_base_mask;

    int delta, p, first, dx;
    int incr, lift, mod, rem;

    // trivial case. Happens often
    if (y1 == y2) {
      set_cur_cell (ex2, ey);
      return;
      }

    //everything is located in a single cell.  That is easy!
    if (ex1 == ex2) {
      delta = y2 - y1;
      m_cur_cell.add_cover(delta, (fx1 + fx2) * delta);
      return;
      }

    //ok, we'll have to render a run of adjacent cells on the same
    //cScanline...
    p = (poly_base_size - fx1) * (y2 - y1);
    first = poly_base_size;
    incr = 1;
    dx = x2 - x1;
    if (dx < 0) {
      p     = fx1 * (y2 - y1);
      first = 0;
      incr  = -1;
      dx    = -dx;
      }

    delta = p / dx;
    mod   = p % dx;
    if (mod < 0) {
      delta--;
      mod += dx;
      }

    m_cur_cell.add_cover(delta, (fx1 + first) * delta);

    ex1 += incr;
    set_cur_cell(ex1, ey);
    y1  += delta;
    if (ex1 != ex2) {
      p =  poly_base_size * (y2 - y1 + delta);
      lift = p / dx;
      rem = p % dx;
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

        m_cur_cell.add_cover(delta, (poly_base_size) * delta);
        y1  += delta;
        ex1 += incr;
        set_cur_cell(ex1, ey);
        }
      }
    delta = y2 - y1;
    m_cur_cell.add_cover(delta, (fx2 + poly_base_size - first) * delta);
    }
  //}}}
  //{{{
  void renderLine (int x1, int y1, int x2, int y2) {

    int ey1 = y1 >> poly_base_shift;
    int ey2 = y2 >> poly_base_shift;
    int fy1 = y1 & poly_base_mask;
    int fy2 = y2 & poly_base_mask;

    int dx, dy, x_from, x_to;
    int p, rem, mod, lift, delta, first, incr;

    if (ey1   < m_min_y)
      m_min_y = ey1;
    if (ey1+1 > m_max_y)
      m_max_y = ey1+1;
    if (ey2   < m_min_y)
      m_min_y = ey2;
    if (ey2+1 > m_max_y)
      m_max_y = ey2+1;

    dx = x2 - x1;
    dy = y2 - y1;

    // everything is on a single cScanline
    if (ey1 == ey2) {
      renderScanline(ey1, x1, fy1, x2, fy2);
      return;
      }

    // Vertical line - we have to calculate start and end cells,
    // and then - the common values of the area and coverage for
    // all cells of the line. We know exactly there's only one
    // cell, so, we don't have to call renderScanline().
    incr  = 1;
    if (dx == 0) {
      int ex = x1 >> poly_base_shift;
      int two_fx = (x1 - (ex << poly_base_shift)) << 1;
      int area;

      first = poly_base_size;
      if(dy < 0) {
        first = 0;
        incr  = -1;
        }

      x_from = x1;

      //renderScanline(ey1, x_from, fy1, x_from, first)
      delta = first - fy1;
      m_cur_cell.add_cover (delta, two_fx * delta);

      ey1 += incr;
      set_cur_cell(ex, ey1);

      delta = first + first - poly_base_size;
      area = two_fx * delta;
      while (ey1 != ey2) {
        //renderScanline (ey1, x_from, poly_base_size - first, x_from, first);
        m_cur_cell.set_cover (delta, area);
        ey1 += incr;
        set_cur_cell(ex, ey1);
        }

      // renderScanline(ey1, x_from, poly_base_size - first, x_from, fy2);
      delta = fy2 - poly_base_size + first;
      m_cur_cell.add_cover (delta, two_fx * delta);
      return;
     }

    // ok, we have to render several cScanlines
    p  = (poly_base_size - fy1) * dx;
    first = poly_base_size;
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
    set_cur_cell (x_from >> poly_base_shift, ey1);

    if (ey1 != ey2) {
      p = poly_base_size * dx;
      lift  = p / dy;
      rem   = p % dy;

      if (rem < 0) {
        lift--;
        rem += dy;
        }
      mod -= dy;
      while(ey1 != ey2) {
        delta = lift;
        mod  += rem;
        if (mod >= 0) {
          mod -= dy;
          delta++;
          }

        x_to = x_from + delta;
        renderScanline (ey1, x_from, poly_base_size - first, x_to, first);
        x_from = x_to;

        ey1 += incr;
        set_cur_cell (x_from >> poly_base_shift, ey1);
        }
      }
    renderScanline (ey1, x_from, poly_base_size - first, x2, fy2);
    }
  //}}}
  //{{{
  void allocateBlock() {

    if (m_cur_block >= m_num_blocks) {
      if (m_num_blocks >= m_max_blocks) {
        tCell** new_cells = new tCell* [m_max_blocks + cell_block_pool];
        if (m_cells) {
          memcpy(new_cells, m_cells, m_max_blocks * sizeof(tCell*));
          delete [] m_cells;
          }
        m_cells = new_cells;
        m_max_blocks += cell_block_pool;
        }
      m_cells[m_num_blocks++] = new tCell [unsigned(cell_block_size)];
      }

    m_cur_cell_ptr = m_cells[m_cur_block++];
    }
  //}}}

  //{{{
  void qsort_cells (tCell** start, unsigned num) {

    tCell**  stack[80];
    tCell*** top;
    tCell**  limit;
    tCell**  base;

    limit = start + num;
    base = start;
    top = stack;

    for (;;) {
      int len = int(limit - base);

      tCell** i;
      tCell** j;
      tCell** pivot;

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

  unsigned  m_num_blocks;
  unsigned  m_max_blocks;
  unsigned  m_cur_block;
  unsigned  m_num_cells;
  tCell**   m_cells;
  tCell*    m_cur_cell_ptr;
  tCell**   m_sorted_cells;
  unsigned  m_sorted_size;
  tCell     m_cur_cell;
  int       m_cur_x;
  int       m_cur_y;
  int       m_close_x;
  int       m_close_y;
  int       m_min_x;
  int       m_min_y;
  int       m_max_x;
  int       m_max_y;
  unsigned  m_flags;
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
  //{{{
  tRgba (unsigned r_, unsigned g_, unsigned b_, unsigned a_= 255) :
      r(uint8_t(r_)), g(uint8_t(g_)), b(uint8_t(b_)), a(uint8_t(a_)) {}
  //}}}
  //{{{
  tRgba (unsigned packed, order o) :
    r((o == rgb) ? ((packed >> 16) & 0xFF) : (packed & 0xFF)),
    g((packed >> 8)  & 0xFF),
    b((o == rgb) ? (packed & 0xFF) : ((packed >> 16) & 0xFF)),
    a(255) {}
  //}}}

  //{{{
  void opacity (double a_) {

    if (a_ < 0.0)
      a_ = 0.0;

    if (a_ > 1.0)
      a_ = 1.0;

    a = uint8_t(a_ * 255.0);
    }
  //}}}
  double opacity() const { return double(a) / 255.0; }

  //{{{
  tRgba gradient (tRgba c, double k) const {

    tRgba ret;
    int ik = int(k * 256);
    ret.r = uint8_t (int(r) + (((int(c.r) - int(r)) * ik) >> 8));
    ret.g = uint8_t (int(g) + (((int(c.g) - int(g)) * ik) >> 8));
    ret.b = uint8_t (int(b) + (((int(c.b) - int(b)) * ik) >> 8));
    ret.a = uint8_t (int(a) + (((int(c.a) - int(a)) * ik) >> 8));
    return ret;
    }
  //}}}

  tRgba pre() const { return tRgba((r*a) >> 8, (g*a) >> 8, (b*a) >> 8, a); }
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
// and 8 bits for fractional - see poly_base_shift. This class can be
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
  enum {
    aa_shift = 8,
    aa_num   = 1 << aa_shift,
    aa_mask  = aa_num - 1,
    aa_2num  = aa_num * 2,
    aa_2mask = aa_2num - 1
    };

  cRasteriser() : mFilling(fill_even_odd) { gamma (1.2); }

  void reset() { mOutline.reset(); }
  void filling_rule (eFilling filling) { mFilling = filling; }

  //{{{
  void gamma (double g) {

    for (unsigned i = 0; i < 256; i++)
      mGamma[i] = (uint8_t)(pow(double(i) / 255.0, g) * 255.0);
    }
  //}}}

  void moveTo (int x, int y) { mOutline.moveTo (x, y); }
  void lineTo (int x, int y) { mOutline.lineTo (x, y); }
  void moveTod (double x, double y) { mOutline.moveTo (poly_coord(x), poly_coord(y)); }
  void lineTod (double x, double y) { mOutline.lineTo (poly_coord(x), poly_coord(y)); }

  int min_x() const { return mOutline.min_x(); }
  int min_y() const { return mOutline.min_y(); }
  int max_x() const { return mOutline.max_x(); }
  int max_y() const { return mOutline.max_y(); }

  //{{{
  unsigned calcAlpha (int area) const {

    int cover = area >> (poly_base_shift*2 + 1 - aa_shift);
    if (cover < 0)
      cover = -cover;

    if (mFilling == fill_even_odd) {
      cover &= aa_2mask;
      if (cover > aa_num)
        cover = aa_2num - cover;
      }

    if (cover > aa_mask)
      cover = aa_mask;

    return cover;
    }
  //}}}

  //{{{
  template<class cRenderer> void render (cRenderer& r, const tRgba& c, int dx = 0, int dy = 0) {

    const tCell* const* cells = mOutline.cells();
    if (mOutline.num_cells() == 0)
      return;

    int x, y;
    int cover;
    int alpha;
    int area;

    mScanline.reset (mOutline.min_x(), mOutline.max_x(), dx, dy);

    cover = 0;
    const tCell* cur_cell = *cells++;
    for(;;) {
      const tCell* start_cell = cur_cell;

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
        alpha = calcAlpha ((cover << (poly_base_shift + 1)) - area);
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
        alpha = calcAlpha (cover << (poly_base_shift + 1));
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

    const tCell* const* cells = mOutline.cells();
    if (mOutline.num_cells() == 0)
      return false;

    int x, y;
    int cover;
    int alpha;
    int area;

    cover = 0;
    const tCell* cur_cell = *cells++;
    for(;;) {
      const tCell* start_cell = cur_cell;

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
        alpha = calcAlpha ((cover << (poly_base_shift + 1)) - area);
        if (alpha)
          if (tx == x && ty == y)
            return true;
        x++;
        }

      if(!cur_cell)
        break;

      if (cur_cell->x > x) {
        alpha = calcAlpha (cover << (poly_base_shift + 1));
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

  cOutline  mOutline;
  cScanline mScanline;
  eFilling  mFilling;
  uint8_t  mGamma[256];
  };
//}}}
