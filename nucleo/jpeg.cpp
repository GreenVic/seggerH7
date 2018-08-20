// jpeg.cpp
//{{{  includes
#include "jpeg.h"

#include "cmsis_os.h"
#include "stm32h7xx_nucleo_144.h"
#include "heap.h"

#include "cLcd.h"
#include "../fatFs/ff.h"

#include "jpeglib.h"

using namespace std;
//}}}
//{{{  defines
#define JPEG_AC_HUFF_TABLE_SIZE  ((uint32_t)162U) /* Huffman AC table size : 162 codes*/
#define JPEG_DC_HUFF_TABLE_SIZE  ((uint32_t)12U)  /* Huffman AC table size : 12 codes*/

#define JPEG_FIFO_SIZE    ((uint32_t)16U) /* JPEG Input/Output HW FIFO size in words*/
#define JPEG_FIFO_TH_SIZE ((uint32_t)8U)  /* JPEG Input/Output HW FIFO Threshold in words*/

#define JPEG_INTERRUPT_MASK  ((uint32_t)0x0000007EU) /* JPEG Interrupt Mask*/

#define JPEG_CONTEXT_PAUSE_INPUT    ((uint32_t)0x00001000U)  /* JPEG context : Pause Input */
#define JPEG_CONTEXT_CUSTOM_TABLES  ((uint32_t)0x00004000U)  /* JPEG context : Use custom quantization tables */
#define JPEG_CONTEXT_ENDING_DMA     ((uint32_t)0x00008000U)  /* JPEG context : ending with DMA in progress */

#define JPEG_PROCESS_ONGOING        ((uint32_t)0x00000000U)  /* Process is on going */
#define JPEG_PROCESS_DONE           ((uint32_t)0x00000001U)  /* Process is done (ends) */
//}}}
//{{{  struct
typedef struct {
  uint8_t CodeLength[JPEG_AC_HUFF_TABLE_SIZE];
  uint32_t HuffmanCode[JPEG_AC_HUFF_TABLE_SIZE];
  } JPEG_AC_HuffCodeTableTypeDef;

typedef struct {
  uint8_t CodeLength[JPEG_DC_HUFF_TABLE_SIZE];
  uint32_t HuffmanCode[JPEG_DC_HUFF_TABLE_SIZE];
  } JPEG_DC_HuffCodeTableTypeDef;

typedef struct {        // These two fields directly represent the contents of a JPEG DHT marker */
  uint8_t Bits[16];     // bits[k] = # of symbols with codes of length k bits, this parameter corresponds to BITS list in the Annex C */
  uint8_t HuffVal[12];  // The symbols, in order of incremented code length, this parameter corresponds to HUFFVAL list in the Annex C */
  }JPEG_DCHuffTableTypeDef;

typedef struct {        // These two fields directly represent the contents of a JPEG DHT marker */
  uint8_t Bits[16];     // bits[k] = # of symbols with codes of length k bits, this parameter corresponds to BITS list in the Annex C */
  uint8_t HuffVal[162]; // The symbols, in order of incremented code length, this parameter corresponds to HUFFVAL list in the Annex C */
  } JPEG_ACHuffTableTypeDef;

typedef struct {
  bool mFull;
  uint8_t* mBuf;
  uint32_t mSize;
  } tBufs;

//{{{
const uint8_t LUM_QuantTable[JPEG_QUANT_TABLE_SIZE] = {
  16,  11,  10,  16,  24,  40,  51,  61,
  12,  12,  14,  19,  26,  58,  60,  55,
  14,  13,  16,  24,  40,  57,  69,  56,
  14,  17,  22,  29,  51,  87,  80,  62,
  18,  22,  37,  56,  68, 109, 103,  77,
  24,  35,  55,  64,  81, 104, 113,  92,
  49,  64,  78,  87, 103, 121, 120, 101,
  72,  92,  95,  98, 112, 100, 103,  99
  };
//}}}
//{{{
const uint8_t CHROM_QuantTable[JPEG_QUANT_TABLE_SIZE] = {
  17,  18,  24,  47,  99,  99,  99,  99,
  18,  21,  26,  66,  99,  99,  99,  99,
  24,  26,  56,  99,  99,  99,  99,  99,
  47,  66,  99,  99,  99,  99,  99,  99,
  99,  99,  99,  99,  99,  99,  99,  99,
  99,  99,  99,  99,  99,  99,  99,  99,
  99,  99,  99,  99,  99,  99,  99,  99,
  99,  99,  99,  99,  99,  99,  99,  99
  };
//}}}

typedef struct {
  JPEG_TypeDef*        Instance
  = nullptr;
  MDMA_HandleTypeDef   hmdmaIn;
  MDMA_HandleTypeDef   hmdmaOut;

  uint8_t*             InBuffPtr = nullptr;
  __IO uint32_t        InCount = 0;
  uint32_t             InLen = 0;

  uint8_t*             OutBuffPtr = nullptr;
  __IO uint32_t        OutCount = 0;
  uint32_t             OutLen = 0;

  uint8_t*             QuantTable0 = (uint8_t*)((uint32_t)LUM_QuantTable);
  uint8_t*             QuantTable1 = (uint8_t*)((uint32_t)CHROM_QuantTable);
  uint8_t*             QuantTable2 = nullptr;
  uint8_t*             QuantTable3 = nullptr;

  uint32_t             mWidth = 0;
  uint32_t             mHeight = 0;
  uint8_t              mColorSpace = 0;
  uint8_t              mChromaSampling = 0;  // 0-> 4:4:4  1-> 4:2:2 2-> 4:1:1 3-> 4:2:0

  __IO uint32_t        Context = 0;
  __IO uint32_t        mReadIndex = 0;
  __IO uint32_t        mWriteIndex = 0;
  __IO bool            mDecodeDone = false;
  } tHandle;
//}}}
//{{{  const
const uint32_t kYuvChunkSize = 0x10000;

//{{{
const JPEG_DCHuffTableTypeDef DCLUM_HuffTable = {
  { 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 },   /*Bits*/
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb }           /*HUFFVAL */
  };
//}}}
//{{{
const JPEG_DCHuffTableTypeDef DCCHROM_HuffTable = {
  { 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 },  /*Bits*/
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb }          /*HUFFVAL */
  };
//}}}

//{{{
const JPEG_ACHuffTableTypeDef ACLUM_HuffTable = {
  { 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d },  /*Bits*/
  {   0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,     /*HUFFVAL */
      0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
      0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
      0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
      0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
      0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
      0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
      0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
      0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
      0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
      0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
      0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
      0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
      0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
      0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
      0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
      0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
      0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
      0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
      0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
      0xf9, 0xfa }
  };
//}}}
//{{{
const JPEG_ACHuffTableTypeDef ACCHROM_HuffTable = {
  { 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 },   /*Bits*/
  {   0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,      /*HUFFVAL */
      0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
      0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
      0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
      0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
      0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
      0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
      0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
      0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
      0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
      0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
      0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
      0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
      0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
      0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
      0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
      0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
      0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
      0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
      0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
      0xf9, 0xfa }
  };
//}}}

//{{{
const uint8_t ZIGZAG_ORDER[JPEG_QUANT_TABLE_SIZE] = {
   0,   1,   8,  16,   9,   2,   3,  10,
  17,  24,  32,  25,  18,  11,   4,   5,
  12,  19,  26,  33,  40,  48,  41,  34,
  27,  20,  13,   6,   7,  14,  21,  28,
  35,  42,  49,  56,  57,  50,  43,  36,
  29,  22,  15,  23,  30,  37,  44,  51,
  58,  59,  52,  45,  38,  31,  39,  46,
  53,  60,  61,  54,  47,  55,  62,  63
  };
//}}}
//}}}

// vars
tHandle mHandle;

//{{{  yuvMcuTo565sw
//{{{  yuvMcuTo565sw defines
// YCbCr 4:2:0 : Each MCU is composed of 4 Y 8x8 blocks + 1 Cb 8x8 block + Cr 8x8 block
// YCbCr 4:2:2 : Each MCU is composed of 2 Y 8x8 blocks + 1 Cb 8x8 block + Cr 8x8 block
// YCbCr 4:4:4 : Each MCU is composed of 1 Y 8x8 block + 1 Cb 8x8 block + Cr 8x8 block

#define YCBCR_420_BLOCK_SIZE     384     /* YCbCr 4:2:0 MCU : 4 8x8 blocks of Y + 1 8x8 block of Cb + 1 8x8 block of Cr   */
#define YCBCR_422_BLOCK_SIZE     256     /* YCbCr 4:2:2 MCU : 2 8x8 blocks of Y + 1 8x8 block of Cb + 1 8x8 block of Cr   */
#define YCBCR_444_BLOCK_SIZE     192     /* YCbCr 4:4:4 MCU : 1 8x8 block of Y + 1 8x8 block of Cb + 1 8x8 block of Cr   */

#define JPEG_GREEN_OFFSET        5       /* Offset of the GREEN color in a pixel         */
#define JPEG_BYTES_PER_PIXEL     2       /* Number of bytes in a pixel                   */
#define JPEG_RGB565_GREEN_MASK   0x07E0  /* Mask of Green component in RGB565 Format     */

#define JPEG_RED_OFFSET          11      /* Offset of the RED color in a pixel           */
#define JPEG_BLUE_OFFSET         0       /* Offset of the BLUE color in a pixel          */
#define JPEG_RGB565_RED_MASK     0xF800  /* Mask of Red component in RGB565 Format       */
#define JPEG_RGB565_BLUE_MASK    0x001F  /* Mask of Blue component in RGB565 Format      */

#define CLAMP(value) CLAMP_LUT[(value) + 0x100] /* Range limitting macro */
//}}}
//{{{  yuvMcuTo565sw statics
static int32_t CR_RED_LUT[256];           /* Cr to Red color conversion Look Up Table  */
static int32_t CB_BLUE_LUT[256];          /* Cb to Blue color conversion Look Up Table */
static int32_t CR_GREEN_LUT[256];         /* Cr to Green color conversion Look Up Table*/
static int32_t CB_GREEN_LUT[256];         /* Cb to Green color conversion Look Up Table*/

static const uint8_t CLAMP_LUT[] = {
/* clamp range 0xffffffff to 0xffffff00 */
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

/* clamp range 0x00 to 0xff */
0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,

/* clamp range 0x100 to 0x1ff */
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};
//}}}
//{{{
void yuvMcuTo565sw (uint8_t* src, uint8_t* dst, uint32_t BlockIndex, uint32_t DataCount) {

  for (int32_t i = 0; i <= 255; i++) {
    int32_t index = (i * 2) - 256;
    CR_RED_LUT[i] = ( (((int32_t) ((1.40200 / 2) * (1L << 16)))  * index) + ((int32_t) 1 << (16 - 1))) >> 16;
    CB_BLUE_LUT[i] = ( (((int32_t) ((1.77200 / 2) * (1L << 16)))  * index) + ((int32_t) 1 << (16 - 1))) >> 16;
    CR_GREEN_LUT[i] = (-((int32_t) ((0.71414 / 2) * (1L << 16)))) * index;
    CB_GREEN_LUT[i] = (-((int32_t) ((0.34414 / 2) * (1L << 16)))) * index;
    }

  uint32_t ImageWidth = mHandle.mWidth;
  uint32_t ImageHeight = mHandle.mHeight;
  uint32_t ImageSize_Bytes = mHandle.mWidth * mHandle.mHeight * JPEG_BYTES_PER_PIXEL;

  uint32_t LineOffset = ImageWidth % 16;
  if (LineOffset != 0)
    LineOffset = 16 - LineOffset;
  uint32_t H_factor = 16;
  uint32_t  V_factor  = 8;

  uint32_t WidthExtend = ImageWidth + LineOffset;
  uint32_t ScaledWidth = JPEG_BYTES_PER_PIXEL * ImageWidth;

  uint32_t hMCU = (ImageWidth / H_factor);
  if ((ImageWidth % H_factor) != 0)
    hMCU++; /*+1 for horizenatl incomplete MCU */

  uint32_t vMCU = (ImageHeight / V_factor);
  if ((ImageHeight % V_factor) != 0)
    vMCU++; /*+1 for vertical incomplete MCU */

  uint32_t MCU_Total_Nb = hMCU * vMCU;

  uint32_t numberMCU = DataCount / YCBCR_422_BLOCK_SIZE;
  uint32_t currentMCU = BlockIndex;

  while (currentMCU < (numberMCU + BlockIndex)) {
    uint32_t xRef = ((currentMCU * 16) / WidthExtend) * 8;
    uint32_t yRef = ((currentMCU * 16) % WidthExtend);
    uint32_t refLine = ScaledWidth * xRef + (JPEG_BYTES_PER_PIXEL * yRef);
    currentMCU++;

    uint8_t* lumPtr = src;
    uint8_t* chrPtr = src + 128; /* chrPtr = src + 2*64 */

    for (uint32_t i = 0; i <  8; i++) {
      if (refLine < ImageSize_Bytes) {
        uint8_t* dstPtr = dst + refLine;

        for (uint32_t k = 0; k < 2; k++) {
          for (uint32_t j = 0; j < 8; j += 2) {
            int32_t cbcomp = (int32_t)(*(chrPtr));
            int32_t c_blue = (int32_t)(*(CB_BLUE_LUT + cbcomp));
            int32_t crcomp = (int32_t)(*(chrPtr + 64));
            int32_t c_red = (int32_t)(*(CR_RED_LUT + crcomp));
            int32_t c_green = ((int32_t)(*(CR_GREEN_LUT + crcomp)) + (int32_t)(*(CB_GREEN_LUT + cbcomp))) >> 16;

            int32_t ycomp = (int32_t)(*(lumPtr +j));

            *(__IO uint16_t*)dstPtr = ((CLAMP(ycomp + c_red) >> 3) << JPEG_RED_OFFSET) |
                                      ((CLAMP(ycomp + c_green) >> 2) << JPEG_GREEN_OFFSET) |
                                      ((CLAMP(ycomp + c_blue) >> 3) << JPEG_BLUE_OFFSET);
            ycomp = (int32_t)(*(lumPtr +j +1));

            *((__IO uint16_t*)(dstPtr + 2)) = ((CLAMP(ycomp + c_red) >> 3) << JPEG_RED_OFFSET) |
                                              ((CLAMP(ycomp + c_green) >> 2) << JPEG_GREEN_OFFSET) |
                                              ((CLAMP(ycomp + c_blue) >> 3) << JPEG_BLUE_OFFSET);
            dstPtr += JPEG_BYTES_PER_PIXEL * 2;
            chrPtr++;
            }
          lumPtr += 64;
          }
        lumPtr = lumPtr - 128 + 8;
        refLine += ScaledWidth;
        }
      }
    src += YCBCR_422_BLOCK_SIZE;
    }
  }
//}}}
//}}}

#define INBUF_SIZE 16384
tBufs mInBuf[2] = { { false, nullptr, 0 }, { false, nullptr, 0 } };

uint32_t mOutChunkSize = 0;
uint32_t mOutYuvLen = 0;
uint8_t* mOutYuvBuf = nullptr;
uint32_t mOutTotalLen = 0;
uint32_t mOutTotalChunks = 0;

//{{{
void outputData (uint32_t len) {

  mOutTotalLen += len;
  mOutTotalChunks++;

  //printf ("outputData %x %d\n", len);
  mHandle.OutBuffPtr += len;
  mHandle.OutLen = mOutChunkSize;
  }
//}}}
//{{{
void dmaDecode (uint8_t* inBuff, uint32_t inLen) {

  mHandle.Context = 0;

  mHandle.InBuffPtr = inBuff;
  mHandle.InLen = inLen;
  mHandle.InCount = 0;

  mHandle.OutBuffPtr = nullptr;
  mHandle.OutLen = 0;
  mHandle.OutCount = 0;

  mOutTotalLen = 0;
  mOutTotalChunks = 0;

  // set JPEG Codec to decode
  JPEG->CONFR1 |= JPEG_CONFR1_DE;

  // stop JPEG processing
  JPEG->CONFR0 &=  ~JPEG_CONFR0_START;
  __HAL_JPEG_DISABLE_IT (&mHandle, JPEG_INTERRUPT_MASK);

  // flush input and output FIFOs
  JPEG->CR |= JPEG_CR_IFF;
  JPEG->CR |= JPEG_CR_OFF;
  __HAL_JPEG_CLEAR_FLAG (&mHandle, JPEG_FLAG_ALL);

  // start decoding
  JPEG->CONFR0 |=  JPEG_CONFR0_START;

  // enable End Of Conversation, End Of Header interrupts
  __HAL_JPEG_ENABLE_IT (&mHandle, JPEG_IT_EOC | JPEG_IT_HPD);

  // if the MDMA In is triggred with JPEG In FIFO Threshold flag then MDMA In buffer size is 32 bytes
  // else (MDMA In is triggred with JPEG In FIFO not full flag then MDMA In buffer size is 4 bytes
  // MDMA transfer size (BNDTR) must be a multiple of MDMA buffer size (TLEN)
  uint32_t inXfrSize = mHandle.hmdmaIn.Init.BufferTransferLength;
  mHandle.InLen = mHandle.InLen - (mHandle.InLen % inXfrSize);

  // start DMA FIFO In transfer
  HAL_MDMA_Start_IT (&mHandle.hmdmaIn, (uint32_t)mHandle.InBuffPtr, (uint32_t)&JPEG->DIR, mHandle.InLen, 1);
  }
//}}}
//{{{
void dmaEnd() {

  mHandle.OutCount = mHandle.OutLen - (mHandle.hmdmaOut.Instance->CBNDTR & MDMA_CBNDTR_BNDT);

  if (mHandle.OutCount == mHandle.OutLen) {
    // Output Buffer full
    outputData (mHandle.OutCount);
    mHandle.OutCount = 0;
    }

  // Check if remaining data in the output FIFO
  if (__HAL_JPEG_GET_FLAG (&mHandle, JPEG_FLAG_OFNEF) == 0) {
    if (mHandle.OutCount > 0) {
      // Output Buffer not empty
      outputData (mHandle.OutCount);
      mHandle.OutCount = 0;
      }

    // stop decoding
    JPEG->CONFR0 &=  ~JPEG_CONFR0_START;
    }

  else {
    // dma residual data
    uint32_t count = JPEG_FIFO_SIZE;
    while ((__HAL_JPEG_GET_FLAG (&mHandle, JPEG_FLAG_OFNEF) != 0) && (count > 0) ) {
      count--;

      uint32_t dataOut = JPEG->DOR;
      mHandle.OutBuffPtr[mHandle.OutCount] = dataOut & 0x000000FF;
      mHandle.OutBuffPtr[mHandle.OutCount + 1] = (dataOut & 0x0000FF00) >> 8;
      mHandle.OutBuffPtr[mHandle.OutCount + 2] = (dataOut & 0x00FF0000) >> 16;
      mHandle.OutBuffPtr[mHandle.OutCount + 3] = (dataOut & 0xFF000000) >> 24;
      mHandle.OutCount += 4;

      if (mHandle.OutCount == mHandle.OutLen) {
        // Output Buffer full
        outputData (mHandle.OutCount);
        mHandle.OutCount = 0;
        }
      }

    // stop decoding
    JPEG->CONFR0 &=  ~JPEG_CONFR0_START;
    if (mHandle.OutCount > 0) {
      // Output Buffer not empty
      outputData (mHandle.OutCount);
      mHandle.OutCount = 0;
      }
    }

  mHandle.mDecodeDone = true;
  }
//}}}

// callbacks
//{{{
void dmaInCpltCallback (MDMA_HandleTypeDef* hmdma) {

  // Disable JPEG IT so DMA Input Callback can not be interrupted by the JPEG EOC IT or JPEG HPD IT
  __HAL_JPEG_DISABLE_IT (&mHandle, JPEG_INTERRUPT_MASK);

  if ((mHandle.Context & JPEG_CONTEXT_ENDING_DMA) == 0) {
    // if  MDMA In is triggred with JPEG In FIFO Threshold flag then MDMA In buffer size is 32 bytes
    // else MDMA In is triggred with JPEG In FIFO not full flag then MDMA In buffer size is 4 bytes
    uint32_t inXfrSize = mHandle.hmdmaIn.Init.BufferTransferLength;
    mHandle.InCount = mHandle.InLen - (hmdma->Instance->CBNDTR & MDMA_CBNDTR_BNDT);

    if (mHandle.InCount != mInBuf[mHandle.mReadIndex].mSize) {
      mHandle.InBuffPtr = mInBuf[mHandle.mReadIndex].mBuf + mHandle.InCount;
      mHandle.InLen = mInBuf[mHandle.mReadIndex].mSize - mHandle.InCount;
      }
    else {
      mInBuf [mHandle.mReadIndex].mFull = false;
      mInBuf [mHandle.mReadIndex].mSize = 0;
      mHandle.mReadIndex = mHandle.mReadIndex ? 0 : 1;
      if (mInBuf [mHandle.mReadIndex].mFull) {
        mHandle.InBuffPtr = mInBuf[mHandle.mReadIndex].mBuf;
        mHandle.InLen = mInBuf[mHandle.mReadIndex].mSize;
        }
      else
        // pause
        mHandle.Context |= JPEG_CONTEXT_PAUSE_INPUT;
      }

    if (mHandle.InLen >= inXfrSize)
      // JPEG Input DMA transfer len must be multiple of MDMA buffer size as destination is 32 bits reg
      mHandle.InLen = mHandle.InLen - (mHandle.InLen % inXfrSize);
    else if (mHandle.InLen > 0) {
      // Transfer remaining Data, multiple of source data size (byte) and destination data size (word)
      if ((mHandle.InLen % 4) != 0)
        mHandle.InLen = ((mHandle.InLen / 4) + 1) * 4;
      }

    // if not paused and some data, start MDMA FIFO In transfer
    if (((mHandle.Context & JPEG_CONTEXT_PAUSE_INPUT) == 0) && (mHandle.InLen > 0))
      HAL_MDMA_Start_IT (&mHandle.hmdmaIn, (uint32_t)mHandle.InBuffPtr, (uint32_t)&JPEG->DIR, mHandle.InLen, 1);

    // JPEG Conversion still on going : Enable the JPEG IT
    __HAL_JPEG_ENABLE_IT (&mHandle, JPEG_IT_EOC | JPEG_IT_HPD);
    }
  }
//}}}
//{{{
void dmaOutCpltCallback (MDMA_HandleTypeDef* hmdma) {

  // Disable JPEG IT so DMA Output Callback not interrupted by the JPEG EOC IT or JPEG HPD IT
  __HAL_JPEG_DISABLE_IT (&mHandle, JPEG_INTERRUPT_MASK);

  if ((mHandle.Context & JPEG_CONTEXT_ENDING_DMA) == 0) {
    if (__HAL_JPEG_GET_FLAG (&mHandle, JPEG_FLAG_EOCF) == 0) {
      mHandle.OutCount = mHandle.OutLen - (hmdma->Instance->CBNDTR & MDMA_CBNDTR_BNDT);

      // outputBuffer full
      outputData (mHandle.OutCount);

      // restart MDMA FIFO Out transfer
      HAL_MDMA_Start_IT (&mHandle.hmdmaOut, (uint32_t)&JPEG->DOR, (uint32_t)mHandle.OutBuffPtr, mHandle.OutLen, 1);
      }

    // JPEG Conversion still on going : Enable the JPEG IT
    __HAL_JPEG_ENABLE_IT (&mHandle, JPEG_IT_EOC | JPEG_IT_HPD);
    }
  }
//}}}
//{{{
void dmaErrorCallback (MDMA_HandleTypeDef* hmdma) {

  // Stop Decoding
  JPEG->CONFR0 &= ~JPEG_CONFR0_START;

  // Disable All Interrupts
  __HAL_JPEG_DISABLE_IT (&mHandle, JPEG_INTERRUPT_MASK);
  }

//}}}
//{{{
void dmaOutAbortCallback (MDMA_HandleTypeDef* hmdma) {

  if ((mHandle.Context & JPEG_CONTEXT_ENDING_DMA) != 0)
    dmaEnd();
  }
//}}}

//{{{
void init() {

  __HAL_RCC_JPGDECEN_CLK_ENABLE();
  __HAL_RCC_MDMA_CLK_ENABLE();

  //{{{  config input MDMA
  mHandle.hmdmaIn.Init.Priority       = MDMA_PRIORITY_HIGH;
  mHandle.hmdmaIn.Init.Endianness     = MDMA_LITTLE_ENDIANNESS_PRESERVE;
  mHandle.hmdmaIn.Init.SourceInc      = MDMA_SRC_INC_BYTE;
  mHandle.hmdmaIn.Init.DestinationInc = MDMA_DEST_INC_DISABLE;
  mHandle.hmdmaIn.Init.SourceDataSize = MDMA_SRC_DATASIZE_BYTE;
  mHandle.hmdmaIn.Init.DestDataSize   = MDMA_DEST_DATASIZE_WORD;
  mHandle.hmdmaIn.Init.DataAlignment  = MDMA_DATAALIGN_PACKENABLE;
  mHandle.hmdmaIn.Init.SourceBurst    = MDMA_SOURCE_BURST_32BEATS;
  mHandle.hmdmaIn.Init.DestBurst      = MDMA_DEST_BURST_16BEATS;
  mHandle.hmdmaIn.Init.SourceBlockAddressOffset = 0;
  mHandle.hmdmaIn.Init.DestBlockAddressOffset = 0;

  // use JPEG Input FIFO Threshold as a trigger for the MDMA
  // Set the MDMA HW trigger to JPEG Input FIFO Threshold flag
  // Set MDMA buffer size to JPEG FIFO threshold size 32bytes 8words
  mHandle.hmdmaIn.Init.Request = MDMA_REQUEST_JPEG_INFIFO_TH;
  mHandle.hmdmaIn.Init.TransferTriggerMode = MDMA_BUFFER_TRANSFER;
  mHandle.hmdmaIn.Init.BufferTransferLength = 32;
  mHandle.hmdmaIn.Instance = MDMA_Channel7;

  HAL_MDMA_DeInit (&mHandle.hmdmaIn);
  HAL_MDMA_Init (&mHandle.hmdmaIn);
  //}}}
  //{{{  config output MDMA
  mHandle.hmdmaOut.Init.Priority       = MDMA_PRIORITY_VERY_HIGH;
  mHandle.hmdmaOut.Init.Endianness     = MDMA_LITTLE_ENDIANNESS_PRESERVE;
  mHandle.hmdmaOut.Init.SourceInc      = MDMA_SRC_INC_DISABLE;
  mHandle.hmdmaOut.Init.DestinationInc = MDMA_DEST_INC_BYTE;
  mHandle.hmdmaOut.Init.SourceDataSize = MDMA_SRC_DATASIZE_WORD;
  mHandle.hmdmaOut.Init.DestDataSize   = MDMA_DEST_DATASIZE_BYTE;
  mHandle.hmdmaOut.Init.DataAlignment  = MDMA_DATAALIGN_PACKENABLE;
  mHandle.hmdmaOut.Init.SourceBurst    = MDMA_SOURCE_BURST_32BEATS;
  mHandle.hmdmaOut.Init.DestBurst      = MDMA_DEST_BURST_32BEATS;
  mHandle.hmdmaOut.Init.SourceBlockAddressOffset = 0;
  mHandle.hmdmaOut.Init.DestBlockAddressOffset = 0;

  // use JPEG Output FIFO Threshold as a trigger for the MDMA
  // Set the MDMA HW trigger to JPEG Output FIFO Threshold flag
  // Set MDMA buffer size to JPEG FIFO threshold size 32bytes 8words
  mHandle.hmdmaOut.Init.Request = MDMA_REQUEST_JPEG_OUTFIFO_TH;
  mHandle.hmdmaOut.Init.TransferTriggerMode = MDMA_BUFFER_TRANSFER;
  mHandle.hmdmaOut.Init.BufferTransferLength = 32;
  mHandle.hmdmaOut.Instance = MDMA_Channel6;

  HAL_MDMA_DeInit (&mHandle.hmdmaOut);
  HAL_MDMA_Init (&mHandle.hmdmaOut);
  //}}}
  HAL_NVIC_SetPriority (MDMA_IRQn, 0x08, 0x0F);
  HAL_NVIC_EnableIRQ (MDMA_IRQn);

  HAL_NVIC_SetPriority (JPEG_IRQn, 0x07, 0x0F);
  HAL_NVIC_EnableIRQ (JPEG_IRQn);

  __HAL_JPEG_ENABLE (&mHandle);

  // stop the JPEG decoding process
  JPEG->CONFR0 &= ~JPEG_CONFR0_START;

  __HAL_JPEG_DISABLE_IT (&mHandle, JPEG_INTERRUPT_MASK);

  // flush input and output FIFOs
  JPEG->CR |= JPEG_CR_IFF;
  JPEG->CR |= JPEG_CR_OFF;
  __HAL_JPEG_CLEAR_FLAG (&mHandle, JPEG_FLAG_ALL);

  // enable header processing
  JPEG->CONFR1 |= JPEG_CONFR1_HDR;

  mHandle.hmdmaIn.XferCpltCallback = dmaInCpltCallback;
  mHandle.hmdmaIn.XferErrorCallback = dmaErrorCallback;
  mHandle.hmdmaOut.XferCpltCallback = dmaOutCpltCallback;
  mHandle.hmdmaOut.XferErrorCallback = dmaErrorCallback;
  mHandle.hmdmaOut.XferAbortCallback = dmaOutAbortCallback;
  }
//}}}

//{{{
extern "C" { void JPEG_IRQHandler() {

  if (__HAL_JPEG_GET_FLAG (&mHandle, JPEG_FLAG_HPDF) != RESET) {
    //{{{  end of header read header values from conf regs
    mHandle.mWidth  = (JPEG->CONFR3 & 0xFFFF0000U) >> 16;
    mHandle.mHeight = (JPEG->CONFR1 & 0xFFFF0000U) >> 16;

    if ((JPEG->CONFR1 & JPEG_CONFR1_NF) == JPEG_CONFR1_NF_1)
      mHandle.mColorSpace = JPEG_YCBCR_COLORSPACE;
    else if ((JPEG->CONFR1 & JPEG_CONFR1_NF) == 0)
      mHandle.mColorSpace = JPEG_GRAYSCALE_COLORSPACE;
    else if ((JPEG->CONFR1 & JPEG_CONFR1_NF) == JPEG_CONFR1_NF)
      mHandle.mColorSpace = JPEG_CMYK_COLORSPACE;

    if ((mHandle.mColorSpace == JPEG_YCBCR_COLORSPACE) || (mHandle.mColorSpace == JPEG_CMYK_COLORSPACE)) {
      uint32_t yblockNb  = (JPEG->CONFR4 & JPEG_CONFR4_NB) >> 4;
      uint32_t cBblockNb = (JPEG->CONFR5 & JPEG_CONFR5_NB) >> 4;
      uint32_t cRblockNb = (JPEG->CONFR6 & JPEG_CONFR6_NB) >> 4;

      if ((yblockNb == 1) && (cBblockNb == 0) && (cRblockNb == 0))
        mHandle.mChromaSampling = JPEG_422_SUBSAMPLING; // 16x8 block
      else if ((yblockNb == 0) && (cBblockNb == 0) && (cRblockNb == 0))
        mHandle.mChromaSampling = JPEG_444_SUBSAMPLING;
      else if ((yblockNb == 3) && (cBblockNb == 0) && (cRblockNb == 0))
        mHandle.mChromaSampling = JPEG_420_SUBSAMPLING;
      else
        mHandle.mChromaSampling = JPEG_444_SUBSAMPLING;
      }
    else
      mHandle.mChromaSampling = JPEG_444_SUBSAMPLING;
    //}}}

    // clear header processing done flag
    __HAL_JPEG_DISABLE_IT (&mHandle, JPEG_IT_HPD);
    __HAL_JPEG_CLEAR_FLAG (&mHandle, JPEG_FLAG_HPDF);

    //{{{  calc mOutYuvLen from mHandle.mChromaSampling
    if (mHandle.mChromaSampling == JPEG_444_SUBSAMPLING) {
      mOutChunkSize = (mHandle.mWidth / 8) * 192;
      mOutYuvLen = mHandle.mWidth * mHandle.mHeight * 3;
      printf ("- JPEG header 422 %dx%d %d chunk:%d\n", mHandle.mWidth, mHandle.mHeight, mOutYuvLen,mOutChunkSize);
      }

    else if (mHandle.mChromaSampling == JPEG_420_SUBSAMPLING) {
      mOutChunkSize = (mHandle.mWidth / 32) * 384;
      mOutYuvLen = (mHandle.mWidth * mHandle.mHeight * 3) / 2;
      printf ("- JPEG header 420 %dx%d %d chunk:%d\n", mHandle.mWidth, mHandle.mHeight, mOutYuvLen, mOutChunkSize);
      }

    else if (mHandle.mChromaSampling == JPEG_422_SUBSAMPLING) {
      mOutChunkSize = (mHandle.mWidth / 16) * 256;
      mOutYuvLen = mHandle.mWidth * mHandle.mHeight * 2;
      printf ("- JPEG header 422 %dx%d %d chunk:%d\n", mHandle.mWidth, mHandle.mHeight, mOutYuvLen, mOutChunkSize);
      }

    else
      printf ("JPEG unrecognised chroma sampling %d\n", mHandle.mChromaSampling);
    //}}}
    // alloc yuv
    mOutYuvBuf = sdRamAllocInt (mOutYuvLen);
    if (!mOutYuvBuf)
      printf ("JPEG mOutYuvBuf alloc fail\n");

    // if the MDMA Out is triggred with JPEG Out FIFO Threshold flag then MDMA out buffer size is 32 bytes
    // else (MDMA Out is triggred with JPEG Out FIFO not empty flag then MDMA buffer size is 4 bytes
    // MDMA transfer size (BNDTR) must be a multiple of MDMA buffer size (TLEN)
    mHandle.OutBuffPtr = mOutYuvBuf;
    mHandle.OutLen = mOutChunkSize;
    uint32_t outXfrSize = mHandle.hmdmaOut.Init.BufferTransferLength;
    mHandle.OutLen = mHandle.OutLen - (mHandle.OutLen % outXfrSize);

    // start MDMA FIFO Out transfer
    HAL_MDMA_Start_IT (&mHandle.hmdmaOut, (uint32_t)&JPEG->DOR, (uint32_t)mHandle.OutBuffPtr, mHandle.OutLen, 1);
    }

  if (__HAL_JPEG_GET_FLAG (&mHandle, JPEG_FLAG_EOCF) != RESET) {
    // end of conversion
    mHandle.Context |= JPEG_CONTEXT_ENDING_DMA;

    // stop decoding
    mHandle.Instance->CONFR0 &= ~JPEG_CONFR0_START;
    __HAL_JPEG_DISABLE_IT (&mHandle, JPEG_INTERRUPT_MASK);
    __HAL_JPEG_CLEAR_FLAG (&mHandle, JPEG_FLAG_ALL);

    if (mHandle.hmdmaIn.State == HAL_MDMA_STATE_BUSY)
      // Stop the MDMA In Xfer
      HAL_MDMA_Abort_IT (&mHandle.hmdmaIn);

    if (mHandle.hmdmaOut.State == HAL_MDMA_STATE_BUSY)
      // Stop the MDMA out Xfer
      HAL_MDMA_Abort_IT (&mHandle.hmdmaOut);
    else
      dmaEnd();
    }
  }
}
//}}}
//{{{
extern "C" { void MDMA_IRQHandler() {
  HAL_MDMA_IRQHandler (&mHandle.hmdmaIn);
  HAL_MDMA_IRQHandler (&mHandle.hmdmaOut);
  }
}
//}}}

//{{{
size_t read_file (FIL* file, uint8_t* buf, uint32_t sizeofbuf) {

  size_t bytesRead;
  f_read (file, buf, sizeofbuf, &bytesRead);
  //printf ("read_file %p %d:%d\n", buf, sizeofbuf, bytesRead);
  return bytesRead;
  }
//}}}

// interface
//{{{
cTile* hwJpegDecode (const string& fileName) {

  mHandle.Instance = JPEG;
  init();

  mInBuf[0].mBuf = (uint8_t*)pvPortMalloc (INBUF_SIZE);
  mInBuf[1].mBuf = (uint8_t*)pvPortMalloc (INBUF_SIZE);
  mOutYuvBuf = nullptr;

  cTile* tile = nullptr;
  FIL file;
  if (f_open (&file, fileName.c_str(), FA_READ) == FR_OK) {
    if (f_read (&file, mInBuf[0].mBuf, INBUF_SIZE, &mInBuf[0].mSize) == FR_OK)
      mInBuf[0].mFull = true;
    if (f_read (&file, mInBuf[1].mBuf, INBUF_SIZE, &mInBuf[1].mSize) == FR_OK)
      mInBuf[1].mFull = true;

    mHandle.mReadIndex = 0;
    mHandle.mWriteIndex = 0;
    mHandle.mDecodeDone = false;
    dmaDecode (mInBuf[0].mBuf, mInBuf[0].mSize);

    while (!mHandle.mDecodeDone) {
      if (!mInBuf[mHandle.mWriteIndex].mFull) {
        // fill next buffer
        if (f_read (&file, mInBuf[mHandle.mWriteIndex].mBuf, INBUF_SIZE, &mInBuf[mHandle.mWriteIndex].mSize) == FR_OK)
          mInBuf[mHandle.mWriteIndex].mFull = true;

        if (((mHandle.Context & JPEG_CONTEXT_PAUSE_INPUT) != 0) && (mHandle.mWriteIndex == mHandle.mReadIndex)) {
          // resume
          mHandle.InBuffPtr = mInBuf[mHandle.mReadIndex].mBuf;
          mHandle.InLen = mInBuf[mHandle.mReadIndex].mSize;
          mHandle.Context &= ~JPEG_CONTEXT_PAUSE_INPUT;

          // if MDMA In is triggred with JPEG In FIFO Threshold flag then MDMA In buffer size is 32 bytes
          // else MDMA In is triggred with JPEG In FIFO not full flag then MDMA In buffer size is 4 bytes
          // MDMA transfer size (BNDTR) must be a multiple of MDMA buffer size (TLEN)
          uint32_t xfrSize = mHandle.hmdmaIn.Init.BufferTransferLength;
          mHandle.InLen = mHandle.InLen - (mHandle.InLen % xfrSize);
          if (mHandle.InLen > 0) // Start DMA FIFO In transfer
            HAL_MDMA_Start_IT (&mHandle.hmdmaIn, (uint32_t)mHandle.InBuffPtr, (uint32_t)&JPEG->DIR, mHandle.InLen, 1);
          }
        mHandle.mWriteIndex = mHandle.mWriteIndex ? 0 : 1;
        }
      }
    f_close (&file);

    printf ("- JPEG decode %p %d:%dx%d - out %d of %d chunks %d\n",
            mOutYuvBuf, mHandle.mChromaSampling, mHandle.mWidth, mHandle.mHeight,
            mOutTotalLen, mOutYuvLen, mOutTotalChunks);

    // alloc rgb
    auto rgb565 = sdRamAllocInt (mHandle.mWidth * mHandle.mHeight * 2);
    if (!rgb565)
      printf ("JPEG rgb565 alloc fail\n");
    else {
      tile = new cTile (rgb565, 2, mHandle.mWidth, 0, 0, mHandle.mWidth,  mHandle.mHeight);
      cLcd::yuvMcuToRgb565 (mOutYuvBuf, rgb565, mHandle.mWidth, mHandle.mHeight, mHandle.mChromaSampling);
      }
    }

  vPortFree (mInBuf[0].mBuf);
  vPortFree (mInBuf[1].mBuf);
  sdRamFree (mOutYuvBuf);

  return tile;
  }
//}}}

//{{{
cTile* swJpegDecode (const string& fileName, int scale) {

  cTile* tile = nullptr;

  FIL file;
  if (f_open (&file, fileName.c_str(), FA_READ))
    printf ("swJpegDecode %s open fail\n", fileName.c_str());
  else {
    printf ("swJpegDecode %s start decoding\n", fileName.c_str());

    struct jpeg_error_mgr jerr;
    struct jpeg_decompress_struct mCinfo;
    mCinfo.err = jpeg_std_error (&jerr);
    jpeg_create_decompress (&mCinfo);

    jpeg_stdio_src (&mCinfo, &file);
    jpeg_read_header (&mCinfo, TRUE);

    mCinfo.dct_method = JDCT_FLOAT;
    mCinfo.out_color_space = JCS_RGB;
    mCinfo.scale_num = 1;
    mCinfo.scale_denom = scale;
    jpeg_start_decompress (&mCinfo);

    auto rgb565Pic = sdRamAlloc (mCinfo.output_width * mCinfo.output_height*2);
    if (!rgb565Pic)
      printf ("swJpegDecode %s rgb565pic alloc fail\n", fileName.c_str());
    else {
      auto rgb888Line = (uint8_t*)sram123Alloc (mCinfo.output_width * 3);
      if (!rgb888Line)
        printf ("swJpegDecode %s rgbLine alloc fail\n", fileName.c_str());
      else {
        tile = new cTile (rgb565Pic, 2, mCinfo.output_width, 0,0, mCinfo.output_width, mCinfo.output_height);
        while (mCinfo.output_scanline < mCinfo.output_height) {
          jpeg_read_scanlines (&mCinfo, &rgb888Line, 1);
          cLcd::rgb888toRgb565 (rgb888Line, rgb565Pic, mCinfo.output_width, 1);
          rgb565Pic += mCinfo.output_width * 2;
          }
        sram123Free (rgb888Line);
        }
      }
    jpeg_finish_decompress (&mCinfo);
    jpeg_destroy_decompress (&mCinfo);
    f_close (&file);
    }

  return tile;
  }
//}}}
