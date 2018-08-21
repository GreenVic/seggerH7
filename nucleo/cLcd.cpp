// cLcd.cpp
//{{{  includes
#include "cLcd.h"
#include "../freetype/FreeSansBold.h"
#include "cpuUsage.h"
#include "math.h"

#include "heap.h"
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
static SemaphoreHandle_t mLockSem;

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
extern "C" { void DMA2D_IRQHandler() {

  uint32_t isr = DMA2D->ISR;
  if (isr & DMA2D_FLAG_TC) {
    DMA2D->IFCR = DMA2D_FLAG_TC;

    portBASE_TYPE taskWoken = pdFALSE;
    if (xSemaphoreGiveFromISR (cLcd::mDma2dSem, &taskWoken) == pdTRUE)
      portEND_SWITCHING_ISR (taskWoken);
    }
  if (isr & DMA2D_FLAG_TE) {
    printf ("DMA2D_IRQHandler transfer error\n");
    DMA2D->IFCR = DMA2D_FLAG_TE;
    }
  if (isr & DMA2D_FLAG_CE) {
    printf ("DMA2D_IRQHandler config error\n");
    DMA2D->IFCR = DMA2D_FLAG_CE;
    }
  }
}
//}}}

//{{{
cLcd::cLcd()  {

  mBuffer[0] = (uint16_t*)sdRamAlloc (LCD_WIDTH*LCD_HEIGHT*2, "lcdBuf0");
  mBuffer[1] = (uint16_t*)sdRamAlloc (LCD_WIDTH*LCD_HEIGHT*2, "lcdBuf1");
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

  mLockSem = xSemaphoreCreateMutex();
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

// logging
//{{{
void cLcd::setShowInfo (bool show) {
  if (show != mShowInfo) {
    mShowInfo = show;
    mChanged = true;
    }
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

// dmma2d draw
//{{{
void cLcd::rect (uint16_t colour, const cRect& r) {
//__IO uint32_t OPFCCR  Output PFC Control Register,    Address offset: 0x34
//__IO uint32_t OCOLR   Output Color Register,          Address offset: 0x38
//__IO uint32_t OMAR    Output Memory Address Register, Address offset: 0x3C
//__IO uint32_t OOR     Output Offset Register,         Address offset: 0x40
//__IO uint32_t NLR     Number of Line Register,        Address offset: 0x44

  if (!xSemaphoreTake (mLockSem, 5000))
    printf ("cLcd take fail\n");

  rectRegs[1] = colour;
  rectRegs[2] = uint32_t (mBuffer[mDrawBuffer] + r.top * getWidth() + r.left);
  rectRegs[3] = getWidth() - r.getWidth();
  rectRegs[4] = (r.getWidth() << 16) | r.getHeight();

  ready();
  memcpy ((void*)(&DMA2D->OPFCCR), rectRegs, 5*4);

  DMA2D->CR = DMA2D_R2M | DMA2D_CR_START | DMA2D_CR_TCIE | DMA2D_CR_TEIE | DMA2D_CR_CEIE;
  mDma2dWait = eWaitIrq;
  ready();
  xSemaphoreGive (mLockSem);
  }
//}}}
//{{{
void cLcd::clear (uint16_t colour) {

  cRect r (getSize());
  rect (colour, r);
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

  if (!xSemaphoreTake (mLockSem, 5000))
    printf ("cLcd take fail\n");

  stampRegs[0] = (uint32_t)src;
  stampRegs[2] = uint32_t(mBuffer[mDrawBuffer] + r.top * getWidth() + r.left);
  stampRegs[3] = getWidth() - r.getWidth();
  stampRegs[4] = DMA2D_INPUT_A8;
  stampRegs[5] = ((colour & 0xF800) << 8) | ((colour & 0x07E0) << 5) | ((colour & 0x001F) << 3);
  stampRegs[12] = stampRegs[2];
  stampRegs[13] = stampRegs[3];
  stampRegs[14] = (r.getWidth() << 16) | r.getHeight();

  memcpy ((void*)(&DMA2D->FGMAR), stampRegs, 15*4);
  DMA2D->CR = DMA2D_M2M_BLEND | DMA2D_CR_START | DMA2D_CR_TCIE | DMA2D_CR_TEIE | DMA2D_CR_CEIE;
  mDma2dWait = eWaitIrq;

  ready();

  xSemaphoreGive (mLockSem);
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
int cLcd::text (uint16_t colour, uint16_t fontHeight, const std::string& str, cRect r) {

  if (!xSemaphoreTake (mLockSem, 5000))
    printf ("cLcd take fail\n");

  for (auto ch : str) {
    if ((ch >= 0x20) && (ch <= 0x7F)) {
      auto fontCharIt = mFontCharMap.find ((fontHeight << 8) | ch);
      cFontChar* fontChar = fontCharIt != mFontCharMap.end() ? fontCharIt->second : nullptr;
      if (!fontChar)
        fontChar = loadChar (fontHeight, ch);
      if (fontChar) {
        if (r.left + fontChar->left + fontChar->pitch >= r.right)
          break;
        else if (fontChar->bitmap) {
          auto src = fontChar->bitmap;
          cRect charRect (r.left + fontChar->left, r.top + fontHeight - fontChar->top,
                          r.left + fontChar->left + fontChar->pitch,
                          r.top + fontHeight - fontChar->top + fontChar->rows);

          // simple clips
          if (charRect.top < 0) {
            src += -charRect.top * charRect.getWidth();
            charRect.top = 0;
            }
          if (charRect.bottom > getHeight())
            charRect.bottom = getHeight();

          if ((charRect.left >= 0) && (charRect.bottom > 0) && (charRect.top < getHeight())) {
            ready();
            stampRegs[0] = (uint32_t)src;
            stampRegs[2] = uint32_t(mBuffer[mDrawBuffer] + charRect.top * getWidth() + charRect.left);
            stampRegs[3] = getWidth() - charRect.getWidth();
            stampRegs[4] = DMA2D_INPUT_A8;
            stampRegs[5] = ((colour & 0xF800) << 8) | ((colour & 0x07E0) << 5) | ((colour & 0x001F) << 3);
            stampRegs[12] = stampRegs[2];
            stampRegs[13] = stampRegs[3];
            stampRegs[14] = (charRect.getWidth() << 16) | charRect.getHeight();

            memcpy ((void*)(&DMA2D->FGMAR), stampRegs, 15*4);
            DMA2D->CR = DMA2D_M2M_BLEND | DMA2D_CR_START | DMA2D_CR_TCIE | DMA2D_CR_TEIE | DMA2D_CR_CEIE;
            mDma2dWait = eWaitIrq;
            }
          }
        r.left += fontChar->advance;
        }
      }
    }
  ready();
  xSemaphoreGive (mLockSem);

  return r.left;
  }
//}}}

// cpu draw
//{{{
void cLcd::grad (uint16_t colTL, uint16_t colTR, uint16_t colBL, uint16_t colBR, const cRect& r) {

  int32_t rTL = (colTL & 0xF800) << 5;
  int32_t rTR = (colTR & 0xF800) << 5;
  int32_t rBL = (colBL & 0xF800) << 5;
  int32_t rBR = (colBR & 0xF800) << 5;

  int32_t gTL = (colTL & 0x07E0) << 11;
  int32_t gTR = (colTR & 0x07E0) << 11;
  int32_t gBL = (colBL & 0x07E0) << 11;
  int32_t gBR = (colBR & 0x07E0) << 11;

  int32_t bTL = (colTL & 0x001F) << 16;
  int32_t bTR = (colTR & 0x001F) << 16;
  int32_t bBL = (colBL & 0x001F) << 16;
  int32_t bBR = (colBR & 0x001F) << 16;

  int32_t rl16 = rTL;
  int32_t gl16 = gTL;
  int32_t bl16 = bTL;
  int32_t rGradl16 = (rBL - rTL) / r.getHeight();
  int32_t gGradl16 = (gBL - gTL) / r.getHeight();
  int32_t bGradl16 = (bBL - bTL) / r.getHeight();

  int32_t rr16 = rTR;
  int32_t gr16 = gTR;
  int32_t br16 = bTR;
  int32_t rGradr16 = (rBR - rTR) / r.getHeight();
  int32_t gGradr16 = (gBR - gTR) / r.getHeight();
  int32_t bGradr16 = (bBR - bTR) / r.getHeight();

  auto dst = mBuffer[mDrawBuffer] + r.top * getWidth() + r.left;
  for (uint16_t y = r.top; y < r.bottom; y++) {
    int32_t rGradx16 = (rr16 - rl16) / r.getWidth();
    int32_t gGradx16 = (gr16 - gl16) / r.getWidth();
    int32_t bGradx16 = (br16 - bl16) / r.getWidth();

    int32_t r16 = rl16;
    int32_t g16 = gl16;
    int32_t b16 = bl16;
    for (uint16_t x = r.left; x < r.right; x++) {
      *dst++ = (b16 >> 16) | ((g16 >> 11) & 0x07E0) | ((r16 >> 5) & 0xF800);
      r16 += rGradx16;
      g16 += gGradx16;
      b16 += bGradx16;
      }
    dst += getWidth() - r.getWidth();

    rl16 += rGradl16;
    gl16 += gGradl16;
    bl16 += bGradl16;

    rr16 += rGradr16;
    gr16 += gGradr16;
    br16 += bGradr16;
    }
  }
//}}}
//{{{
void cLcd::line (uint16_t colour, cPoint p1, cPoint p2) {

  int16_t deltax = abs(p2.x - p1.x); // The difference between the x's
  int16_t deltay = abs(p2.y - p1.y); // The difference between the y's

  cPoint p = p1;
  cPoint inc1 ((p2.x >= p1.x) ? 1 : -1, (p2.y >= p1.y) ? 1 : -1);
  cPoint inc2 = inc1;

  int16_t numAdd = (deltax >= deltay) ? deltay : deltax;
  int16_t den = (deltax >= deltay) ? deltax : deltay;
  if (deltax >= deltay) { // There is at least one x-value for every y-value
    inc1.x = 0;            // Don't change the x when numerator >= denominator
    inc2.y = 0;            // Don't change the y for every iteration
    }
  else {                  // There is at least one y-value for every x-value
    inc2.x = 0;            // Don't change the x for every iteration
    inc1.y = 0;            // Don't change the y when numerator >= denominator
    }

  int16_t num = den / 2;
  int16_t numPix = den;
  for (int16_t pix = 0; pix <= numPix; pix++) {
    pixel (colour, p);
    num += numAdd;     // Increase the numerator by the top of the fraction
    if (num >= den) {   // Check if numerator >= denominator
      num -= den;       // Calculate the new numerator value
      p += inc1;
      }
    p += inc2;
    }
  }
//}}}
//{{{
void cLcd::ellipseOutline (uint16_t colour, cPoint centre, cPoint radius) {

  int x = 0;
  int y = -radius.y;

  int err = 2 - 2 * radius.x;
  float k = (float)radius.y / (float)radius.x;

  do {
    pixel (colour, centre + cPoint (-(int16_t)(x / k), y));
    pixel (colour, centre + cPoint ((int16_t)(x / k), y));
    pixel (colour, centre + cPoint ((int16_t)(x / k), -y));
    pixel (colour, centre + cPoint (- (int16_t)(x / k), - y));

    int e2 = err;
    if (e2 <= x) {
      err += ++x * 2+ 1 ;
      if (-y == x && e2 <= y)
        e2 = 0;
      }
    if (e2 > y)
      err += ++y *2 + 1;
    } while (y <= 0);
  }
//}}}

// statics
//{{{
void cLcd::rgb888toRgb565 (uint8_t* src, uint8_t* dst, uint16_t xsize, uint16_t ysize) {

  if (!xSemaphoreTake (mLockSem, 5000))
    printf ("cLcd take fail\n");

  DMA2D->FGPFCCR = DMA2D_INPUT_RGB888;
  DMA2D->FGMAR = uint32_t(src);
  DMA2D->FGOR = 0;

  DMA2D->OPFCCR = DMA2D_OUTPUT_RGB565;
  DMA2D->OMAR = uint32_t(dst);
  DMA2D->OOR = 0;

  DMA2D->NLR = (xsize << 16) | ysize;
  DMA2D->CR = DMA2D_M2M_PFC | DMA2D_CR_START | DMA2D_CR_TCIE | DMA2D_CR_TEIE | DMA2D_CR_CEIE;
  mDma2dWait = eWaitIrq;

  ready();

  xSemaphoreGive (mLockSem);
  }
//}}}
//{{{
void cLcd::yuvMcuToRgb565 (uint8_t* src, uint8_t* dst, uint16_t xsize, uint16_t ysize, uint32_t chromaSampling) {

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

  if (!xSemaphoreTake (mLockSem, 5000))
    printf ("cLcd take fail\n");

  DMA2D->FGPFCCR = DMA2D_INPUT_YCBCR | (cssMode << POSITION_VAL(DMA2D_FGPFCCR_CSS));
  DMA2D->FGMAR = (uint32_t)src;
  DMA2D->FGOR = inputLineOffset;

  DMA2D->OPFCCR = DMA2D_OUTPUT_RGB565;
  DMA2D->OMAR = (uint32_t)dst;
  DMA2D->OOR = 0;

  DMA2D->NLR = (xsize << 16) | ysize;
  DMA2D->CR = DMA2D_M2M_PFC | DMA2D_CR_START | DMA2D_CR_TCIE | DMA2D_CR_TEIE | DMA2D_CR_CEIE;
  mDma2dWait = eWaitIrq;

  ready();

  xSemaphoreGive (mLockSem);
  }
//}}}

// cTile
//{{{
void cLcd::copy (cTile* srcTile, cPoint p) {

  if (!xSemaphoreTake (mLockSem, 5000))
    printf ("cLcd take fail\n");

  uint16_t width = p.x + srcTile->mWidth > getWidth() ? getWidth() - p.x : srcTile->mWidth;
  uint16_t height = p.y + srcTile->mHeight > getHeight() ? getHeight() - p.y : srcTile->mHeight;

  DMA2D->FGPFCCR = srcTile->mFormat;
  DMA2D->FGMAR = (uint32_t)srcTile->mPiccy;
  DMA2D->FGOR = srcTile->mPitch - width;

  DMA2D->OPFCCR = DMA2D_OUTPUT_RGB565;
  DMA2D->OMAR = uint32_t(mBuffer[mDrawBuffer] + (p.y * getWidth()) + p.x);
  DMA2D->OOR = getWidth() > srcTile->mWidth ? getWidth() - srcTile->mWidth : 0;

  DMA2D->NLR = (width << 16) | height;
  DMA2D->CR = DMA2D_M2M_PFC | DMA2D_CR_START | DMA2D_CR_TCIE | DMA2D_CR_TEIE | DMA2D_CR_CEIE;
  mDma2dWait = eWaitIrq;

  ready();

  xSemaphoreGive (mLockSem);
  }
//}}}
//{{{
void cLcd::copy90 (cTile* srcTile, cPoint p) {

  if (!xSemaphoreTake (mLockSem, 5000))
    printf ("cLcd take fail\n");

  uint32_t src = (uint32_t)srcTile->mPiccy;
  uint32_t dst = (uint32_t)mBuffer[mDrawBuffer];

  DMA2D->FGPFCCR = srcTile->mFormat;
  DMA2D->FGOR = 0;

  DMA2D->OPFCCR = DMA2D_OUTPUT_RGB565;
  DMA2D->OOR = getWidth() - 1;
  DMA2D->NLR = 0x10000 | (srcTile->mWidth);

  for (int line = 0; line < srcTile->mHeight; line++) {
    DMA2D->FGMAR = src;
    DMA2D->OMAR = dst;
    DMA2D->CR = DMA2D_M2M_PFC | DMA2D_CR_START | DMA2D_CR_TCIE | DMA2D_CR_TEIE | DMA2D_CR_CEIE;
    src += srcTile->mWidth * srcTile->mComponents;
    dst += 2;

    mDma2dWait = eWaitDone;
    ready();
    }

  xSemaphoreGive (mLockSem);
  }
//}}}
//{{{
void cLcd::size (cTile* srcTile, const cRect& r) {

  uint32_t xStep16 = ((srcTile->mWidth - 1) << 16) / (r.getWidth() - 1);
  uint32_t yStep16 = ((srcTile->mHeight - 1) << 16) / (r.getHeight() - 1);

  auto dst = mBuffer[mDrawBuffer] + r.top * getWidth() + r.left;

  if (!xSemaphoreTake (mLockSem, 5000))
    printf ("cLcd take fail\n");

  for (uint32_t y16 = (srcTile->mY << 16); y16 < ((srcTile->mY + r.getHeight()) * yStep16); y16 += yStep16) {
    auto src = (uint16_t*)(srcTile->mPiccy) + (srcTile->mY + (y16 >> 16)) * srcTile->mPitch + srcTile->mX;
    for (uint32_t x16 = srcTile->mX << 16; x16 < (srcTile->mX + r.getWidth()) * xStep16; x16 += xStep16)
      *dst++ = *(src + (x16 >> 16));
    dst += getWidth() - r.getWidth();
    }

  xSemaphoreGive (mLockSem);
  }
//}}}

//{{{
void cLcd::start() {
  mStartTime = HAL_GetTick();
  }
//}}}
//{{{
void cLcd::drawInfo() {

  const int kTitleHeight = 20;
  const int kFooterHeight = 14;
  const int kInfoHeight = 12;
  const int kGap = 4;
  const int kSmallGap = 2;

  // draw title
  const cRect titleRect (0,0, getWidth(), kTitleHeight+kGap);
  text (COL_BLACK, kTitleHeight, mTitle, titleRect);
  text (COL_YELLOW, kTitleHeight, mTitle, titleRect + cPoint(-2,-2));

  if (mShowInfo) {
    // draw footer
    auto y = getHeight() - kFooterHeight - kGap;
    text (COL_WHITE, kFooterHeight,
          dec(mNumPresents) + ":" + dec (mDrawTime) + ":" + dec (mWaitTime) + " " +
          dec (osGetCPUUsage()) + "%:" + dec (mBrightness) + "% " +
          "dtcm:" + dec (getDtcmFree()/1000) + ":" + dec (getDtcmSize()/1000) + " " +
          "s123:" + dec (getSram123Free()/1000) + ":" + dec (getSram123Size()/1000) + " " +
          "axi:" + dec (getSramFree()/1000) + ":" + dec (getSramMinFree()/1000) + ":" + dec (getSramSize()/1000) + " " +
          "sd:" + dec (getSdRamFree()/1000) + ":" + dec (getSdRamMinFree()/1000) + ":" + dec (getSdRamSize()/1000),
          cRect(0, y, getWidth(), kTitleHeight+kGap));

    // draw log
    y -= kTitleHeight - kGap;
    auto line = mCurLine - 1;
    while ((y > kTitleHeight) && (line >= 0)) {
      int lineIndex = line-- % kMaxLines;
      auto x = text (COL_GREEN, kInfoHeight,
                     dec ((mLines[lineIndex].mTime-mBaseTime) / 1000) + "." +
                     dec ((mLines[lineIndex].mTime-mBaseTime) % 1000, 3, '0'),
                     cRect(0, y, getWidth(), 20));
      text (mLines[lineIndex].mColour, kInfoHeight, mLines[lineIndex].mString,
            cRect (x + kSmallGap, y, getWidth(), 20));
      y -= kInfoHeight + kSmallGap;
      }
    }
  }
//}}}
//{{{
void cLcd::present() {

  //ready();
  mDrawTime = HAL_GetTick() - mStartTime;

  // enable interrupts
  mShowBuffer = (uint32_t)mBuffer[mDrawBuffer];
  LTDC->IER = LTDC_IT_TE | LTDC_IT_FU | LTDC_IT_LI;

  if (!xSemaphoreTake (mFrameSem, 100))
    printf ("cLcd present take fail\n");
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

  //{{{  config gpio
  //  R2 <-> PC.10
  //  B2 <-> PD.06
  //                 G2 <-> PA.06
  //  R3 <-> PB.00   G3 <-> PG.10   B3 <-> PA.08
  //  R4 <-> PA.05   G4 <-> PB.10   B4 <-> PA.10
  //  R5 <-> PA.09   G5 <-> PB.11   B5 <-> PA.03
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

  // gpioB - AF9
  GPIO_InitTypeDef GPIO_InitStructure;
  GPIO_InitStructure.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStructure.Pull = GPIO_NOPULL;
  GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStructure.Alternate = GPIO_AF9_LTDC;
  GPIO_InitStructure.Pin = GPIO_PIN_0 | GPIO_PIN_1;
  HAL_GPIO_Init (GPIOB, &GPIO_InitStructure);

  // gpioG - AF9
  GPIO_InitStructure.Pin = GPIO_PIN_10 | GPIO_PIN_12;
  HAL_GPIO_Init (GPIOG, &GPIO_InitStructure);

  // gpioA - AF12
  GPIO_InitStructure.Alternate = GPIO_AF12_LTDC;
  GPIO_InitStructure.Pin = GPIO_PIN_10;
  HAL_GPIO_Init (GPIOA, &GPIO_InitStructure);

  // gpioA - AF12
  GPIO_InitStructure.Alternate = GPIO_AF13_LTDC;
  GPIO_InitStructure.Pin = GPIO_PIN_8;
  HAL_GPIO_Init (GPIOA, &GPIO_InitStructure);

  // AF14 gpioA
  GPIO_InitStructure.Alternate = GPIO_AF14_LTDC;
  GPIO_InitStructure.Pin = GPIO_PIN_3 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_9 ;
  HAL_GPIO_Init (GPIOA, &GPIO_InitStructure);

  // gpioB
  GPIO_InitStructure.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;
  HAL_GPIO_Init (GPIOB, &GPIO_InitStructure);

  // gpioC
  GPIO_InitStructure.Pin = GPIO_PIN_7;
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

  // enable dither
  LTDC->GCR |= (uint32_t)LTDC_GCR_DEN;

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
    if (!xSemaphoreTake (mDma2dSem, 5000))
      printf ("cLcd ready take fail\n");

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
    fontChar->bitmap = (uint8_t*)pvPortMalloc (FTglyphSlot->bitmap.pitch * FTglyphSlot->bitmap.rows);
    memcpy (fontChar->bitmap, FTglyphSlot->bitmap.buffer, FTglyphSlot->bitmap.pitch * FTglyphSlot->bitmap.rows);
    }

  return mFontCharMap.insert (
    std::map<uint16_t, cFontChar*>::value_type (fontHeight<<8 | ch, fontChar)).first->second;
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
