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

tBufs mInBuf[2] = { { false, nullptr, 0 }, { false, nullptr, 0 } };
uint8_t* mOutYuvBuf = nullptr;
uint32_t mOutYuvLen = 0;
uint32_t mOutYuvStripLen = 0;
uint32_t mOutLen = 0;

//{{{
void outputData (uint8_t* data, uint32_t len) {

  //printf ("outputData %x %d\n", data, len);
  //lcd->info (COL_GREEN, "outputData " + hex(uint32_t(data)) + ":" + hex(len));
  //lcd->changed();
  mHandle.OutBuffPtr = data + len;
  mHandle.OutLen = mOutYuvStripLen;
  mOutLen += len;
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
    outputData (mHandle.OutBuffPtr, mHandle.OutCount);
    mHandle.OutCount = 0;
    }

  // Check if remaining data in the output FIFO
  if (__HAL_JPEG_GET_FLAG (&mHandle, JPEG_FLAG_OFNEF) == 0) {
    if (mHandle.OutCount > 0) {
      // Output Buffer not empty
      outputData (mHandle.OutBuffPtr, mHandle.OutCount);
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
        outputData (mHandle.OutBuffPtr, mHandle.OutCount);
        mHandle.OutCount = 0;
        }
      }

    // stop decoding
    JPEG->CONFR0 &=  ~JPEG_CONFR0_START;
    if (mHandle.OutCount > 0) {
      // Output Buffer not empty
      outputData (mHandle.OutBuffPtr, mHandle.OutCount);
      mHandle.OutCount = 0;
      }
    }

  mHandle.mDecodeDone = true;
  }
//}}}

// inits
//{{{
//void BitsToSizeCodes (uint8_t* Bits, uint8_t* Huffsize, uint32_t* Huffcode, uint32_t* LastK) {

  //uint32_t i, p, l, code, si;

  //// Figure C.1: Generation of table of Huffman code sizes */
  //p = 0;
  //for (l = 0; l < 16; l++) {
    //i = (uint32_t)Bits[l];
    //if ( (p + i) > 256)
      //return;
    //while (i != 0) {
      //Huffsize[p++] = (uint8_t) l+1;
      //i--;
      //}
    //}
  //Huffsize[p] = 0;
  //*LastK = p;

  //// Figure C.2: Generation of table of Huffman codes */
  //code = 0;
  //si = Huffsize[0];
  //p = 0;
  //while (Huffsize[p] != 0) {
    //while (((uint32_t) Huffsize[p]) == si) {
      //Huffcode[p++] = code;
      //code++;
      //}
    //// code must fit in "size" bits (si), no code is allowed to be all ones*/
    //if (((uint32_t) code) >= (((uint32_t) 1) << si))
      //return;
    //code <<= 1;
    //si++;
    //}
  //}
//}}}
//{{{
//void DCHuffBitsValsToSizeCodes (JPEG_DCHuffTableTypeDef* DC_BitsValsTable,
                                //JPEG_DC_HuffCodeTableTypeDef* DC_SizeCodesTable) {

  //uint32_t lastK;
  //uint8_t huffsize[257];
  //uint32_t huffcode[257];
  //BitsToSizeCodes (DC_BitsValsTable->Bits, huffsize, huffcode, &lastK);

  //// Figure C.3: ordering procedure for encoding procedure code tables */
  //uint32_t k = 0;

  //while (k < lastK) {
    //uint32_t l = DC_BitsValsTable->HuffVal[k];
    //if (l >= JPEG_DC_HUFF_TABLE_SIZE)
      //return;
    //else {
      //DC_SizeCodesTable->HuffmanCode[l] = huffcode[k];
      //DC_SizeCodesTable->CodeLength[l] = huffsize[k] - 1;
      //k++;
      //}
    //}
  //}
//}}}
//{{{
//void SetHuffDCMem (JPEG_DCHuffTableTypeDef* HuffTableDC, __IO uint32_t *DCTableAddress) {

  //JPEG_DC_HuffCodeTableTypeDef dcSizeCodesTable;
  //uint32_t i, lsb, msb;
  //__IO uint32_t *address, *addressDef;

  //if (DCTableAddress == (JPEG->HUFFENC_DC0))
    //address = (JPEG->HUFFENC_DC0 + (JPEG_DC_HUFF_TABLE_SIZE/2));
  //else if (DCTableAddress == (JPEG->HUFFENC_DC1))
    //address = (JPEG->HUFFENC_DC1 + (JPEG_DC_HUFF_TABLE_SIZE/2));
  //else
    //return;

  //if (HuffTableDC != NULL) {
    //DCHuffBitsValsToSizeCodes (HuffTableDC, &dcSizeCodesTable);
    //addressDef = address;
    //*addressDef = 0x0FFF0FFF;
    //addressDef++;
    //*addressDef = 0x0FFF0FFF;

    //i = JPEG_DC_HUFF_TABLE_SIZE;
    //while (i > 0) {
      //i--;
      //address --;
      //msb = ((uint32_t)(((uint32_t)dcSizeCodesTable.CodeLength[i] & 0xF) << 8 )) |
                        //((uint32_t)dcSizeCodesTable.HuffmanCode[i] & 0xFF);
      //i--;
      //lsb = ((uint32_t)(((uint32_t)dcSizeCodesTable.CodeLength[i] & 0xF) << 8 )) |
                        //((uint32_t)dcSizeCodesTable.HuffmanCode[i] & 0xFF);
      //*address = lsb | (msb << 16);
      //}
    //}
  //}
//}}}
//{{{
//void ACHuffBitsValsToSizeCodes (JPEG_ACHuffTableTypeDef* AC_BitsValsTable,
                                //JPEG_AC_HuffCodeTableTypeDef* AC_SizeCodesTable) {

  //uint8_t huffsize[257];
  //uint32_t huffcode[257];
  //uint32_t lastK;
  //BitsToSizeCodes (AC_BitsValsTable->Bits, huffsize, huffcode, &lastK);

  //// Figure C.3: Ordering procedure for encoding procedure code tables */
  //uint32_t k = 0;
  //while (k < lastK) {
    //uint32_t l = AC_BitsValsTable->HuffVal[k];
    //if (l == 0)
      //l = 160; //l = 0x00 EOB code*/
    //else if(l == 0xF0) // l = 0xF0 ZRL code*/
      //l = 161;
    //else {
      //uint32_t msb = (l & 0xF0) >> 4;
      //uint32_t lsb = (l & 0x0F);
      //l = (msb * 10) + lsb - 1;
      //}
    //if (l >= JPEG_AC_HUFF_TABLE_SIZE)
      //return; // Huffman Table overflow error*/
    //else {
      //AC_SizeCodesTable->HuffmanCode[l] = huffcode[k];
      //AC_SizeCodesTable->CodeLength[l] = huffsize[k] - 1;
      //k++;
      //}
    //}
  //}
//}}}
//{{{
//void SetHuffACMem (JPEG_ACHuffTableTypeDef* HuffTableAC, __IO uint32_t* ACTableAddress) {

  //JPEG_AC_HuffCodeTableTypeDef acSizeCodesTable;

  //__IO uint32_t* address;
  //if (ACTableAddress == (JPEG->HUFFENC_AC0))
    //address = (JPEG->HUFFENC_AC0 + (JPEG_AC_HUFF_TABLE_SIZE / 2));
  //else if (ACTableAddress == (mHandle.Instance->HUFFENC_AC1))
    //address = (JPEG->HUFFENC_AC1 + (JPEG_AC_HUFF_TABLE_SIZE / 2));
  //else
    //return;

  //if (HuffTableAC != NULL) {
     //ACHuffBitsValsToSizeCodes (HuffTableAC, &acSizeCodesTable);

    //// Default values settings: 162:167 FFFh , 168:175 FD0h_FD7h
    //// Locations 162:175 of each AC table contain information used internally by the core
    //__IO uint32_t* addressDef = address;
    //for (uint32_t i = 0; i < 3; i++) {
      //*addressDef = 0x0FFF0FFF;
      //addressDef++;
      //}
    //*addressDef = 0x0FD10FD0;
    //addressDef++;
    //*addressDef = 0x0FD30FD2;
    //addressDef++;
    //*addressDef = 0x0FD50FD4;
    //addressDef++;
    //*addressDef = 0x0FD70FD6;
    //// end of Locations 162:175

    //uint32_t i = JPEG_AC_HUFF_TABLE_SIZE;
    //while (i > 0) {
      //i--;
      //address--;
      //uint32_t msb = ((uint32_t)(((uint32_t)acSizeCodesTable.CodeLength[i] & 0xF) << 8 )) |
                                 //((uint32_t)acSizeCodesTable.HuffmanCode[i] & 0xFF);
      //i--;
      //uint32_t lsb = ((uint32_t)(((uint32_t)acSizeCodesTable.CodeLength[i] & 0xF) << 8 )) |
                                 //((uint32_t)acSizeCodesTable.HuffmanCode[i] & 0xFF);
      //*address = lsb | (msb << 16);
      //}
    //}
  //}
//}}}
//{{{
//void SetHuffDHTMem (JPEG_ACHuffTableTypeDef* HuffTableAC0,
                    //JPEG_DCHuffTableTypeDef* HuffTableDC0,
                    //JPEG_ACHuffTableTypeDef* HuffTableAC1,
                    //JPEG_DCHuffTableTypeDef* HuffTableDC1) {

  //uint32_t value, index;
  //__IO uint32_t* address;

  //if (HuffTableDC0 != NULL) {
    //// DC0 Huffman Table : BITS*/
    //// DC0 BITS is a 16 Bytes table i.e 4x32bits words from DHTMEM base address to DHTMEM + 3*/
    //address = (JPEG->DHTMEM + 3);
    //index = 16;
    //while(index > 0) {
      //*address = (((uint32_t)HuffTableDC0->Bits[index-1] & 0xFF) << 24)|
                 //(((uint32_t)HuffTableDC0->Bits[index-2] & 0xFF) << 16)|
                 //(((uint32_t)HuffTableDC0->Bits[index-3] & 0xFF) << 8) |
                 //((uint32_t)HuffTableDC0->Bits[index-4] & 0xFF);
      //address--;
      //index -=4;
      //}

    //// DC0 Huffman Table : Val*/
    //// DC0 VALS is a 12 Bytes table i.e 3x32bits words from DHTMEM base address +4 to DHTMEM + 6 */
    //address = (JPEG->DHTMEM + 6);
    //index = 12;
    //while(index > 0) {
      //*address = (((uint32_t)HuffTableDC0->HuffVal[index-1] & 0xFF) << 24)|
                 //(((uint32_t)HuffTableDC0->HuffVal[index-2] & 0xFF) << 16)|
                 //(((uint32_t)HuffTableDC0->HuffVal[index-3] & 0xFF) << 8) |
                 //((uint32_t)HuffTableDC0->HuffVal[index-4] & 0xFF);
      //address--;
      //index -=4;
      //}
    //}

  //if (HuffTableAC0 != NULL) {
    //// AC0 Huffman Table : BITS*/
    //// AC0 BITS is a 16 Bytes table i.e 4x32bits words from DHTMEM base address + 7 to DHTMEM + 10*/
    //address = (JPEG->DHTMEM + 10);
    //index = 16;
    //while(index > 0) {
      //*address = (((uint32_t)HuffTableAC0->Bits[index-1] & 0xFF) << 24)|
                 //(((uint32_t)HuffTableAC0->Bits[index-2] & 0xFF) << 16)|
                 //(((uint32_t)HuffTableAC0->Bits[index-3] & 0xFF) << 8) |
                 //((uint32_t)HuffTableAC0->Bits[index-4] & 0xFF);
      //address--;
      //index -=4;
      //}
    //// AC0 Huffman Table : Val*/
    //// AC0 VALS is a 162 Bytes table i.e 41x32bits words from DHTMEM base address + 11 to DHTMEM + 51 */
    //// only Byte 0 and Byte 1 of the last word (@ DHTMEM + 51) belong to AC0 VALS table */
    //address = (JPEG->DHTMEM + 51);
    //value = *address & 0xFFFF0000U;
    //value = value | (((uint32_t)HuffTableAC0->HuffVal[161] & 0xFF) << 8) | ((uint32_t)HuffTableAC0->HuffVal[160] & 0xFF);
    //*address = value;

    //// continue setting 160 AC0 huffman values */
    //address--; // address = JPEG->DHTMEM + 50*/
    //index = 160;
    //while(index > 0) {
      //*address = (((uint32_t)HuffTableAC0->HuffVal[index-1] & 0xFF) << 24)|
                 //(((uint32_t)HuffTableAC0->HuffVal[index-2] & 0xFF) << 16)|
                 //(((uint32_t)HuffTableAC0->HuffVal[index-3] & 0xFF) << 8) |
                 //((uint32_t)HuffTableAC0->HuffVal[index-4] & 0xFF);
      //address--;
      //index -=4;
      //}
    //}

  //if (HuffTableDC1 != NULL) {
    //// DC1 Huffman Table : BITS*/
    //// DC1 BITS is a 16 Bytes table i.e 4x32bits words from DHTMEM + 51 base address to DHTMEM + 55*/
    //// only Byte 2 and Byte 3 of the first word (@ DHTMEM + 51) belong to DC1 Bits table */
    //address = (JPEG->DHTMEM + 51);
    //value = *address & 0x0000FFFFU;
    //value = value | (((uint32_t)HuffTableDC1->Bits[1] & 0xFF) << 24) | (((uint32_t)HuffTableDC1->Bits[0] & 0xFF) << 16);
    //*address = value;

    //// only Byte 0 and Byte 1 of the last word (@ DHTMEM + 55) belong to DC1 Bits table */
    //address = (JPEG->DHTMEM + 55);
    //value = *address & 0xFFFF0000U;
    //value = value | (((uint32_t)HuffTableDC1->Bits[15] & 0xFF) << 8) | ((uint32_t)HuffTableDC1->Bits[14] & 0xFF);
    //*address = value;

    //// continue setting 12 DC1 huffman Bits from DHTMEM + 54 down to DHTMEM + 52*/
    //address--;
    //index = 12;
    //while(index > 0) {
      //*address = (((uint32_t)HuffTableDC1->Bits[index+1] & 0xFF) << 24)|
                 //(((uint32_t)HuffTableDC1->Bits[index] & 0xFF) << 16)|
                 //(((uint32_t)HuffTableDC1->Bits[index-1] & 0xFF) << 8) |
                 //((uint32_t)HuffTableDC1->Bits[index-2] & 0xFF);
      //address--;
      //index -=4;
      //}
    //// DC1 Huffman Table : Val*/
    //// DC1 VALS is a 12 Bytes table i.e 3x32bits words from DHTMEM base address +55 to DHTMEM + 58 */
    //// only Byte 2 and Byte 3 of the first word (@ DHTMEM + 55) belong to DC1 Val table */
    //address = (JPEG->DHTMEM + 55);
    //value = *address & 0x0000FFFF;
    //value = value | (((uint32_t)HuffTableDC1->HuffVal[1] & 0xFF) << 24) | (((uint32_t)HuffTableDC1->HuffVal[0] & 0xFF) << 16);
    //*address = value;

    //// only Byte 0 and Byte 1 of the last word (@ DHTMEM + 58) belong to DC1 Val table */
    //address = (JPEG->DHTMEM + 58);
    //value = *address & 0xFFFF0000U;
    //value = value | (((uint32_t)HuffTableDC1->HuffVal[11] & 0xFF) << 8) | ((uint32_t)HuffTableDC1->HuffVal[10] & 0xFF);
    //*address = value;

    //// continue setting 8 DC1 huffman val from DHTMEM + 57 down to DHTMEM + 56*/
    //address--;
    //index = 8;
    //while(index > 0) {
      //*address = (((uint32_t)HuffTableDC1->HuffVal[index+1] & 0xFF) << 24)|
                 //(((uint32_t)HuffTableDC1->HuffVal[index] & 0xFF) << 16)|
                 //(((uint32_t)HuffTableDC1->HuffVal[index-1] & 0xFF) << 8) |
                 //((uint32_t)HuffTableDC1->HuffVal[index-2] & 0xFF);
      //address--;
      //index -=4;
      //}
    //}

  //if (HuffTableAC1 != NULL) {
    //// AC1 Huffman Table : BITS*/
    //// AC1 BITS is a 16 Bytes table i.e 4x32bits words from DHTMEM base address + 58 to DHTMEM + 62*/
    //// only Byte 2 and Byte 3 of the first word (@ DHTMEM + 58) belong to AC1 Bits table */
    //address = (JPEG->DHTMEM + 58);
    //value = *address & 0x0000FFFFU;
    //value = value | (((uint32_t)HuffTableAC1->Bits[1] & 0xFF) << 24) | (((uint32_t)HuffTableAC1->Bits[0] & 0xFF) << 16);
    //*address = value;

    //// only Byte 0 and Byte 1 of the last word (@ DHTMEM + 62) belong to Bits Val table */
    //address = (JPEG->DHTMEM + 62);
    //value = *address & 0xFFFF0000U;
    //value = value | (((uint32_t)HuffTableAC1->Bits[15] & 0xFF) << 8) | ((uint32_t)HuffTableAC1->Bits[14] & 0xFF);
    //*address = value;

    //// continue setting 12 AC1 huffman Bits from DHTMEM + 61 down to DHTMEM + 59*/
    //address--;
    //index = 12;
    //while(index > 0) {
      //*address = (((uint32_t)HuffTableAC1->Bits[index+1] & 0xFF) << 24)|
                 //(((uint32_t)HuffTableAC1->Bits[index] & 0xFF) << 16)|
                 //(((uint32_t)HuffTableAC1->Bits[index-1] & 0xFF) << 8) |
                 //((uint32_t)HuffTableAC1->Bits[index-2] & 0xFF);
      //address--;
      //index -=4;
      //}

    //// AC1 Huffman Table : Val*/
    //// AC1 VALS is a 162 Bytes table i.e 41x32bits words from DHTMEM base address + 62 to DHTMEM + 102 */
    //// only Byte 2 and Byte 3 of the first word (@ DHTMEM + 62) belong to AC1 VALS table */
    //address = (JPEG->DHTMEM + 62);
    //value = *address & 0x0000FFFF;
    //value = value | (((uint32_t)HuffTableAC1->HuffVal[1] & 0xFF) << 24) | (((uint32_t)HuffTableAC1->HuffVal[0] & 0xFF) << 16);
    //*address = value;

    //// continue setting 160 AC1 huffman values from DHTMEM + 63 to DHTMEM+102 */
    //address = (JPEG->DHTMEM + 102);
    //index = 160;
    //while(index > 0) {
      //*address = (((uint32_t)HuffTableAC1->HuffVal[index+1] & 0xFF) << 24)|
                 //(((uint32_t)HuffTableAC1->HuffVal[index] & 0xFF) << 16)|
                 //(((uint32_t)HuffTableAC1->HuffVal[index-1] & 0xFF) << 8) |
                 //((uint32_t)HuffTableAC1->HuffVal[index-2] & 0xFF);
      //address--;
      //index -=4;
      //}
    //}
  //}
//}}}
//{{{
//void SetHuffEncMem (JPEG_ACHuffTableTypeDef* HuffTableAC0,
                                 //JPEG_DCHuffTableTypeDef *HuffTableDC0,
                                 //JPEG_ACHuffTableTypeDef *HuffTableAC1,
                                 //JPEG_DCHuffTableTypeDef *HuffTableDC1) {

  //SetHuffDHTMem (HuffTableAC0, HuffTableDC0, HuffTableAC1, HuffTableDC1);

  //SetHuffACMem (HuffTableAC0, (JPEG->HUFFENC_AC0));
  //SetHuffACMem (HuffTableAC1, (JPEG->HUFFENC_AC1));
  //SetHuffDCMem (HuffTableDC0, JPEG->HUFFENC_DC0);
  //SetHuffDCMem (HuffTableDC1, JPEG->HUFFENC_DC1);
  //}
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
      outputData (mHandle.OutBuffPtr, mHandle.OutCount);

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

  // Stop the JPEG encoding/decoding process
  JPEG->CONFR0 &= ~JPEG_CONFR0_START;

  __HAL_JPEG_DISABLE_IT (&mHandle, JPEG_INTERRUPT_MASK);

  // Flush input and output FIFOs
  JPEG->CR |= JPEG_CR_IFF;
  JPEG->CR |= JPEG_CR_OFF;
  __HAL_JPEG_CLEAR_FLAG (&mHandle, JPEG_FLAG_ALL);

  //uint32_t acLum_huffmanTableAddr = (uint32_t)(&ACLUM_HuffTable);
  //uint32_t dcLum_huffmanTableAddr = (uint32_t)(&DCLUM_HuffTable);
  //uint32_t acChrom_huffmanTableAddr = (uint32_t)(&ACCHROM_HuffTable);
  //uint32_t dcChrom_huffmanTableAddr = (uint32_t)(&DCCHROM_HuffTable);
  //SetHuffEncMem ((JPEG_ACHuffTableTypeDef*)acLum_huffmanTableAddr,
  //               (JPEG_DCHuffTableTypeDef*)dcLum_huffmanTableAddr,
  //               (JPEG_ACHuffTableTypeDef*)acChrom_huffmanTableAddr,
  //               (JPEG_DCHuffTableTypeDef*)dcChrom_huffmanTableAddr);

  // Enable header processing
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
    //{{{  end of header, get info
    mHandle.mHeight = (JPEG->CONFR1 & 0xFFFF0000U) >> 16;
    mHandle.mWidth  = (JPEG->CONFR3 & 0xFFFF0000U) >> 16;

    // read header values from conf regs
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

    __HAL_JPEG_DISABLE_IT (&mHandle, JPEG_IT_HPD);

    // clear header processing done flag
    __HAL_JPEG_CLEAR_FLAG (&mHandle, JPEG_FLAG_HPDF);

    if (mHandle.mChromaSampling == JPEG_444_SUBSAMPLING) {
      mOutYuvLen = ((mHandle.mWidth + 7) & ~8) * ((mHandle.mHeight + 7) & ~8) * 3;
      mOutYuvStripLen = ((mHandle.mWidth + 7) & ~8) * 8 * 3;
      }
    else if (mHandle.mChromaSampling == JPEG_422_SUBSAMPLING) {
      mOutYuvLen = ((mHandle.mWidth + 15) & ~16) * ((mHandle.mHeight + 7) & ~8) * 2;
      mOutYuvStripLen = ((mHandle.mWidth + 7) & ~8) * 8 * 3;
      }
    mOutYuvBuf = (uint8_t*)sdRamAllocInt (mOutYuvLen);
    mHandle.OutBuffPtr = mOutYuvBuf;
    mHandle.OutLen = mOutYuvStripLen;

    // if the MDMA Out is triggred with JPEG Out FIFO Threshold flag then MDMA out buffer size is 32 bytes
    // else (MDMA Out is triggred with JPEG Out FIFO not empty flag then MDMA buffer size is 4 bytes
    // MDMA transfer size (BNDTR) must be a multiple of MDMA buffer size (TLEN)
    uint32_t outXfrSize = mHandle.hmdmaOut.Init.BufferTransferLength;
    mHandle.OutLen = mHandle.OutLen - (mHandle.OutLen % outXfrSize);

    // start MDMA FIFO Out transfer
    HAL_MDMA_Start_IT (&mHandle.hmdmaOut, (uint32_t)&JPEG->DOR,
                       (uint32_t)mHandle.OutBuffPtr, mHandle.OutLen, 1);
    }
    //}}}
  if (__HAL_JPEG_GET_FLAG (&mHandle, JPEG_FLAG_EOCF) != RESET) {
    //{{{  end of conversion
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
    //}}}
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

// interface
//{{{
cTile* hwJpegDecode (const string& fileName) {

  mHandle.Instance = JPEG;
  init();

  mInBuf[0].mBuf = (uint8_t*)pvPortMalloc (4096);
  mInBuf[1].mBuf = (uint8_t*)pvPortMalloc (4096);
  mOutLen = 0;

  cTile* tile = nullptr;
  FIL file;
  if (f_open (&file, fileName.c_str(), FA_READ) == FR_OK) {
    if (f_read (&file, mInBuf[0].mBuf, 4096, &mInBuf[0].mSize) == FR_OK)
      mInBuf[0].mFull = true;
    if (f_read (&file, mInBuf[1].mBuf, 4096, &mInBuf[1].mSize) == FR_OK)
      mInBuf[1].mFull = true;

    mHandle.mReadIndex = 0;
    mHandle.mWriteIndex = 0;
    mHandle.mDecodeDone = false;
    dmaDecode (mInBuf[0].mBuf, mInBuf[0].mSize);

    while (!mHandle.mDecodeDone) {
      if (!mInBuf[mHandle.mWriteIndex].mFull) {
        if (f_read (&file, mInBuf[mHandle.mWriteIndex].mBuf, 4096, &mInBuf[mHandle.mWriteIndex].mSize) == FR_OK)
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
          if (mHandle.InLen > 0)
            // Start DMA FIFO In transfer
            HAL_MDMA_Start_IT (&mHandle.hmdmaIn, (uint32_t)mHandle.InBuffPtr, (uint32_t)&JPEG->DIR, mHandle.InLen, 1);
          }

        mHandle.mWriteIndex = mHandle.mWriteIndex ? 0 : 1;
        }
      else
        vTaskDelay (1);
      }
    f_close (&file);

    printf ("hwJpegDecode %dx%d:%d - %d:%dbytes\n",
            mHandle.mWidth, mHandle.mHeight, mHandle.mChromaSampling, mOutYuvLen, mOutLen);

    auto rgb565pic = (uint16_t*)sdRamAlloc (mHandle.mWidth * mHandle.mHeight * 2);
    if (rgb565pic) {
      cLcd::jpegYuvTo565 (mOutYuvBuf, rgb565pic, mHandle.mWidth, mHandle.mHeight, mHandle.mChromaSampling);
      tile = new cTile ((uint8_t*)rgb565pic, 2, mHandle.mWidth, 0, 0, mHandle.mWidth,  mHandle.mHeight);
      }
    }

  vPortFree (mInBuf[0].mBuf);
  mInBuf[0].mBuf = nullptr;
  vPortFree (mInBuf[1].mBuf);
  mInBuf[1].mBuf = nullptr;
  sdRamFree (mOutYuvBuf);
  mOutYuvBuf = nullptr;

  return tile;
  }
//}}}

//{{{
cTile* swJpegDecode (const string& fileName, int scale) {

  FILINFO filInfo;
  if (f_stat (fileName.c_str(), &filInfo)) {
    printf ("swJpegDecode fstat fail\n");
    return nullptr;
    }

  //lcd->info (COL_YELLOW, "loadFile " + fileName + " bytes:" + dec ((int)(filInfo.fsize)) + " " +
  //           dec (filInfo.ftime >> 11) + ":" + dec ((filInfo.ftime >> 5) & 63) + " " +
  //           dec (filInfo.fdate & 31) + ":" + dec ((filInfo.fdate >> 5) & 15) + ":" + dec ((filInfo.fdate >> 9) + 1980));
  //lcd->changed();

  FIL gFile;
  if (f_open (&gFile, fileName.c_str(), FA_READ)) {
    printf ("swJpegDecode open fail\n");
    return nullptr;
    }

  auto buf = (uint8_t*)pvPortMalloc (filInfo.fsize);
  if (!buf)  {
    printf ("swJpegDecode alloc fail\n");
    return nullptr;
    }

  UINT bytesRead = 0;
  f_read (&gFile, buf, (UINT)filInfo.fsize, &bytesRead);
  f_close (&gFile);

  cTile* tile = nullptr;
  if (bytesRead > 0) {
    struct jpeg_error_mgr jerr;
    struct jpeg_decompress_struct mCinfo;
    mCinfo.err = jpeg_std_error (&jerr);
    jpeg_create_decompress (&mCinfo);

    jpeg_mem_src (&mCinfo, buf, bytesRead);
    jpeg_read_header (&mCinfo, TRUE);

    mCinfo.dct_method = JDCT_FLOAT;
    mCinfo.out_color_space = JCS_RGB;
    mCinfo.scale_num = 1;
    mCinfo.scale_denom = scale;
    jpeg_start_decompress (&mCinfo);

    auto rgb565pic = (uint16_t*)sdRamAlloc (mCinfo.output_width * mCinfo.output_height*2);
    if (rgb565pic) {
      tile = new cTile ((uint8_t*)rgb565pic, 2, mCinfo.output_width, 0,0, mCinfo.output_width, mCinfo.output_height);
      auto rgbLine = (uint8_t*)malloc (mCinfo.output_width * 3);
      while (mCinfo.output_scanline < mCinfo.output_height) {
        jpeg_read_scanlines (&mCinfo, &rgbLine, 1);
        cLcd::rgb888to565 (rgbLine, rgb565pic + ((mCinfo.output_scanline-1) * mCinfo.output_width), mCinfo.output_width, 1);
        }
      free (rgbLine);
      }
    else
      printf ("swJpegDecode alloc fail\n");
    jpeg_finish_decompress (&mCinfo);
    jpeg_destroy_decompress (&mCinfo);
    }
  else {
    printf ("swJpegDecode read fail\n");
    }

  free (buf);
  return tile;
  }
//}}}
