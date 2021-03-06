#include "stm32h7xx_hal.h"
//{{{  defines
#define JPEG_TIMEOUT_VALUE  ((uint32_t)1000U)     /* 1s */
#define JPEG_AC_HUFF_TABLE_SIZE  ((uint32_t)162U) /* Huffman AC table size : 162 codes*/
#define JPEG_DC_HUFF_TABLE_SIZE  ((uint32_t)12U)  /* Huffman AC table size : 12 codes*/

#define JPEG_FIFO_SIZE    ((uint32_t)16U) /* JPEG Input/Output HW FIFO size in words*/
#define JPEG_FIFO_TH_SIZE ((uint32_t)8U)  /* JPEG Input/Output HW FIFO Threshold in words*/

#define JPEG_INTERRUPT_MASK  ((uint32_t)0x0000007EU) /* JPEG Interrupt Mask*/

#define JPEG_CONTEXT_ENCODE          ((uint32_t)0x00000001U) /* JPEG context : operation is encoding*/
#define JPEG_CONTEXT_DECODE          ((uint32_t)0x00000002U) /* JPEG context : operation is decoding*/
#define JPEG_CONTEXT_OPERATION_MASK  ((uint32_t)0x00000003U) /* JPEG context : operation Mask */

#define JPEG_CONTEXT_POLLING        ((uint32_t)0x00000004U)  /* JPEG context : Transfer use Polling */
#define JPEG_CONTEXT_IT             ((uint32_t)0x00000008U)  /* JPEG context : Transfer use Interrupt */
#define JPEG_CONTEXT_DMA            ((uint32_t)0x0000000CU)  /* JPEG context : Transfer use DMA */
#define JPEG_CONTEXT_METHOD_MASK    ((uint32_t)0x0000000CU)  /* JPEG context : Transfer Mask */

#define JPEG_CONTEXT_CONF_ENCODING  ((uint32_t)0x00000100U)  /* JPEG context : encoding config done */

#define JPEG_CONTEXT_PAUSE_INPUT    ((uint32_t)0x00001000U)  /* JPEG context : Pause Input */
#define JPEG_CONTEXT_PAUSE_OUTPUT   ((uint32_t)0x00002000U)  /* JPEG context : Pause Output */

#define JPEG_CONTEXT_CUSTOM_TABLES  ((uint32_t)0x00004000U)  /* JPEG context : Use custom quantization tables */

#define JPEG_CONTEXT_ENDING_DMA     ((uint32_t)0x00008000U)  /* JPEG context : ending with DMA in progress */

#define JPEG_PROCESS_ONGOING        ((uint32_t)0x00000000U)  /* Process is on going */
#define JPEG_PROCESS_DONE           ((uint32_t)0x00000001U)  /* Process is done (ends) */
//}}}
//{{{  struct JPEG_AC_HuffCodeTableTypeDef
typedef struct {
  uint8_t CodeLength[JPEG_AC_HUFF_TABLE_SIZE];      /*!< Code length  */
  uint32_t HuffmanCode[JPEG_AC_HUFF_TABLE_SIZE];    /*!< HuffmanCode */
  } JPEG_AC_HuffCodeTableTypeDef;
//}}}
//{{{  struct JPEG_DC_HuffCodeTableTypeDef
typedef struct {
  uint8_t CodeLength[JPEG_DC_HUFF_TABLE_SIZE];        /*!< Code length  */
  uint32_t HuffmanCode[JPEG_DC_HUFF_TABLE_SIZE];    /*!< HuffmanCode */
  } JPEG_DC_HuffCodeTableTypeDef;
//}}}

//{{{  struct JPEG_DCHuffTableTypeDef
typedef struct {
  /* These two fields directly represent the contents of a JPEG DHT marker */
  uint8_t Bits[16];     /*!< bits[k] = # of symbols with codes of length k bits, this parameter corresponds to BITS list in the Annex C */
  uint8_t HuffVal[12];  /*!< The symbols, in order of incremented code length, this parameter corresponds to HUFFVAL list in the Annex C */
  }JPEG_DCHuffTableTypeDef;
//}}}
//{{{
static const JPEG_DCHuffTableTypeDef DCLUM_HuffTable = {
  { 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 },   /*Bits*/
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb }           /*HUFFVAL */
  };
//}}}
//{{{
static const JPEG_DCHuffTableTypeDef DCCHROM_HuffTable = {
  { 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 },  /*Bits*/
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb }          /*HUFFVAL */
  };
//}}}

//{{{  struct JPEG_ACHuffTableTypeDef
/*
 JPEG Huffman Table Structure definition :
 This implementation of Huffman table structure is compliant with ISO/IEC 10918-1 standard , Annex C Huffman Table specification
 */
typedef struct {
  /* These two fields directly represent the contents of a JPEG DHT marker */
  uint8_t Bits[16];        /*!< bits[k] = # of symbols with codes of length k bits, this parameter corresponds to BITS list in the Annex C */
  uint8_t HuffVal[162];    /*!< The symbols, in order of incremented code length, this parameter corresponds to HUFFVAL list in the Annex C */
  } JPEG_ACHuffTableTypeDef;
//}}}
//{{{
static const JPEG_ACHuffTableTypeDef ACLUM_HuffTable = {
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
static const JPEG_ACHuffTableTypeDef ACCHROM_HuffTable = {
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
static const uint8_t LUM_QuantTable[JPEG_QUANT_TABLE_SIZE] = {
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
static const uint8_t CHROM_QuantTable[JPEG_QUANT_TABLE_SIZE] = {
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
//{{{
static const uint8_t ZIGZAG_ORDER[JPEG_QUANT_TABLE_SIZE] = {
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

//{{{
static void SetHuffDHTMem (JPEG_HandleTypeDef* hjpeg, JPEG_ACHuffTableTypeDef *HuffTableAC0, JPEG_DCHuffTableTypeDef *HuffTableDC0 ,  JPEG_ACHuffTableTypeDef *HuffTableAC1, JPEG_DCHuffTableTypeDef *HuffTableDC1)
{
  uint32_t value, index;
  __IO uint32_t* address;

  if (HuffTableDC0 != NULL) {
    /* DC0 Huffman Table : BITS*/
    /* DC0 BITS is a 16 Bytes table i.e 4x32bits words from DHTMEM base address to DHTMEM + 3*/
    address = (hjpeg->Instance->DHTMEM + 3);
    index = 16;
    while(index > 0) {
      *address = (((uint32_t)HuffTableDC0->Bits[index-1] & 0xFF) << 24)|
                 (((uint32_t)HuffTableDC0->Bits[index-2] & 0xFF) << 16)|
                 (((uint32_t)HuffTableDC0->Bits[index-3] & 0xFF) << 8) |
                 ((uint32_t)HuffTableDC0->Bits[index-4] & 0xFF);
      address--;
      index -=4;
      }

    /* DC0 Huffman Table : Val*/
    /* DC0 VALS is a 12 Bytes table i.e 3x32bits words from DHTMEM base address +4 to DHTMEM + 6 */
    address = (hjpeg->Instance->DHTMEM + 6);
    index = 12;
    while(index > 0) {
      *address = (((uint32_t)HuffTableDC0->HuffVal[index-1] & 0xFF) << 24)|
                 (((uint32_t)HuffTableDC0->HuffVal[index-2] & 0xFF) << 16)|
                 (((uint32_t)HuffTableDC0->HuffVal[index-3] & 0xFF) << 8) |
                 ((uint32_t)HuffTableDC0->HuffVal[index-4] & 0xFF);
      address--;
      index -=4;
      }
    }

  if (HuffTableAC0 != NULL) {
    /* AC0 Huffman Table : BITS*/
    /* AC0 BITS is a 16 Bytes table i.e 4x32bits words from DHTMEM base address + 7 to DHTMEM + 10*/
    address = (hjpeg->Instance->DHTMEM + 10);
    index = 16;
    while(index > 0) {
      *address = (((uint32_t)HuffTableAC0->Bits[index-1] & 0xFF) << 24)|
                 (((uint32_t)HuffTableAC0->Bits[index-2] & 0xFF) << 16)|
                 (((uint32_t)HuffTableAC0->Bits[index-3] & 0xFF) << 8) |
                 ((uint32_t)HuffTableAC0->Bits[index-4] & 0xFF);
      address--;
      index -=4;
      }
    /* AC0 Huffman Table : Val*/
    /* AC0 VALS is a 162 Bytes table i.e 41x32bits words from DHTMEM base address + 11 to DHTMEM + 51 */
    /* only Byte 0 and Byte 1 of the last word (@ DHTMEM + 51) belong to AC0 VALS table */
    address = (hjpeg->Instance->DHTMEM + 51);
    value = *address & 0xFFFF0000U;
    value = value | (((uint32_t)HuffTableAC0->HuffVal[161] & 0xFF) << 8) | ((uint32_t)HuffTableAC0->HuffVal[160] & 0xFF);
    *address = value;

    /*continue setting 160 AC0 huffman values */
    address--; /* address = hjpeg->Instance->DHTMEM + 50*/
    index = 160;
    while(index > 0) {
      *address = (((uint32_t)HuffTableAC0->HuffVal[index-1] & 0xFF) << 24)|
                 (((uint32_t)HuffTableAC0->HuffVal[index-2] & 0xFF) << 16)|
                 (((uint32_t)HuffTableAC0->HuffVal[index-3] & 0xFF) << 8) |
                 ((uint32_t)HuffTableAC0->HuffVal[index-4] & 0xFF);
      address--;
      index -=4;
      }
    }

  if (HuffTableDC1 != NULL) {
    /* DC1 Huffman Table : BITS*/
    /* DC1 BITS is a 16 Bytes table i.e 4x32bits words from DHTMEM + 51 base address to DHTMEM + 55*/
    /* only Byte 2 and Byte 3 of the first word (@ DHTMEM + 51) belong to DC1 Bits table */
    address = (hjpeg->Instance->DHTMEM + 51);
    value = *address & 0x0000FFFFU;
    value = value | (((uint32_t)HuffTableDC1->Bits[1] & 0xFF) << 24) | (((uint32_t)HuffTableDC1->Bits[0] & 0xFF) << 16);
    *address = value;

    /* only Byte 0 and Byte 1 of the last word (@ DHTMEM + 55) belong to DC1 Bits table */
    address = (hjpeg->Instance->DHTMEM + 55);
    value = *address & 0xFFFF0000U;
    value = value | (((uint32_t)HuffTableDC1->Bits[15] & 0xFF) << 8) | ((uint32_t)HuffTableDC1->Bits[14] & 0xFF);
    *address = value;

    /*continue setting 12 DC1 huffman Bits from DHTMEM + 54 down to DHTMEM + 52*/
    address--;
    index = 12;
    while(index > 0) {
      *address = (((uint32_t)HuffTableDC1->Bits[index+1] & 0xFF) << 24)|
                 (((uint32_t)HuffTableDC1->Bits[index] & 0xFF) << 16)|
                 (((uint32_t)HuffTableDC1->Bits[index-1] & 0xFF) << 8) |
                 ((uint32_t)HuffTableDC1->Bits[index-2] & 0xFF);
      address--;
      index -=4;
      }
    /* DC1 Huffman Table : Val*/
    /* DC1 VALS is a 12 Bytes table i.e 3x32bits words from DHTMEM base address +55 to DHTMEM + 58 */
    /* only Byte 2 and Byte 3 of the first word (@ DHTMEM + 55) belong to DC1 Val table */
    address = (hjpeg->Instance->DHTMEM + 55);
    value = *address & 0x0000FFFF;
    value = value | (((uint32_t)HuffTableDC1->HuffVal[1] & 0xFF) << 24) | (((uint32_t)HuffTableDC1->HuffVal[0] & 0xFF) << 16);
    *address = value;

    /* only Byte 0 and Byte 1 of the last word (@ DHTMEM + 58) belong to DC1 Val table */
    address = (hjpeg->Instance->DHTMEM + 58);
    value = *address & 0xFFFF0000U;
    value = value | (((uint32_t)HuffTableDC1->HuffVal[11] & 0xFF) << 8) | ((uint32_t)HuffTableDC1->HuffVal[10] & 0xFF);
    *address = value;

    /*continue setting 8 DC1 huffman val from DHTMEM + 57 down to DHTMEM + 56*/
    address--;
    index = 8;
    while(index > 0) {
      *address = (((uint32_t)HuffTableDC1->HuffVal[index+1] & 0xFF) << 24)|
                 (((uint32_t)HuffTableDC1->HuffVal[index] & 0xFF) << 16)|
                 (((uint32_t)HuffTableDC1->HuffVal[index-1] & 0xFF) << 8) |
                 ((uint32_t)HuffTableDC1->HuffVal[index-2] & 0xFF);
      address--;
      index -=4;
    }
  }

  if (HuffTableAC1 != NULL) {
    /* AC1 Huffman Table : BITS*/
    /* AC1 BITS is a 16 Bytes table i.e 4x32bits words from DHTMEM base address + 58 to DHTMEM + 62*/
    /* only Byte 2 and Byte 3 of the first word (@ DHTMEM + 58) belong to AC1 Bits table */
    address = (hjpeg->Instance->DHTMEM + 58);
    value = *address & 0x0000FFFFU;
    value = value | (((uint32_t)HuffTableAC1->Bits[1] & 0xFF) << 24) | (((uint32_t)HuffTableAC1->Bits[0] & 0xFF) << 16);
    *address = value;

    /* only Byte 0 and Byte 1 of the last word (@ DHTMEM + 62) belong to Bits Val table */
    address = (hjpeg->Instance->DHTMEM + 62);
    value = *address & 0xFFFF0000U;
    value = value | (((uint32_t)HuffTableAC1->Bits[15] & 0xFF) << 8) | ((uint32_t)HuffTableAC1->Bits[14] & 0xFF);
    *address = value;

    /*continue setting 12 AC1 huffman Bits from DHTMEM + 61 down to DHTMEM + 59*/
    address--;
    index = 12;
    while(index > 0) {
      *address = (((uint32_t)HuffTableAC1->Bits[index+1] & 0xFF) << 24)|
                 (((uint32_t)HuffTableAC1->Bits[index] & 0xFF) << 16)|
                 (((uint32_t)HuffTableAC1->Bits[index-1] & 0xFF) << 8) |
                 ((uint32_t)HuffTableAC1->Bits[index-2] & 0xFF);
      address--;
      index -=4;
      }

    /* AC1 Huffman Table : Val*/
    /* AC1 VALS is a 162 Bytes table i.e 41x32bits words from DHTMEM base address + 62 to DHTMEM + 102 */
    /* only Byte 2 and Byte 3 of the first word (@ DHTMEM + 62) belong to AC1 VALS table */
    address = (hjpeg->Instance->DHTMEM + 62);
    value = *address & 0x0000FFFF;
    value = value | (((uint32_t)HuffTableAC1->HuffVal[1] & 0xFF) << 24) | (((uint32_t)HuffTableAC1->HuffVal[0] & 0xFF) << 16);
    *address = value;

    /*continue setting 160 AC1 huffman values from DHTMEM + 63 to DHTMEM+102 */
    address = (hjpeg->Instance->DHTMEM + 102);
    index = 160;
    while(index > 0) {
      *address = (((uint32_t)HuffTableAC1->HuffVal[index+1] & 0xFF) << 24)|
                 (((uint32_t)HuffTableAC1->HuffVal[index] & 0xFF) << 16)|
                 (((uint32_t)HuffTableAC1->HuffVal[index-1] & 0xFF) << 8) |
                 ((uint32_t)HuffTableAC1->HuffVal[index-2] & 0xFF);
      address--;
      index -=4;
      }
    }
  }
//}}}
//{{{
static HAL_StatusTypeDef BitsToSizeCodes (uint8_t* Bits, uint8_t *Huffsize, uint32_t *Huffcode, uint32_t *LastK)
{
  uint32_t i, p, l, code, si;

  /* Figure C.1: Generation of table of Huffman code sizes */
  p = 0;
  for (l = 0; l < 16; l++) {
    i = (uint32_t)Bits[l];
    if ( (p + i) > 256)
      return HAL_ERROR;
    while (i != 0) {
      Huffsize[p++] = (uint8_t) l+1;
      i--;
      }
    }
  Huffsize[p] = 0;
  *LastK = p;

  /* Figure C.2: Generation of table of Huffman codes */
  code = 0;
  si = Huffsize[0];
  p = 0;
  while (Huffsize[p] != 0) {
    while (((uint32_t) Huffsize[p]) == si) {
      Huffcode[p++] = code;
      code++;
      }
    /* code must fit in "size" bits (si), no code is allowed to be all ones*/
    if (((uint32_t) code) >= (((uint32_t) 1) << si))
      return HAL_ERROR;
    code <<= 1;
    si++;
    }

  /* Return function status */
  return HAL_OK;
  }
//}}}
//{{{
static HAL_StatusTypeDef ACHuffBitsValsToSizeCodes (JPEG_ACHuffTableTypeDef* AC_BitsValsTable,
                                                    JPEG_AC_HuffCodeTableTypeDef* AC_SizeCodesTable) {

  HAL_StatusTypeDef error;
  uint8_t huffsize[257];
  uint32_t huffcode[257];
  uint32_t lastK;
  error = BitsToSizeCodes (AC_BitsValsTable->Bits, huffsize, huffcode, &lastK);
  if (error != HAL_OK)
    return  error;

  /* Figure C.3: Ordering procedure for encoding procedure code tables */
  uint32_t k = 0;
  while (k < lastK) {
    uint32_t l = AC_BitsValsTable->HuffVal[k];
    if (l == 0)
      l = 160; /*l = 0x00 EOB code*/
    else if(l == 0xF0)/* l = 0xF0 ZRL code*/
      l = 161;
    else {
      uint32_t msb = (l & 0xF0) >> 4;
      uint32_t lsb = (l & 0x0F);
      l = (msb * 10) + lsb - 1;
      }
    if (l >= JPEG_AC_HUFF_TABLE_SIZE)
      return HAL_ERROR; /* Huffman Table overflow error*/
    else {
      AC_SizeCodesTable->HuffmanCode[l] = huffcode[k];
      AC_SizeCodesTable->CodeLength[l] = huffsize[k] - 1;
      k++;
      }
    }

  return HAL_OK;
  }
//}}}
//{{{
static HAL_StatusTypeDef DCHuffBitsValsToSizeCodes (JPEG_DCHuffTableTypeDef* DC_BitsValsTable,
                                                    JPEG_DC_HuffCodeTableTypeDef* DC_SizeCodesTable) {

  HAL_StatusTypeDef error;

  uint32_t lastK;
  uint8_t huffsize[257];
  uint32_t huffcode[257];
  error = BitsToSizeCodes (DC_BitsValsTable->Bits, huffsize, huffcode, &lastK);
  if (error != HAL_OK)
    return  error;

  /* Figure C.3: ordering procedure for encoding procedure code tables */
  uint32_t k = 0;

  while (k < lastK) {
    uint32_t l = DC_BitsValsTable->HuffVal[k];
    if (l >= JPEG_DC_HUFF_TABLE_SIZE)
      return HAL_ERROR; /* Huffman Table overflow error*/
    else {
      DC_SizeCodesTable->HuffmanCode[l] = huffcode[k];
      DC_SizeCodesTable->CodeLength[l] = huffsize[k] - 1;
      k++;
      }
    }

  return HAL_OK;
  }
//}}}
//{{{
static HAL_StatusTypeDef SetHuffDCMem (JPEG_HandleTypeDef* hjpeg, 
                                       JPEG_DCHuffTableTypeDef *HuffTableDC, 
                                       __IO uint32_t *DCTableAddress) {

  HAL_StatusTypeDef error = HAL_OK;
  JPEG_DC_HuffCodeTableTypeDef dcSizeCodesTable;
  uint32_t i, lsb, msb;
  __IO uint32_t *address, *addressDef;

  if (DCTableAddress == (hjpeg->Instance->HUFFENC_DC0))
    address = (hjpeg->Instance->HUFFENC_DC0 + (JPEG_DC_HUFF_TABLE_SIZE/2));
  else if (DCTableAddress == (hjpeg->Instance->HUFFENC_DC1))
    address = (hjpeg->Instance->HUFFENC_DC1 + (JPEG_DC_HUFF_TABLE_SIZE/2));
  else
    return HAL_ERROR;

  if (HuffTableDC != NULL) {
    error = DCHuffBitsValsToSizeCodes(HuffTableDC, &dcSizeCodesTable);
    if(error != HAL_OK)
      return  error;
    addressDef = address;
    *addressDef = 0x0FFF0FFF;
    addressDef++;
    *addressDef = 0x0FFF0FFF;

    i = JPEG_DC_HUFF_TABLE_SIZE;
    while (i > 0) {
      i--;
      address --;
      msb = ((uint32_t)(((uint32_t)dcSizeCodesTable.CodeLength[i] & 0xF) << 8 )) | ((uint32_t)dcSizeCodesTable.HuffmanCode[i] & 0xFF);
      i--;
      lsb = ((uint32_t)(((uint32_t)dcSizeCodesTable.CodeLength[i] & 0xF) << 8 )) | ((uint32_t)dcSizeCodesTable.HuffmanCode[i] & 0xFF);
      *address = lsb | (msb << 16);
      }
    }

  return HAL_OK;
  }
//}}}
//{{{
static HAL_StatusTypeDef SetHuffACMem (JPEG_HandleTypeDef* hjpeg, 
                                       JPEG_ACHuffTableTypeDef* HuffTableAC,
                                       __IO uint32_t* ACTableAddress) {

  HAL_StatusTypeDef error = HAL_OK;
  JPEG_AC_HuffCodeTableTypeDef acSizeCodesTable;

  __IO uint32_t* address;
  if (ACTableAddress == (hjpeg->Instance->HUFFENC_AC0))
    address = (hjpeg->Instance->HUFFENC_AC0 + (JPEG_AC_HUFF_TABLE_SIZE / 2));
  else if (ACTableAddress == (hjpeg->Instance->HUFFENC_AC1))
    address = (hjpeg->Instance->HUFFENC_AC1 + (JPEG_AC_HUFF_TABLE_SIZE / 2));
  else
    return HAL_ERROR;

  if (HuffTableAC != NULL) {
    error = ACHuffBitsValsToSizeCodes (HuffTableAC, &acSizeCodesTable);
    if (error != HAL_OK)
      return  error;

    // Default values settings: 162:167 FFFh , 168:175 FD0h_FD7h
    // Locations 162:175 of each AC table contain information used internally by the core
    __IO uint32_t* addressDef = address;
    for (uint32_t i = 0; i < 3; i++) {
      *addressDef = 0x0FFF0FFF;
      addressDef++;
      }
    *addressDef = 0x0FD10FD0;
    addressDef++;
    *addressDef = 0x0FD30FD2;
    addressDef++;
    *addressDef = 0x0FD50FD4;
    addressDef++;
    *addressDef = 0x0FD70FD6;
    // end of Locations 162:175

    uint32_t i = JPEG_AC_HUFF_TABLE_SIZE;
    while (i > 0) {
      i--;
      address--;
      uint32_t msb = ((uint32_t)(((uint32_t)acSizeCodesTable.CodeLength[i] & 0xF) << 8 )) | 
                                 ((uint32_t)acSizeCodesTable.HuffmanCode[i] & 0xFF);
      i--;
      uint32_t lsb = ((uint32_t)(((uint32_t)acSizeCodesTable.CodeLength[i] & 0xF) << 8 )) | 
                                 ((uint32_t)acSizeCodesTable.HuffmanCode[i] & 0xFF);
      *address = lsb | (msb << 16);
      }
    }

  return HAL_OK;
  }
//}}}
//{{{
static HAL_StatusTypeDef SetHuffEncMem (JPEG_HandleTypeDef* hjpeg, JPEG_ACHuffTableTypeDef *HuffTableAC0, JPEG_DCHuffTableTypeDef *HuffTableDC0 ,  JPEG_ACHuffTableTypeDef *HuffTableAC1, JPEG_DCHuffTableTypeDef *HuffTableDC1)
{
  HAL_StatusTypeDef error = HAL_OK;

  SetHuffDHTMem(hjpeg, HuffTableAC0, HuffTableDC0, HuffTableAC1, HuffTableDC1);

  if (HuffTableAC0 != NULL) {
    error = SetHuffACMem(hjpeg, HuffTableAC0, (hjpeg->Instance->HUFFENC_AC0));
    if (error != HAL_OK)
      return  error;
    }

  if (HuffTableAC1 != NULL) {
    error = SetHuffACMem(hjpeg, HuffTableAC1, (hjpeg->Instance->HUFFENC_AC1));
    if (error != HAL_OK)
      return  error;
    }

  if (HuffTableDC0 != NULL) {
    error = SetHuffDCMem(hjpeg, HuffTableDC0, hjpeg->Instance->HUFFENC_DC0);
    if (error != HAL_OK)
      return  error;
    }

  if (HuffTableDC1 != NULL) {
    error = SetHuffDCMem(hjpeg, HuffTableDC1, hjpeg->Instance->HUFFENC_DC1);
    if (error != HAL_OK)
      return  error;
    }

  /* Return function status */
  return HAL_OK;
  }
//}}}

//{{{
static void dmaPollResidualData (JPEG_HandleTypeDef* hjpeg) {

  uint32_t count = JPEG_FIFO_SIZE;
  while ((__HAL_JPEG_GET_FLAG(hjpeg, JPEG_FLAG_OFNEF) != 0) &&
         (count > 0) && ((hjpeg->Context &  JPEG_CONTEXT_PAUSE_OUTPUT) == 0)) {
    count--;

    uint32_t dataOut = hjpeg->Instance->DOR;
    hjpeg->pJpegOutBuffPtr[hjpeg->JpegOutCount] = dataOut & 0x000000FF;
    hjpeg->pJpegOutBuffPtr[hjpeg->JpegOutCount + 1] = (dataOut & 0x0000FF00) >> 8;
    hjpeg->pJpegOutBuffPtr[hjpeg->JpegOutCount + 2] = (dataOut & 0x00FF0000) >> 16;
    hjpeg->pJpegOutBuffPtr[hjpeg->JpegOutCount + 3] = (dataOut & 0xFF000000) >> 24;
    hjpeg->JpegOutCount += 4;

    if (hjpeg->JpegOutCount == hjpeg->OutDataLength) {
      // Output Buffer is full
      HAL_JPEG_DataReadyCallback (hjpeg, hjpeg->pJpegOutBuffPtr, hjpeg->JpegOutCount);
      hjpeg->JpegOutCount = 0;
      }
    }

  if ((hjpeg->Context &  JPEG_CONTEXT_PAUSE_OUTPUT) == 0) {
    // Stop Encoding/Decoding
    hjpeg->Instance->CONFR0 &=  ~JPEG_CONFR0_START;

    if (hjpeg->JpegOutCount > 0) {
      // Output Buffer is not empty
      HAL_JPEG_DataReadyCallback (hjpeg, hjpeg->pJpegOutBuffPtr, hjpeg->JpegOutCount);
      hjpeg->JpegOutCount = 0;
      }

    uint32_t tmpContext = hjpeg->Context;
    // Clear all context fileds execpt JPEG_CONTEXT_CONF_ENCODING and JPEG_CONTEXT_CUSTOM_TABLES
    hjpeg->Context &= (JPEG_CONTEXT_CONF_ENCODING | JPEG_CONTEXT_CUSTOM_TABLES);

    // Process Unlocked
    __HAL_UNLOCK(hjpeg);

    hjpeg->State = HAL_JPEG_STATE_READY;

    // Call End of Encoding/Decoding callback
    if ((tmpContext & JPEG_CONTEXT_OPERATION_MASK) == JPEG_CONTEXT_DECODE)
      HAL_JPEG_DecodeCpltCallback (hjpeg);
    }
  }
//}}}
//{{{
static uint32_t dmaEndProcess (JPEG_HandleTypeDef* hjpeg) {

  hjpeg->JpegOutCount = hjpeg->OutDataLength - (hjpeg->hdmaout->Instance->CBNDTR & MDMA_CBNDTR_BNDT);

  if (hjpeg->JpegOutCount == hjpeg->OutDataLength) {
    // Output Buffer is full
    HAL_JPEG_DataReadyCallback (hjpeg, hjpeg->pJpegOutBuffPtr, hjpeg->JpegOutCount);
    hjpeg->JpegOutCount = 0;
    }

  // Check if remaining data in the output FIFO
  if (__HAL_JPEG_GET_FLAG(hjpeg, JPEG_FLAG_OFNEF) == 0) {
    if (hjpeg->JpegOutCount > 0) {
      // Output Buffer is not empty
      HAL_JPEG_DataReadyCallback (hjpeg, hjpeg->pJpegOutBuffPtr, hjpeg->JpegOutCount);
      hjpeg->JpegOutCount = 0;
      }

    // Stop Encoding/Decoding
    hjpeg->Instance->CONFR0 &=  ~JPEG_CONFR0_START;

    uint32_t tmpContext = hjpeg->Context;
    // Clear all context fileds execpt JPEG_CONTEXT_CONF_ENCODING and JPEG_CONTEXT_CUSTOM_TABLES*/
    hjpeg->Context &= (JPEG_CONTEXT_CONF_ENCODING | JPEG_CONTEXT_CUSTOM_TABLES);

    // Process Unlocked
    __HAL_UNLOCK(hjpeg);

    // Change the JPEG state
    hjpeg->State = HAL_JPEG_STATE_READY;

    // Call End of Encoding/Decoding callback
    if ((tmpContext & JPEG_CONTEXT_OPERATION_MASK) == JPEG_CONTEXT_DECODE)
      HAL_JPEG_DecodeCpltCallback (hjpeg);
    }

  else if ((hjpeg->Context & JPEG_CONTEXT_PAUSE_OUTPUT) == 0) {
    dmaPollResidualData (hjpeg);
    return JPEG_PROCESS_DONE;
    }

  return JPEG_PROCESS_ONGOING;
  }
//}}}

//{{{
static void MDMAInCpltCallback (MDMA_HandleTypeDef* hmdma) {

  JPEG_HandleTypeDef* hjpeg = (JPEG_HandleTypeDef*)((MDMA_HandleTypeDef*)hmdma)->Parent;

  /* Disable The JPEG IT so the DMA Input Callback can not be interrupted by the JPEG EOC IT or JPEG HPD IT */
  __HAL_JPEG_DISABLE_IT(hjpeg,JPEG_INTERRUPT_MASK);

  if (((hjpeg->Context & JPEG_CONTEXT_METHOD_MASK) == JPEG_CONTEXT_DMA) &&
      ((hjpeg->Context & JPEG_CONTEXT_ENDING_DMA) == 0)) {
    // if the MDMA In is triggred with JPEG In FIFO Threshold flag then MDMA In buffer size is 32 bytes
    //  else (MDMA In is triggred with JPEG In FIFO not full flag) then MDMA In buffer size is 4 bytes
    uint32_t inXfrSize = hjpeg->hdmain->Init.BufferTransferLength;
    hjpeg->JpegInCount = hjpeg->InDataLength - (hmdma->Instance->CBNDTR & MDMA_CBNDTR_BNDT);
    HAL_JPEG_GetDataCallback (hjpeg, hjpeg->JpegInCount);
    if (hjpeg->InDataLength >= inXfrSize) {
      /*JPEG Input DMA transfer data number must be multiple of MDMA buffer size
        as the destination is a 32 bits register */
      hjpeg->InDataLength = hjpeg->InDataLength - (hjpeg->InDataLength % inXfrSize);

      }
    else if(hjpeg->InDataLength > 0) {
      /* Transfer the remaining Data, must be multiple of source data size (byte) and destination data size (word) */
      if((hjpeg->InDataLength % 4) != 0)
        hjpeg->InDataLength = ((hjpeg->InDataLength / 4) + 1) * 4;
      }

    if (((hjpeg->Context &  JPEG_CONTEXT_PAUSE_INPUT) == 0) && (hjpeg->InDataLength > 0))
      /* Start MDMA FIFO In transfer */
      HAL_MDMA_Start_IT (hjpeg->hdmain, (uint32_t)hjpeg->pJpegInBuffPtr, (uint32_t)&hjpeg->Instance->DIR, hjpeg->InDataLength, 1);

    /* JPEG Conversion still on going : Enable the JPEG IT */
    __HAL_JPEG_ENABLE_IT (hjpeg,JPEG_IT_EOC |JPEG_IT_HPD);
    }
  }
//}}}
//{{{
static void MDMAOutCpltCallback (MDMA_HandleTypeDef* hmdma) {

  JPEG_HandleTypeDef* hjpeg = (JPEG_HandleTypeDef*)((MDMA_HandleTypeDef*)hmdma)->Parent;

  // Disable The JPEG IT so the DMA Output Callback not interrupted by the JPEG EOC IT or JPEG HPD IT
  __HAL_JPEG_DISABLE_IT(hjpeg,JPEG_INTERRUPT_MASK);

  if (((hjpeg->Context & JPEG_CONTEXT_METHOD_MASK) == JPEG_CONTEXT_DMA) &&
      ((hjpeg->Context & JPEG_CONTEXT_ENDING_DMA) == 0)) {
    if (__HAL_JPEG_GET_FLAG(hjpeg, JPEG_FLAG_EOCF) == 0) {
      hjpeg->JpegOutCount = hjpeg->OutDataLength - (hmdma->Instance->CBNDTR & MDMA_CBNDTR_BNDT);

      // Output Buffer is full
      HAL_JPEG_DataReadyCallback (hjpeg, hjpeg->pJpegOutBuffPtr, hjpeg->JpegOutCount);

      if ((hjpeg->Context &  JPEG_CONTEXT_PAUSE_OUTPUT) == 0)
        // Start MDMA FIFO Out transfer
        HAL_MDMA_Start_IT (hjpeg->hdmaout, (uint32_t)&hjpeg->Instance->DOR, (uint32_t)hjpeg->pJpegOutBuffPtr, hjpeg->OutDataLength, 1);
      }

    // JPEG Conversion still on going : Enable the JPEG IT
    __HAL_JPEG_ENABLE_IT (hjpeg,JPEG_IT_EOC |JPEG_IT_HPD);
    }
  }
//}}}
//{{{
static void MDMAErrorCallback (MDMA_HandleTypeDef* hmdma)
{
  JPEG_HandleTypeDef* hjpeg = (JPEG_HandleTypeDef*)((MDMA_HandleTypeDef*)hmdma)->Parent;

  /*Stop Encoding/Decoding*/
  hjpeg->Instance->CONFR0 &= ~JPEG_CONFR0_START;

  /* Disable All Interrupts */
  __HAL_JPEG_DISABLE_IT (hjpeg,JPEG_INTERRUPT_MASK);

  hjpeg->State= HAL_JPEG_STATE_READY;
  hjpeg->ErrorCode |= HAL_JPEG_ERROR_DMA;
  HAL_JPEG_ErrorCallback(hjpeg);
}

//}}}
//{{{
static void MDMAOutAbortCallback (MDMA_HandleTypeDef* hmdma) {

  JPEG_HandleTypeDef* hjpeg = (JPEG_HandleTypeDef*)((MDMA_HandleTypeDef*)hmdma)->Parent;

  if ((hjpeg->Context & JPEG_CONTEXT_ENDING_DMA) != 0)
    dmaEndProcess (hjpeg);
  }
//}}}
//
//{{{
HAL_StatusTypeDef HAL_JPEG_Init (JPEG_HandleTypeDef* hjpeg) {

  uint32_t acLum_huffmanTableAddr = (uint32_t)(&ACLUM_HuffTable);
  uint32_t dcLum_huffmanTableAddr = (uint32_t)(&DCLUM_HuffTable);
  uint32_t acChrom_huffmanTableAddr = (uint32_t)(&ACCHROM_HuffTable);
  uint32_t dcChrom_huffmanTableAddr = (uint32_t)(&DCCHROM_HuffTable);

  if (hjpeg->State == HAL_JPEG_STATE_RESET) {
    hjpeg->Lock = HAL_UNLOCKED;
    HAL_JPEG_MspInit (hjpeg);
    }

  hjpeg->State = HAL_JPEG_STATE_BUSY;
  __HAL_JPEG_ENABLE (hjpeg);

  // Stop the JPEG encoding/decoding process*/
  hjpeg->Instance->CONFR0 &= ~JPEG_CONFR0_START;

  __HAL_JPEG_DISABLE_IT (hjpeg, JPEG_INTERRUPT_MASK);

  // Flush input and output FIFOs*/
  hjpeg->Instance->CR |= JPEG_CR_IFF;
  hjpeg->Instance->CR |= JPEG_CR_OFF;
  __HAL_JPEG_CLEAR_FLAG (hjpeg,JPEG_FLAG_ALL);

  /* init default quantization tables*/
  hjpeg->QuantTable0 = (uint8_t*)((uint32_t)LUM_QuantTable);
  hjpeg->QuantTable1 = (uint8_t*)((uint32_t)CHROM_QuantTable);
  hjpeg->QuantTable2 = NULL;
  hjpeg->QuantTable3 = NULL;

  if (SetHuffEncMem (hjpeg,
                     (JPEG_ACHuffTableTypeDef*)acLum_huffmanTableAddr,
                     (JPEG_DCHuffTableTypeDef*)dcLum_huffmanTableAddr,
                     (JPEG_ACHuffTableTypeDef*)acChrom_huffmanTableAddr,
                     (JPEG_DCHuffTableTypeDef*)dcChrom_huffmanTableAddr) != HAL_OK) {
    hjpeg->ErrorCode = HAL_JPEG_ERROR_HUFF_TABLE;
    return HAL_ERROR;
    }

  // Enable header processing
  hjpeg->Instance->CONFR1 |= JPEG_CONFR1_HDR;

  hjpeg->JpegInCount = 0;
  hjpeg->JpegOutCount = 0;

  hjpeg->State = HAL_JPEG_STATE_READY;
  hjpeg->ErrorCode = HAL_JPEG_ERROR_NONE;

  // Clear the context fields
  hjpeg->Context = 0;

  return HAL_OK;
  }
//}}}
//{{{
HAL_StatusTypeDef HAL_JPEG_Decode_DMA (JPEG_HandleTypeDef* hjpeg,
                                       uint8_t* pDataIn, uint32_t InDataLength,
                                       uint8_t* pDataOutMCU ,uint32_t OutDataLength) {

  __HAL_LOCK (hjpeg);

  if (hjpeg->State == HAL_JPEG_STATE_READY) {
    hjpeg->State = HAL_JPEG_STATE_BUSY_DECODING;

    // Set the Context to Decode with DMA
    hjpeg->Context &= ~(JPEG_CONTEXT_OPERATION_MASK | JPEG_CONTEXT_METHOD_MASK);
    hjpeg->Context |= JPEG_CONTEXT_DECODE | JPEG_CONTEXT_DMA;

    hjpeg->pJpegInBuffPtr = pDataIn;
    hjpeg->InDataLength = InDataLength;

    hjpeg->pJpegOutBuffPtr = pDataOutMCU;
    hjpeg->OutDataLength = OutDataLength;

    // Reset In/out data counter
    hjpeg->JpegInCount = 0;
    hjpeg->JpegOutCount = 0;

    // Reset pause
    hjpeg->Context &= (~(JPEG_CONTEXT_PAUSE_INPUT | JPEG_CONTEXT_PAUSE_OUTPUT));

    if ((hjpeg->Context & JPEG_CONTEXT_OPERATION_MASK) == JPEG_CONTEXT_DECODE)
      /*Set JPEG Codec to Decoding mode */
      hjpeg->Instance->CONFR1 |= JPEG_CONFR1_DE;
    else if ((hjpeg->Context & JPEG_CONTEXT_OPERATION_MASK) == JPEG_CONTEXT_ENCODE)
      /*Set JPEG Codec to Encoding mode */
      hjpeg->Instance->CONFR1 &= ~JPEG_CONFR1_DE;

    // Stop JPEG processing */
    hjpeg->Instance->CONFR0 &=  ~JPEG_CONFR0_START;

    __HAL_JPEG_DISABLE_IT (hjpeg,JPEG_INTERRUPT_MASK);

    // Flush input and output FIFOs*/
    hjpeg->Instance->CR |= JPEG_CR_IFF;
    hjpeg->Instance->CR |= JPEG_CR_OFF;
    __HAL_JPEG_CLEAR_FLAG (hjpeg,JPEG_FLAG_ALL);

    // Start Encoding/Decoding*/
    hjpeg->Instance->CONFR0 |=  JPEG_CONFR0_START;

    if ((hjpeg->Context & JPEG_CONTEXT_METHOD_MASK) == JPEG_CONTEXT_IT)
      /*Enable IN/OUT, end of Conversation, and end of header parsing interruptions*/
      __HAL_JPEG_ENABLE_IT (hjpeg, JPEG_IT_IFT | JPEG_IT_IFNF | JPEG_IT_OFT | JPEG_IT_OFNE | JPEG_IT_EOC |JPEG_IT_HPD);
    else if ((hjpeg->Context & JPEG_CONTEXT_METHOD_MASK) == JPEG_CONTEXT_DMA)
      /*Enable End Of Conversation, and End Of Header parsing interruptions*/
      __HAL_JPEG_ENABLE_IT (hjpeg, JPEG_IT_EOC |JPEG_IT_HPD);

    uint32_t inXfrSize, outXfrSize;

    // if the MDMA In is triggred with JPEG In FIFO Threshold flag then MDMA In buffer size is 32 bytes
    // else (MDMA In is triggred with JPEG In FIFO not full flag then MDMA In buffer size is 4 bytes
    inXfrSize = hjpeg->hdmain->Init.BufferTransferLength;

    // if the MDMA Out is triggred with JPEG Out FIFO Threshold flag then MDMA out buffer size is 32 bytes
    // else (MDMA Out is triggred with JPEG Out FIFO not empty flag then MDMA buffer size is 4 bytes
    outXfrSize = hjpeg->hdmaout->Init.BufferTransferLength;

    if ((hjpeg->InDataLength < inXfrSize) || (hjpeg->OutDataLength < outXfrSize))
      return HAL_ERROR;

    // Set the JPEG MDMA In transfer complete callback */
    hjpeg->hdmain->XferCpltCallback = MDMAInCpltCallback;

    // Set the MDMA In error callback */
    hjpeg->hdmain->XferErrorCallback = MDMAErrorCallback;

    // Set the JPEG MDMA Out transfer complete callback */
    hjpeg->hdmaout->XferCpltCallback = MDMAOutCpltCallback;

    // Set the MDMA In error callback */
    hjpeg->hdmaout->XferErrorCallback = MDMAErrorCallback;

    // Set the MDMA Out Abort callback */
    hjpeg->hdmaout->XferAbortCallback = MDMAOutAbortCallback;

    // MDMA transfer size (BNDTR) must be a multiple of MDMA buffer size (TLEN)*/
    hjpeg->InDataLength = hjpeg->InDataLength - (hjpeg->InDataLength % inXfrSize);

    // MDMA transfer size (BNDTR) must be a multiple of MDMA buffer size (TLEN)*/
    hjpeg->OutDataLength = hjpeg->OutDataLength - (hjpeg->OutDataLength % outXfrSize);

    // Start MDMA FIFO Out transfer */
    HAL_MDMA_Start_IT (hjpeg->hdmaout, (uint32_t)&hjpeg->Instance->DOR,
                       (uint32_t)hjpeg->pJpegOutBuffPtr, hjpeg->OutDataLength, 1);

    // Start DMA FIFO In transfer */
    HAL_MDMA_Start_IT (hjpeg->hdmain, (uint32_t)hjpeg->pJpegInBuffPtr,
                      (uint32_t)&hjpeg->Instance->DIR, hjpeg->InDataLength, 1);
    }

  else {
    __HAL_UNLOCK(hjpeg);
    return HAL_BUSY;
    }

  return HAL_OK;
  }
//}}}
//{{{
HAL_StatusTypeDef HAL_JPEG_Pause (JPEG_HandleTypeDef* hjpeg, uint32_t XferSelection) {

  uint32_t mask = 0;

  assert_param (IS_JPEG_PAUSE_RESUME_STATE(XferSelection));

  if ((hjpeg->Context & JPEG_CONTEXT_METHOD_MASK) == JPEG_CONTEXT_DMA) {
    if ((XferSelection & JPEG_PAUSE_RESUME_INPUT) == JPEG_PAUSE_RESUME_INPUT)
      hjpeg->Context |= JPEG_CONTEXT_PAUSE_INPUT;
    if ((XferSelection & JPEG_PAUSE_RESUME_OUTPUT) == JPEG_PAUSE_RESUME_OUTPUT)
      hjpeg->Context |= JPEG_CONTEXT_PAUSE_OUTPUT;
    }

  else if ((hjpeg->Context & JPEG_CONTEXT_METHOD_MASK) == JPEG_CONTEXT_IT) {
    if ((XferSelection & JPEG_PAUSE_RESUME_INPUT) == JPEG_PAUSE_RESUME_INPUT) {
      hjpeg->Context |= JPEG_CONTEXT_PAUSE_INPUT;
      mask |= (JPEG_IT_IFT | JPEG_IT_IFNF);
      }
    if ((XferSelection & JPEG_PAUSE_RESUME_OUTPUT) == JPEG_PAUSE_RESUME_OUTPUT) {
      hjpeg->Context |= JPEG_CONTEXT_PAUSE_OUTPUT;
      mask |=  (JPEG_IT_OFT | JPEG_IT_OFNE | JPEG_IT_EOC);
      }
    __HAL_JPEG_DISABLE_IT(hjpeg,mask);
    }

  /* Return function status */
  return HAL_OK;
  }
//}}}
//{{{
HAL_StatusTypeDef HAL_JPEG_Resume (JPEG_HandleTypeDef* hjpeg, uint32_t XferSelection) {

  uint32_t mask = 0;
  uint32_t xfrSize = 0;

  assert_param(IS_JPEG_PAUSE_RESUME_STATE(XferSelection));

  if ((hjpeg->Context & JPEG_CONTEXT_METHOD_MASK) == JPEG_CONTEXT_DMA) {
    if ((XferSelection & JPEG_PAUSE_RESUME_INPUT) == JPEG_PAUSE_RESUME_INPUT) {
      hjpeg->Context &= (~JPEG_CONTEXT_PAUSE_INPUT);

      // if the MDMA In is triggred with JPEG In FIFO Threshold flag then MDMA In buffer size is 32 bytes
      // else (MDMA In is triggred with JPEG In FIFO not full flag) then MDMA In buffer size is 4 bytes
      xfrSize = hjpeg->hdmain->Init.BufferTransferLength;

      // MDMA transfer size (BNDTR) must be a multiple of MDMA buffer size (TLEN)*/
      hjpeg->InDataLength = hjpeg->InDataLength - (hjpeg->InDataLength % xfrSize);

      if(hjpeg->InDataLength > 0)
        /* Start DMA FIFO In transfer */
        HAL_MDMA_Start_IT(hjpeg->hdmain, (uint32_t)hjpeg->pJpegInBuffPtr, (uint32_t)&hjpeg->Instance->DIR, hjpeg->InDataLength, 1);
      }

    if ((XferSelection & JPEG_PAUSE_RESUME_OUTPUT) == JPEG_PAUSE_RESUME_OUTPUT) {
      hjpeg->Context &= (~JPEG_CONTEXT_PAUSE_OUTPUT);

      if ((hjpeg->Context & JPEG_CONTEXT_ENDING_DMA) != 0)
        dmaPollResidualData (hjpeg);
      else {
        // if the MDMA Out is triggred with JPEG Out FIFO Threshold flag then MDMA out buffer size is 32 bytes
        // else (MDMA Out is triggred with JPEG Out FIFO not empty flag) then MDMA buffer size is 4 bytes
        xfrSize = hjpeg->hdmaout->Init.BufferTransferLength;

        // MDMA transfer size (BNDTR) must be a multiple of MDMA buffer size (TLEN)*/
        hjpeg->OutDataLength = hjpeg->OutDataLength - (hjpeg->OutDataLength % xfrSize);

        // Start DMA FIFO Out transfer
        HAL_MDMA_Start_IT (hjpeg->hdmaout, (uint32_t)&hjpeg->Instance->DOR, (uint32_t)hjpeg->pJpegOutBuffPtr, hjpeg->OutDataLength, 1);
        }
      }
    }

  else if ((hjpeg->Context & JPEG_CONTEXT_METHOD_MASK) == JPEG_CONTEXT_IT) {
    if ((XferSelection & JPEG_PAUSE_RESUME_INPUT) == JPEG_PAUSE_RESUME_INPUT) {
      hjpeg->Context &= (~JPEG_CONTEXT_PAUSE_INPUT);
      mask |= (JPEG_IT_IFT | JPEG_IT_IFNF);
      }
    if ((XferSelection & JPEG_PAUSE_RESUME_OUTPUT) == JPEG_PAUSE_RESUME_OUTPUT) {
      hjpeg->Context &= (~JPEG_CONTEXT_PAUSE_OUTPUT);
      mask |=  (JPEG_IT_OFT | JPEG_IT_OFNE | JPEG_IT_EOC);
      }
    __HAL_JPEG_ENABLE_IT (hjpeg,mask);
    }

  return HAL_OK;
  }
//}}}

//{{{
void HAL_JPEG_ConfigInputBuffer (JPEG_HandleTypeDef* hjpeg, uint8_t *pNewInputBuffer, uint32_t InDataLength) {

  hjpeg->pJpegInBuffPtr =  pNewInputBuffer;
  hjpeg->InDataLength = InDataLength;
  }
//}}}
//{{{
void HAL_JPEG_ConfigOutputBuffer (JPEG_HandleTypeDef* hjpeg, uint8_t *pNewOutputBuffer, uint32_t OutDataLength) {

  hjpeg->pJpegOutBuffPtr = pNewOutputBuffer;
  hjpeg->OutDataLength = OutDataLength;
  }
//}}}

//{{{
HAL_StatusTypeDef HAL_JPEG_GetInfo (JPEG_HandleTypeDef* hjpeg, JPEG_ConfTypeDef *pInfo) {

  pInfo->ImageHeight = (hjpeg->Instance->CONFR1 & 0xFFFF0000U) >> 16;
  pInfo->ImageWidth  = (hjpeg->Instance->CONFR3 & 0xFFFF0000U) >> 16;

  // Read the conf parameters
  if ((hjpeg->Instance->CONFR1 & JPEG_CONFR1_NF) == JPEG_CONFR1_NF_1)
    pInfo->ColorSpace = JPEG_YCBCR_COLORSPACE;
  else if ((hjpeg->Instance->CONFR1 & JPEG_CONFR1_NF) == 0)
    pInfo->ColorSpace = JPEG_GRAYSCALE_COLORSPACE;
  else if ((hjpeg->Instance->CONFR1 & JPEG_CONFR1_NF) == JPEG_CONFR1_NF)
    pInfo->ColorSpace = JPEG_CMYK_COLORSPACE;

  if ((pInfo->ColorSpace == JPEG_YCBCR_COLORSPACE) ||
      (pInfo->ColorSpace == JPEG_CMYK_COLORSPACE)) {
    uint32_t yblockNb  = (hjpeg->Instance->CONFR4 & JPEG_CONFR4_NB) >> 4;
    uint32_t cBblockNb = (hjpeg->Instance->CONFR5 & JPEG_CONFR5_NB) >> 4;
    uint32_t cRblockNb = (hjpeg->Instance->CONFR6 & JPEG_CONFR6_NB) >> 4;

    if ((yblockNb == 1) && (cBblockNb == 0) && (cRblockNb == 0))
      pInfo->ChromaSubsampling = JPEG_422_SUBSAMPLING; /*16x8 block*/
    else if ((yblockNb == 0) && (cBblockNb == 0) && (cRblockNb == 0))
      pInfo->ChromaSubsampling = JPEG_444_SUBSAMPLING;
    else if ((yblockNb == 3) && (cBblockNb == 0) && (cRblockNb == 0))
      pInfo->ChromaSubsampling = JPEG_420_SUBSAMPLING;
    else /*Default is 4:4:4*/
      pInfo->ChromaSubsampling = JPEG_444_SUBSAMPLING;
    }
  else
    pInfo->ChromaSubsampling = JPEG_444_SUBSAMPLING;

  pInfo->ImageQuality = 0;

  return HAL_OK;
  }
//}}}

//{{{
void HAL_JPEG_IRQHandler (JPEG_HandleTypeDef* hjpeg) {

  if (hjpeg->State == HAL_JPEG_STATE_BUSY_DECODING) {
    // End of header processing flag rises
    if (((hjpeg->Context & JPEG_CONTEXT_OPERATION_MASK) == JPEG_CONTEXT_DECODE) &&
        (__HAL_JPEG_GET_FLAG(hjpeg, JPEG_FLAG_HPDF) != RESET)) {

      HAL_JPEG_GetInfo (hjpeg, &hjpeg->Conf);

      // Reset the ImageQuality, is only available at the end of the decoding operation
      hjpeg->Conf.ImageQuality = 0;
      HAL_JPEG_InfoReadyCallback (hjpeg, &hjpeg->Conf);

      __HAL_JPEG_DISABLE_IT (hjpeg, JPEG_IT_HPD);

      // Clear header processing done flag
      __HAL_JPEG_CLEAR_FLAG (hjpeg, JPEG_FLAG_HPDF);
      }

    // End of Conversion handling
    if (__HAL_JPEG_GET_FLAG (hjpeg, JPEG_FLAG_EOCF) != RESET) {
      hjpeg->Context |= JPEG_CONTEXT_ENDING_DMA;

      // Stop Encoding/Decoding
      hjpeg->Instance->CONFR0 &= ~JPEG_CONFR0_START;

      __HAL_JPEG_DISABLE_IT (hjpeg, JPEG_INTERRUPT_MASK);

      // Clear all flags
      __HAL_JPEG_CLEAR_FLAG (hjpeg, JPEG_FLAG_ALL);

      if (hjpeg->hdmain->State == HAL_MDMA_STATE_BUSY)
        // Stop the MDMA In Xfer
        HAL_MDMA_Abort_IT (hjpeg->hdmain);

      if (hjpeg->hdmaout->State == HAL_MDMA_STATE_BUSY)
        // Stop the MDMA out Xfer
        HAL_MDMA_Abort_IT (hjpeg->hdmaout);
      else
        dmaEndProcess (hjpeg);
      }
    }
  }
//}}}
