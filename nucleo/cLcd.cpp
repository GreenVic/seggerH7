// cLcd.cpp
//{{{  includes
#include "cLcd.h"
#include "../freetype/FreeSansBold.h"
#include "cpuUsage.h"
//}}}
//{{{  screen resolution defines
#ifdef NEXXY_SCREEN
  // NEXXY 7 inch
  #define LTDC_CLOCK_4      130  // 32.5Mhz
  #define HORIZ_SYNC         64
  #define VERT_SYNC           1
#else
  // ASUS eee 10 inch
  #define LTDC_CLOCK_4      100  // 25Mhz
  #define HORIZ_SYNC        136  // min  136  typ 176   max 216
  #define VERT_SYNC          12  // min   12  typ  25   max  38
#endif
//}}}

//{{{  static var inits
cLcd* cLcd::mLcd = nullptr;

uint32_t cLcd::mShowBuffer = 0;

cLcd::eDma2dWait cLcd::mDma2dWait = eWaitNone;
SemaphoreHandle_t cLcd::mDma2dSem;

SemaphoreHandle_t cLcd::mFrameSem;
//}}}
DMA2D_HandleTypeDef DMA2D_Handle;

//{{{
extern "C" {void LTDC_IRQHandler() {

  // line Interrupt
  if ((LTDC->ISR & LTDC_FLAG_LI) != RESET) {
    LTDC->IER &= ~(LTDC_IT_TE | LTDC_IT_FU | LTDC_IT_LI);
    LTDC->ICR = LTDC_FLAG_LI;

    LTDC_Layer1->CFBAR = cLcd::mShowBuffer;
    LTDC->SRCR = LTDC_SRCR_IMR;

    portBASE_TYPE taskWoken = pdFALSE;
    if (xSemaphoreGiveFromISR (cLcd::mFrameSem, &taskWoken) == pdTRUE)
      portEND_SWITCHING_ISR (taskWoken);
    }

  // register reload Interrupt
  if ((LTDC->ISR & LTDC_FLAG_RR) != RESET) {
    LTDC->IER &= ~LTDC_FLAG_RR;
    LTDC->ICR = LTDC_FLAG_RR;
    //cLcd::mLcd->debug (LCD_COLOR_YELLOW, "ltdc reload IRQ");
    }
  }
}
//}}}
//{{{
extern "C" {void LTDC_ER_IRQHandler() {

  // transfer Error Interrupt
  if ((LTDC->ISR &  LTDC_FLAG_TE) != RESET) {
    LTDC->IER &= ~(LTDC_IT_TE | LTDC_IT_FU | LTDC_IT_LI);
    LTDC->ICR = LTDC_IT_TE;
    cLcd::mLcd->info (COL_RED, "ltdc te IRQ");
    }

  // FIFO underrun Interrupt
  if ((LTDC->ISR &  LTDC_FLAG_FU) != RESET) {
    LTDC->IER &= ~(LTDC_IT_TE | LTDC_IT_FU | LTDC_IT_LI);
    LTDC->ICR = LTDC_FLAG_FU;
    cLcd::mLcd->info (COL_RED, "ltdc fifoUnderrun IRQ");
    }
  }
}
//}}}
//{{{
extern "C" {void DMA2D_IRQHandler() {

  if (DMA2D->ISR & DMA2D_FLAG_TC) {
    DMA2D->IFCR = DMA2D_FLAG_TC;

    portBASE_TYPE taskWoken = pdFALSE;
    if (xSemaphoreGiveFromISR (cLcd::mDma2dSem, &taskWoken) == pdTRUE)
      portEND_SWITCHING_ISR (taskWoken);
    }
  }
}
//}}}

//{{{
class cFontChar {
public:
  uint8_t* bitmap;
  int16_t left;
  int16_t top;
  int16_t pitch;
  int16_t rows;
  int16_t advance;
  };
//}}}

//{{{
cLcd::cLcd (uint16_t* buffer0, uint16_t* buffer1)  {

  mBuffer[0] = buffer0;
  mBuffer[1] = buffer1;
  mLcd = this;

  // consts
  rectRegs[0] = DMA2D_OUTPUT_RGB565;

  stampRegs[1] = 0;
  stampRegs[6] = DMA2D_INPUT_RGB565;
  stampRegs[7] = 0;
  stampRegs[8] = 0;
  stampRegs[9] = 0;
  stampRegs[10] = DMA2D_OUTPUT_RGB565;
  stampRegs[11] = 0;
  }
//}}}
//{{{
cLcd::~cLcd() {
  FT_Done_Face (FTface);
  FT_Done_FreeType (FTlibrary);
  }
//}}}
//{{{
void cLcd::init (const std::string& title) {

  FT_Init_FreeType (&FTlibrary);
  FT_New_Memory_Face (FTlibrary, (FT_Byte*)freeSansBold, sizeof (freeSansBold), 0, &FTface);
  FTglyphSlot = FTface->glyph;

  mTitle = title;

  vSemaphoreCreateBinary (mFrameSem);
  ltdcInit (mBuffer[mDrawBuffer]);

  vSemaphoreCreateBinary (mDma2dSem);
  HAL_NVIC_SetPriority (DMA2D_IRQn, 0x0F, 0);
  HAL_NVIC_EnableIRQ (DMA2D_IRQn);
  }
//}}}

//{{{
bool cLcd::changed() {
  bool wasChanged = mChanged;
  mChanged = false;
  return wasChanged;
  }
//}}}
//{{{
void cLcd::toggle() {
  mShowInfo = !mShowInfo;
  change();
  }
//}}}

//{{{
void cLcd::info (uint16_t colour, const std::string str) {

  uint16_t line = mCurLine++ % kMaxLines;
  mLines[line].mTime = HAL_GetTick();
  mLines[line].mColour = colour;
  mLines[line].mString = str;

  mChanged = true;
  }
//}}}

//{{{
void cLcd::clear (uint16_t colour) {
  cRect r (getSize());
  rect (colour, r);
  }
//}}}
//{{{
void cLcd::rect (uint16_t colour, const cRect& r) {
//__IO uint32_t OPFCCR  Output PFC Control Register,    Address offset: 0x34
//__IO uint32_t OCOLR   Output Color Register,          Address offset: 0x38
//__IO uint32_t OMAR    Output Memory Address Register, Address offset: 0x3C
//__IO uint32_t OOR     Output Offset Register,         Address offset: 0x40
//__IO uint32_t NLR     Number of Line Register,        Address offset: 0x44

  rectRegs[1] = colour;
  rectRegs[2] = uint32_t (mBuffer[mDrawBuffer] + r.top * getWidth() + r.left);
  rectRegs[3] = getWidth() - r.getWidth();
  rectRegs[4] = (r.getWidth() << 16) | r.getHeight();
  ready();
  memcpy ((void*)(&DMA2D->OPFCCR), rectRegs, 5*4);

  DMA2D->CR = DMA2D_R2M | DMA2D_CR_START | DMA2D_CR_TCIE | DMA2D_CR_TEIE | DMA2D_CR_CEIE;
  mDma2dWait = eWaitIrq;
  }
//}}}
//{{{
void cLcd::rectClipped (uint16_t colour, cRect r) {

  if (r.right <= 0)
    return;
  if (r.bottom <= 0)
    return;

  if (r.left >= getWidth())
    return;
  if (r.left < 0)
    r.left = 0;
  if (r.right > getWidth())
    r.right = getWidth();
  if (r.right <= r.left)
    return;

  if (r.top >= getHeight())
    return;
  if (r.top < 0)
    r.top = 0;
  if (r.bottom > getHeight())
    r.bottom = getHeight();
  if (r.bottom <= r.top)
    return;

  rect (colour, r);
  }
//}}}
//{{{
void cLcd::rectOutline (uint16_t colour, const cRect& r, uint8_t thickness) {

  rectClipped (colour, cRect (r.left, r.top, r.right, r.top+thickness));
  rectClipped (colour, cRect (r.right-thickness, r.top, r.right, r.bottom));
  rectClipped (colour, cRect (r.left, r.bottom-thickness, r.right, r.bottom));
  rectClipped (colour, cRect (r.left, r.top, r.left+thickness, r.bottom));
  }
//}}}
//{{{
void cLcd::stamp (uint16_t colour, uint8_t* src, const cRect& r) {
//__IO uint32_t FGMAR;    Foreground Memory Address Register,       Address offset: 0x0C
//__IO uint32_t FGOR;     Foreground Offset Register,               Address offset: 0x10
//__IO uint32_t BGMAR;    Background Memory Address Register,       Address offset: 0x14
//__IO uint32_t BGOR;     Background Offset Register,               Address offset: 0x18
//__IO uint32_t FGPFCCR;  Foreground PFC Control Register,          Address offset: 0x1C
//__IO uint32_t FGCOLR;   Foreground Color Register,                Address offset: 0x20
//__IO uint32_t BGPFCCR;  Background PFC Control Register,          Address offset: 0x24
//__IO uint32_t BGCOLR;   Background Color Register,                Address offset: 0x28
//__IO uint32_t FGCMAR;   Foreground CLUT Memory Address Register,  Address offset: 0x2C
//__IO uint32_t BGCMAR;   Background CLUT Memory Address Register,  Address offset: 0x30
//__IO uint32_t OPFCCR;   Output PFC Control Register,              Address offset: 0x34
//__IO uint32_t OCOLR;    Output Color Register,                    Address offset: 0x38
//__IO uint32_t OMAR;     Output Memory Address Register,           Address offset: 0x3C
//__IO uint32_t OOR;      Output Offset Register,                   Address offset: 0x40
//__IO uint32_t NLR;      Number of Line Register,                  Address offset: 0x44
//{{{  alternative code
//  uint32_t address = mBuffer[mDrawBuffer] + y * getWidth()) + x;
//  uint32_t col = ((colour & 0xF800) << 8) | ((colour & 0x07E0) << 5) | ((colour & 0x001F) << 3);
//  uint32_t stride = getWidth() - width;
//  uint32_t nlr = (width << 16) | height;
//  ready();
//  DMA2D->FGMAR   = (uint32_t)src;  // fgnd start address
//  DMA2D->FGOR    = 0;              // fgnd stride
//  DMA2D->BGMAR   = address;        // - repeated to bgnd start addres
//  DMA2D->BGOR    = stride;         // - repeated to bgnd stride
//  DMA2D->FGPFCCR = DMA2D_INPUT_A8; // fgnd PFC
//  DMA2D->FGCOLR  = col;
//  DMA2D->BGPFCCR = DMA2D_RGB565;
//  DMA2D->OPFCCR  = DMA2D_RGB565;
//  DMA2D->OMAR    = address;        // output start address
//  DMA2D->OOR     = stride;         // output stride
//  DMA2D->NLR     = nlr;            //  width:height
//  DMA2D->CR = DMA2D_M2M_BLEND | DMA2D_CR_TCIE | DMA2D_CR_TEIE | DMA2D_CR_CEIE | DMA2D_CR_START;
//}}}

  stampRegs[0] = (uint32_t)src;
  stampRegs[2] = uint32_t(mBuffer[mDrawBuffer] + r.top * getWidth() + r.left);
  stampRegs[3] = getWidth() - r.getWidth();
  stampRegs[4] = DMA2D_INPUT_A8;
  stampRegs[5] = ((colour & 0xF800) << 8) | ((colour & 0x07E0) << 5) | ((colour & 0x001F) << 3);
  stampRegs[12] = stampRegs[2];
  stampRegs[13] = stampRegs[3];
  stampRegs[14] = (r.getWidth() << 16) | r.getHeight();

  ready();
  memcpy ((void*)(&DMA2D->FGMAR), stampRegs, 15*4);
  DMA2D->CR = DMA2D_M2M_BLEND | DMA2D_CR_START | DMA2D_CR_TCIE | DMA2D_CR_TEIE | DMA2D_CR_CEIE;
  mDma2dWait = eWaitIrq;
  }
//}}}
//{{{
void cLcd::stampClipped (uint16_t colour, uint8_t* src, cRect r) {

  if (!r.getWidth())
    return;
  if (!r.getHeight())
    return;

  if (r.left < 0)
    return;

  if (r.top < 0) {
    // top clip
    if (r.bottom <= 0)
      return;

    r.bottom = getHeight();
    src += -r.top * r.getWidth();
    r.top = 0;
    }

  if (r.bottom > getHeight()) {
    // bottom yclip
    if (r.top >= getHeight())
      return;
    r.bottom = getHeight();
    }

  stamp (colour, src, r);
  }
//}}}
//{{{
void cLcd::copy (cTile* srcTile, cPoint p) {

  uint16_t width = p.x + srcTile->mWidth > getWidth() ? getWidth() - p.x : srcTile->mWidth;
  uint16_t height = p.y + srcTile->mHeight > getHeight() ? getHeight() - p.y : srcTile->mHeight;

  ready();
  DMA2D->FGPFCCR = srcTile->mFormat;
  DMA2D->FGMAR = (uint32_t)srcTile->mPiccy;
  DMA2D->FGOR = srcTile->mPitch - width;
  DMA2D->OPFCCR = DMA2D_OUTPUT_RGB565;
  DMA2D->OMAR = uint32_t(mBuffer[mDrawBuffer] + p.y * getWidth() + p.x);
  DMA2D->OOR = getWidth() - srcTile->mWidth;
  DMA2D->NLR = (width << 16) | height;
  DMA2D->CR = DMA2D_M2M_PFC | DMA2D_CR_START | DMA2D_CR_TCIE | DMA2D_CR_TEIE | DMA2D_CR_CEIE;
  mDma2dWait = eWaitIrq;
  }
//}}}
//{{{
void cLcd::copy90 (cTile* srcTile, cPoint p) {

  uint32_t src = (uint32_t)srcTile->mPiccy;
  uint32_t dst = (uint32_t)mBuffer[mDrawBuffer];

  ready();
  DMA2D->FGPFCCR = srcTile->mFormat;
  DMA2D->FGOR = 0;
  DMA2D->OPFCCR = DMA2D_OUTPUT_RGB565;
  DMA2D->OOR = getWidth() - 1;
  DMA2D->NLR = 0x10000 | (srcTile->mWidth);

  for (int line = 0; line < srcTile->mHeight; line++) {
    DMA2D->FGMAR = src;
    DMA2D->OMAR = dst;
    DMA2D->CR = DMA2D_M2M_PFC | DMA2D_CR_START | DMA2D_CR_TCIE | DMA2D_CR_TEIE | DMA2D_CR_CEIE;
    mDma2dWait = eWaitIrq;

    src += srcTile->mWidth * srcTile->mComponents;
    dst += 2;

    mDma2dWait = eWaitDone;
    ready();
    }
  }
//}}}
//{{{
void cLcd::size (cTile* srcTile, const cRect& r) {

  uint32_t xStep16 = ((srcTile->mWidth - 1) << 16) / (r.getWidth() - 1);
  uint32_t yStep16 = ((srcTile->mHeight - 1) << 16) / (r.getHeight() - 1);

  auto dstPtr = mBuffer[mDrawBuffer] + r.top * getWidth() + r.left;

  if (srcTile->mComponents == 2) {
    auto srcBase = (uint16_t*)(srcTile->mPiccy) + (srcTile->mY * srcTile->mPitch) + srcTile->mX;
    for (uint32_t y16 = (srcTile->mY << 16); y16 < ((srcTile->mY + r.getHeight()) * yStep16); y16 += yStep16) {
      auto src11 = srcBase + (y16 >> 16) * srcTile->mPitch;
      for (uint32_t x16 = srcTile->mX << 16; x16 < (srcTile->mX + r.getWidth()) * xStep16; x16 += xStep16)
        *dstPtr++ = *(src11 + (x16 >> 16));
      dstPtr += getWidth() - r.getWidth();
      }
    }

  else {
    auto srcBase = (uint8_t*)(srcTile->mPiccy + ((srcTile->mY * srcTile->mPitch) + srcTile->mX) * srcTile->mComponents);
    for (uint32_t y16 = (srcTile->mY << 16); y16 < ((srcTile->mY + r.getHeight()) * yStep16); y16 += yStep16) {
      auto srcy = srcBase + ((y16 >> 16) * srcTile->mPitch) * srcTile->mComponents;
      for (uint32_t x16 = srcTile->mX << 16; x16 < (srcTile->mX + r.getWidth()) * xStep16; x16 += xStep16) {
        auto src11 = srcy + (x16 >> 16) * srcTile->mComponents;
        *dstPtr++ = ((*src11++) >> 3) | (((*src11++) & 0xFC) << 3) | (((*src11) & 0xF8) << 8);
        }
      dstPtr += getWidth() - r.getWidth();
      }
    }
  }
//}}}
//{{{
void cLcd::sizeBi (cTile* srcTile, const cRect& r) {
// only for src->components = 3,4

  uint32_t xStep16 = ((srcTile->mWidth - 1) << 16) / (r.getWidth() - 1);
  uint32_t yStep16 = ((srcTile->mHeight - 1) << 16) / (r.getHeight() - 1);

  uint16_t* dstPtr = mBuffer[mDrawBuffer] + r.top * getWidth() + r.left;

  uint32_t ySrcOffset = srcTile->mPitch * srcTile->mComponents;
  for (uint32_t y16 = (srcTile->mY << 16); y16 < (srcTile->mY + r.getHeight()) * yStep16; y16 += yStep16) {
    uint8_t yweight2 = (y16 >> 9) & 0x7F;
    uint8_t yweight1 = 0x80 - yweight2;
    const uint8_t* srcy = (uint8_t*)srcTile->mPiccy + ((y16 >> 16) * ySrcOffset) + (srcTile->mX * srcTile->mComponents);
    for (uint32_t x16 = srcTile->mX << 16; x16 < (srcTile->mX + r.getWidth()) * xStep16; x16 += xStep16) {
      uint8_t xweight2 = (x16 >> 9) & 0x7F;
      uint8_t xweight1 = 0x80 - xweight2;
      const uint8_t* src11 = srcy + (x16 >> 16) * srcTile->mComponents;
      const uint8_t* src12 = src11 + srcTile->mComponents;
      const uint8_t* src21 = src11 + ySrcOffset;
      const uint8_t* src22 = src21 + srcTile->mComponents;

      *dstPtr++ = ((((*src11++ * xweight1 + *src12++ * xweight2) * yweight1) +
                     (*src21++ * xweight1 + *src22++ * xweight2) * yweight2) >> 17) |
                  (((((*src11++ * xweight1 + *src12++ * xweight2) * yweight1) +
                      (*src21++ * xweight1 + *src22++ * xweight2) * yweight2) >> 11) & 0x07E0) |
                  (((((*src11 * xweight1 + *src12 * xweight2) * yweight1) +
                      (*src21 * xweight1 + *src22 * xweight2) * yweight2) >> 6) & 0xF800);
      }
    }
  }
//}}}
//{{{
void cLcd::pixel (uint16_t colour, cPoint p) {
  *(mBuffer[mDrawBuffer] + p.y * getWidth() + p.x) = colour;
  }
//}}}
//{{{
void cLcd::ellipse (uint16_t colour, cPoint centre, cPoint radius) {

  if (!radius.x)
    return;
  if (!radius.y)
    return;

  int x1 = 0;
  int y1 = -radius.x;
  int err = 2 - 2*radius.x;
  float k = (float)radius.y / radius.x;

  do {
    rectClipped (colour, cRect (centre.x-(uint16_t)(x1 / k), centre.y + y1,
                                centre.x-(uint16_t)(x1 / k) + 2*(uint16_t)(x1 / k) + 1, centre.y  + y1 + 1));
    rectClipped (colour, cRect (centre.x-(uint16_t)(x1 / k), centre.y  - y1,
                                centre.x-(uint16_t)(x1 / k) + 2*(uint16_t)(x1 / k) + 1, centre.y  - y1 + 1));

    int e2 = err;
    if (e2 <= x1) {
      err += ++x1 * 2 + 1;
      if (-y1 == centre.x && e2 <= y1)
        e2 = 0;
      }
    if (e2 > y1)
      err += ++y1*2 + 1;
    } while (y1 <= 0);
  }
//}}}
//{{{
void cLcd::ellipseOutline (uint16_t colour, cPoint centre, cPoint radius) {

  if (radius.x && radius.y) {
    int x1 = 0;
    int y1 = -radius.y;
    int err = 2 - 2*radius.x;
    float k = (float)radius.y / radius.x;

    do {
      rectClipped (colour, cRect (centre.x - (uint16_t)(x1 / k), centre.x - (uint16_t)(x1 / k) + centre.y  + y1,
                                  1, centre.y  + y1 + 1));
      rectClipped (colour, cRect (centre.x + (uint16_t)(x1 / k), centre.x + (uint16_t)(x1 / k) + centre.y  + y1,
                                  1, centre.y  + y1 + 1));
      rectClipped (colour, cRect (centre.x + (uint16_t)(x1 / k), centre.x + (uint16_t)(x1 / k) + centre.y  - y1,
                                  1, centre.y  - y1 + 1));
      rectClipped (colour, cRect (centre.x - (uint16_t)(x1 / k), centre.x - (uint16_t)(x1 / k) + centre.y  - y1,
                                  1, centre.y  - y1 + 1));

      int e2 = err;
      if (e2 <= x1) {
        err += ++x1*2 + 1;
        if (-y1 == x1 && e2 <= y1)
          e2 = 0;
        }
      if (e2 > y1)
        err += ++y1*2 + 1;
      } while (y1 <= 0);
    }
  }
//}}}
//{{{
void cLcd::line (uint16_t colour, cPoint p1, cPoint p2) {

  int16_t deltax = (p2.x - p1.x) > 0 ? (p2.x - p1.x) : -(p2.x - p1.x);        /* The difference between the x's */
  int16_t deltay = (p2.y - p1.y) > 0 ? (p2.y - p1.y) : -(p2.y - p1.y);        /* The difference between the y's */
  int16_t x = p1.x;                       /* Start x off at the first pixel */
  int16_t y = p1.y;                       /* Start y off at the first pixel */

  int16_t xinc1;
  int16_t xinc2;
  if (p2.x >= p1.x) {               /* The x-values are increasing */
    xinc1 = 1;
    xinc2 = 1;
    }
  else {                         /* The x-values are decreasing */
    xinc1 = -1;
    xinc2 = -1;
    }

  int yinc1;
  int yinc2;
  if (p2.y >= p1.y) {                 /* The y-values are increasing */
    yinc1 = 1;
    yinc2 = 1;
    }
  else {                         /* The y-values are decreasing */
    yinc1 = -1;
    yinc2 = -1;
    }

  int den = 0;
  int num = 0;
  int num_add = 0;
  int num_pixels = 0;
  if (deltax >= deltay) {        /* There is at least one x-value for every y-value */
    xinc1 = 0;                  /* Don't change the x when numerator >= denominator */
    yinc2 = 0;                  /* Don't change the y for every iteration */
    den = deltax;
    num = deltax / 2;
    num_add = deltay;
    num_pixels = deltax;         /* There are more x-values than y-values */
    }
  else {                         /* There is at least one y-value for every x-value */
    xinc2 = 0;                  /* Don't change the x for every iteration */
    yinc1 = 0;                  /* Don't change the y when numerator >= denominator */
    den = deltay;
    num = deltay / 2;
    num_add = deltax;
    num_pixels = deltay;         /* There are more y-values than x-values */
    }

  for (int curpixel = 0; curpixel <= num_pixels; curpixel++) {
    rectClipped (colour, cRect(x, y, x+1, y+1));   /* Draw the current pixel */
    num += num_add;                            /* Increase the numerator by the top of the fraction */
    if (num >= den) {                          /* Check if numerator >= denominator */
      num -= den;                             /* Calculate the new numerator value */
      x += xinc1;                             /* Change the x as appropriate */
      y += yinc1;                             /* Change the y as appropriate */
      }
    x += xinc2;                               /* Change the x as appropriate */
    y += yinc2;                               /* Change the y as appropriate */
    }
  }
//}}}
//{{{
int cLcd::text (uint16_t colour, uint16_t fontHeight, const std::string str, cRect r) {

  for (auto ch : str) {
    if ((ch >= 0x20) && (ch <= 0x7F)) {
      auto fontCharIt = mFontCharMap.find ((fontHeight << 8) | ch);
      cFontChar* fontChar = fontCharIt != mFontCharMap.end() ? fontCharIt->second : nullptr;
      if (!fontChar)
        fontChar = loadChar (fontHeight, ch);
      if (fontChar) {
        if (r.left + fontChar->left + fontChar->pitch >= r.right)
          break;
        else if (fontChar->bitmap)
          stampClipped (colour, fontChar->bitmap,
                         cRect (r.left + fontChar->left, r.top + fontHeight - fontChar->top,
                                r.left + fontChar->left + fontChar->pitch,
                                r.top + fontHeight - fontChar->top + fontChar->rows));
        r.left += fontChar->advance;
        }
      }
    }

  return r.left;
  }
//}}}

//{{{
void cLcd::rgb888to565 (uint8_t* src, uint16_t* dst, uint16_t xsize) {

  ready();
  for (uint16_t x = 0; x < xsize; x++) {
    uint8_t b = (*src++) & 0xF8;
    uint8_t g = (*src++) & 0xFC;
    uint8_t r = (*src++) & 0xF8;
    *dst++ = (r << 8) | (g << 3) | (b >> 3);
    }
  }
//}}}
//{{{
void cLcd::jpegYuvTo565 (uint8_t* src, uint16_t* dst, uint16_t xsize, uint16_t ysize, uint32_t chromaSampling) {

  ready();

  uint32_t cssMode = DMA2D_CSS_420;
  uint32_t inputLineOffset = 0;

  if (chromaSampling == JPEG_420_SUBSAMPLING) {
    cssMode = DMA2D_CSS_420;
    inputLineOffset = xsize % 16;
    if (inputLineOffset != 0)
      inputLineOffset = 16 - inputLineOffset;
    }
  else if (chromaSampling == JPEG_444_SUBSAMPLING) {
    cssMode = DMA2D_NO_CSS;
    inputLineOffset = xsize % 8;
    if (inputLineOffset != 0)
      inputLineOffset = 8 - inputLineOffset;
    }
  else if (chromaSampling == JPEG_422_SUBSAMPLING) {
    cssMode = DMA2D_CSS_422;
    inputLineOffset = xsize % 16;
    if (inputLineOffset != 0)
      inputLineOffset = 16 - inputLineOffset;
    }

  // Configure the DMA2D Mode, Color Mode and output offset
  DMA2D_Handle.Instance = DMA2D;
  DMA2D_Handle.Init.Mode = DMA2D_M2M_PFC;
  DMA2D_Handle.Init.ColorMode = DMA2D_OUTPUT_RGB565;
  DMA2D_Handle.Init.OutputOffset = 0; //1024 - xsize;
  DMA2D_Handle.Init.AlphaInverted = DMA2D_REGULAR_ALPHA;  /* No Output Alpha Inversion*/
  DMA2D_Handle.Init.RedBlueSwap = DMA2D_RB_REGULAR;     /* No Output Red & Blue swap */

  // DMA2D Callbacks Configuration
  DMA2D_Handle.XferCpltCallback  = NULL;

  // Foreground Configuration
  DMA2D_Handle.LayerCfg[1].AlphaMode = DMA2D_REPLACE_ALPHA;
  DMA2D_Handle.LayerCfg[1].InputAlpha = 0xFF;
  DMA2D_Handle.LayerCfg[1].InputColorMode = DMA2D_INPUT_YCBCR;
  DMA2D_Handle.LayerCfg[1].ChromaSubSampling = cssMode;
  DMA2D_Handle.LayerCfg[1].InputOffset = inputLineOffset;
  DMA2D_Handle.LayerCfg[1].RedBlueSwap = DMA2D_RB_REGULAR; /* No ForeGround Red/Blue swap */
  DMA2D_Handle.LayerCfg[1].AlphaInverted = DMA2D_REGULAR_ALPHA; /* No ForeGround Alpha inversion */

  // DMA2D Initialization
  HAL_DMA2D_Init (&DMA2D_Handle);
  HAL_DMA2D_ConfigLayer (&DMA2D_Handle, 1);

  // copy the new decoded frame to the LCD Frame buffer
  HAL_DMA2D_Start (&DMA2D_Handle, (uint32_t)src, (uint32_t)dst, xsize, ysize);
  HAL_DMA2D_PollForTransfer (&DMA2D_Handle, 25);
  }
//}}}

//{{{
void cLcd::start() {
  mStartTime = HAL_GetTick();
  }
//}}}
//{{{
void cLcd::drawInfo() {

  int titleHeight = 20;
  int gap = 4;
  int infoHeight = 12;
  int smallGap = 2;

  // draw title
  text (COL_YELLOW, titleHeight, mTitle, cRect(0, 0, getWidth(), titleHeight+gap));

  // draw footer
  auto y = getHeight() - titleHeight-4;
  text (COL_WHITE, titleHeight,
  //      dec (xPortGetFreeHeapSize()) + ":" + dec (xPortGetMinimumEverFreeHeapSize()) +
        dec(mNumPresents) + ":" + dec (mDrawTime) + ":" + dec (mWaitTime) + "ms " +
        dec (osGetCPUUsage()) + "% " + dec (mBrightness),
        cRect(0, y, getWidth(), titleHeight+gap));

  if (mShowInfo) {
    y -= titleHeight - gap;
    auto line = mCurLine - 1;
    while ((y > titleHeight) && (line >= 0)) {
      int lineIndex = line-- % kMaxLines;
      auto x = text (COL_GREEN, infoHeight,
                     dec ((mLines[lineIndex].mTime-mBaseTime) / 1000) + "." +
                     dec ((mLines[lineIndex].mTime-mBaseTime) % 1000, 3, '0'),
                     cRect(0, y, getWidth(), 20));
      text (mLines[lineIndex].mColour, infoHeight, mLines[lineIndex].mString,
            cRect (x + smallGap, y, getWidth(), 20));
      y -= infoHeight + smallGap;
      }
    }
  }
//}}}
//{{{
void cLcd::present() {

  ready();
  mDrawTime = HAL_GetTick() - mStartTime;

  // enable interrupts
  mShowBuffer = (uint32_t)mBuffer[mDrawBuffer];
  LTDC->IER = LTDC_IT_TE | LTDC_IT_FU | LTDC_IT_LI;

  xSemaphoreTake (mFrameSem, 1000);
  mWaitTime = HAL_GetTick() - mStartTime;

  mNumPresents++;

  // flip
  mDrawBuffer = !mDrawBuffer;
  }
//}}}

//{{{
void cLcd::display (int brightness) {

  mBrightness = brightness;
  TIM4->CCR2 = 50 * brightness;
  }
//}}}

// private
//{{{
void cLcd::ltdcInit (uint16_t* frameBufferAddress) {

  // config clocks
  //{{{  config gpio
  //  VS <-> PA.04 - unused
  //  HS <-> PC.06 - unused
  //  R2 <-> PC.10 - unused
  //
  //                 G2 <-> PA.06   B2 <-> PD.06
  //  R3 <-> PB.00   G3 <-> PG.10   B3 <-> PG.11
  //  R4 <-> PA.11   G4 <-> PB.10   B4 <-> PG.12
  //  R5 <-> PA.12   G5 <-> PB.11   B5 <-> PA.03
  //  R6 <-> PB.01   G6 <-> PC.07   B6 <-> PB.08
  //  R7 <-> PG.06   G7 <-> PD.03   B7 <-> PB.09
  //
  //  CK <-> PG.07
  //  DE <-> PF.10
  // ADJ <-> PD.13

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  // gpioA - AF14
  GPIO_InitTypeDef GPIO_InitStructure;
  GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStructure.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStructure.Pull = GPIO_NOPULL;
  GPIO_InitStructure.Alternate = GPIO_AF14_LTDC;
  GPIO_InitStructure.Pin = GPIO_PIN_3 | GPIO_PIN_6 | GPIO_PIN_11 | GPIO_PIN_12; // GPIO_PIN_4
  HAL_GPIO_Init (GPIOA, &GPIO_InitStructure);

  // gpioB
  GPIO_InitStructure.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;
  HAL_GPIO_Init (GPIOB, &GPIO_InitStructure);

  // gpioC
  GPIO_InitStructure.Pin = GPIO_PIN_7; // ; | GPIO_PIN_10; // GPIO_PIN_6
  HAL_GPIO_Init (GPIOC, &GPIO_InitStructure);

  // gpioD
  GPIO_InitStructure.Pin = GPIO_PIN_3 | GPIO_PIN_6;
  HAL_GPIO_Init (GPIOD, &GPIO_InitStructure);

  // gpioF
  GPIO_InitStructure.Pin = GPIO_PIN_10;
  HAL_GPIO_Init (GPIOF, &GPIO_InitStructure);

  // gpioG
  GPIO_InitStructure.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_11;
  HAL_GPIO_Init (GPIOG, &GPIO_InitStructure);

  // gpioB - AF9
  GPIO_InitStructure.Alternate = GPIO_AF9_LTDC;
  GPIO_InitStructure.Pin = GPIO_PIN_0 | GPIO_PIN_1;
  HAL_GPIO_Init (GPIOB, &GPIO_InitStructure);

  // gpioG - AF9
  GPIO_InitStructure.Pin = GPIO_PIN_10 | GPIO_PIN_12;
  HAL_GPIO_Init (GPIOG, &GPIO_InitStructure);
  //}}}
  //{{{  adj PWM - PD13
  __HAL_RCC_TIM4_CLK_ENABLE();

  GPIO_InitStructure.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStructure.Pull = GPIO_NOPULL;
  GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStructure.Alternate = GPIO_AF2_TIM4;
  GPIO_InitStructure.Pin = GPIO_PIN_13;
  HAL_GPIO_Init (GPIOD, &GPIO_InitStructure);
  //HAL_GPIO_WritePin (GPIOD, GPIO_PIN_13, GPIO_PIN_SET);

  //  config TIM4 chan2 PWM to PD13
  TIM_HandleTypeDef mTimHandle;
  mTimHandle.Instance = TIM4;
  mTimHandle.Init.Period = 5000 - 1;
  mTimHandle.Init.Prescaler = 1;
  mTimHandle.Init.ClockDivision = 0;
  mTimHandle.Init.CounterMode = TIM_COUNTERMODE_UP;

  if (HAL_TIM_Base_Init (&mTimHandle))
    printf ("HAL_TIM_Base_Init failed\n");

  // init timOcInit
  TIM_OC_InitTypeDef timOcInit = {0};
  timOcInit.OCMode       = TIM_OCMODE_PWM1;
  timOcInit.OCPolarity   = TIM_OCPOLARITY_HIGH;
  timOcInit.OCFastMode   = TIM_OCFAST_DISABLE;
  timOcInit.OCNPolarity  = TIM_OCNPOLARITY_HIGH;
  timOcInit.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  timOcInit.OCIdleState  = TIM_OCIDLESTATE_RESET;
  timOcInit.Pulse = 50 * mBrightness;

  if (HAL_TIM_PWM_ConfigChannel (&mTimHandle, &timOcInit, TIM_CHANNEL_2))
    printf ("HAL_TIM_PWM_ConfigChannel failed\n");

  if (HAL_TIM_PWM_Start (&mTimHandle, TIM_CHANNEL_2))
    printf ("HAL_TIM_PWM_Start TIM4 ch2 failed\n");
  //}}}

  __HAL_RCC_LTDC_CLK_ENABLE();
  mLtdcHandle.Instance = LTDC;
  mLtdcHandle.Init.HorizontalSync     = HORIZ_SYNC - 1;
  mLtdcHandle.Init.AccumulatedHBP     = HORIZ_SYNC - 1;
  mLtdcHandle.Init.AccumulatedActiveW = HORIZ_SYNC + LCD_WIDTH - 1;
  mLtdcHandle.Init.TotalWidth         = HORIZ_SYNC + LCD_WIDTH - 1;
  mLtdcHandle.Init.VerticalSync       = VERT_SYNC - 1;
  mLtdcHandle.Init.AccumulatedVBP     = VERT_SYNC - 1;
  mLtdcHandle.Init.AccumulatedActiveH = VERT_SYNC + LCD_HEIGHT - 1;
  mLtdcHandle.Init.TotalHeigh         = VERT_SYNC + LCD_HEIGHT - 1;
  mLtdcHandle.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  mLtdcHandle.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  mLtdcHandle.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  mLtdcHandle.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
  mLtdcHandle.Init.Backcolor.Red = 0;
  mLtdcHandle.Init.Backcolor.Blue = 0;
  mLtdcHandle.Init.Backcolor.Green = 0;
  HAL_LTDC_Init (&mLtdcHandle);

  LTDC_LayerCfgTypeDef* curLayerCfg = &mLtdcHandle.LayerCfg[0];
  curLayerCfg->WindowX0 = 0;
  curLayerCfg->WindowY0 = 0;
  curLayerCfg->WindowX1 = getWidth();
  curLayerCfg->WindowY1 = getHeight();
  curLayerCfg->PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
  curLayerCfg->FBStartAdress = (uint32_t)frameBufferAddress;
  curLayerCfg->Alpha = 255;
  curLayerCfg->Alpha0 = 0;
  curLayerCfg->Backcolor.Blue = 0;
  curLayerCfg->Backcolor.Green = 0;
  curLayerCfg->Backcolor.Red = 0;
  curLayerCfg->BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
  curLayerCfg->BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
  curLayerCfg->ImageWidth = getWidth();
  curLayerCfg->ImageHeight = getHeight();
  HAL_LTDC_ConfigLayer (&mLtdcHandle, curLayerCfg, 0);

  // set line interupt lineNumber
  LTDC->LIPCR = 0;
  LTDC->ICR = LTDC_IT_TE | LTDC_IT_FU | LTDC_IT_LI;

  HAL_NVIC_SetPriority (LTDC_IRQn, 0xE, 0);
  HAL_NVIC_EnableIRQ (LTDC_IRQn);

  __HAL_RCC_DMA2D_CLK_ENABLE();
  }
//}}}
//{{{
void cLcd::ready() {

  if (mDma2dWait == eWaitDone) {
    while (!(DMA2D->ISR & DMA2D_FLAG_TC))
      taskYIELD();
    DMA2D->IFCR = DMA2D_FLAG_TC;
    }
  else if (mDma2dWait == eWaitIrq)
    xSemaphoreTake (mDma2dSem, 100);

  mDma2dWait = eWaitNone;
  }
//}}}

//{{{
cFontChar* cLcd::loadChar (uint16_t fontHeight, char ch) {

  FT_Set_Pixel_Sizes (FTface, 0, fontHeight);
  FT_Load_Char (FTface, ch, FT_LOAD_RENDER);

  auto fontChar = new cFontChar();
  fontChar->left = FTglyphSlot->bitmap_left;
  fontChar->top = FTglyphSlot->bitmap_top;
  fontChar->pitch = FTglyphSlot->bitmap.pitch;
  fontChar->rows = FTglyphSlot->bitmap.rows;
  fontChar->advance = FTglyphSlot->advance.x / 64;
  fontChar->bitmap = nullptr;

  if (FTglyphSlot->bitmap.buffer) {
    fontChar->bitmap = (uint8_t*)malloc (FTglyphSlot->bitmap.pitch * FTglyphSlot->bitmap.rows);
    memcpy (fontChar->bitmap, FTglyphSlot->bitmap.buffer, FTglyphSlot->bitmap.pitch * FTglyphSlot->bitmap.rows);
    }

  auto insertPair = mFontCharMap.insert (cFontCharMap::value_type (fontHeight<<8 | ch, fontChar));
  auto fontCharIt = insertPair.first;
  return fontCharIt->second;
  }
//}}}

//{{{
void cLcd::reset() {

  for (auto i = 0; i < kMaxLines; i++)
    mLines[i].clear();

  mBaseTime = HAL_GetTick();
  mCurLine = 0;
  }
//}}}
