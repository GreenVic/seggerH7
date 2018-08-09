// cHwJpeg.cpp
//{{{  includes
#include "cHwJpeg.h"

#include "cmsis_os.h"
#include "stm32h7xx_nucleo_144.h"
#include "heap.h"

#include "cLcd.h"
#include "../fatFs/ff.h"

using namespace std;
//}}}

//{{{
cHwJpeg::cHwJpeg() {
  mHandle.Instance = JPEG;
  HAL_JPEG_Init (&mHandle);
  }
//}}}

//{{{
cTile* cHwJpeg::decode (const string& fileName) {

  FIL file;
  if (f_open (&file, fileName.c_str(), FA_READ) == FR_OK) {
    if (f_read (&file, mBufs[0].mBuf, 4096, &mBufs[0].mSize) == FR_OK)
      mBufs[0].mFull = true;
    if (f_read (&file, mBufs[1].mBuf, 4096, &mBufs[1].mSize) == FR_OK)
      mBufs[1].mFull = true;

    if (!mYuvBuf)
      mYuvBuf = (uint8_t*)sdRamAlloc (400*272*3);

    mReadIndex = 0;
    mWriteIndex = 0;
    mInPaused = 0;
    mDecodeDone = false;
    HAL_JPEG_Decode_DMA (&mHandle, mBufs[0].mBuf, mBufs[0].mSize, mYuvBuf, kYuvChunkSize);

    while (!mDecodeDone) {
      if (!mBufs[mWriteIndex].mFull) {
        if (f_read (&file, mBufs[mWriteIndex].mBuf, 4096, &mBufs[mWriteIndex].mSize) == FR_OK)
          mBufs[mWriteIndex].mFull = true;
        if (mInPaused && (mWriteIndex == mReadIndex)) {
          mInPaused = false;
          HAL_JPEG_ConfigInputBuffer (&mHandle, mBufs[mReadIndex].mBuf, mBufs[mReadIndex].mSize);
          HAL_JPEG_Resume (&mHandle, JPEG_PAUSE_RESUME_INPUT);
          }
        mWriteIndex = mWriteIndex ? 0 : 1;
        }
      else
        vTaskDelay (1);
      }

    f_close (&file);

    HAL_JPEG_GetInfo (&mHandle, &mInfo);
    auto rgb565pic = (uint16_t*)sdRamAlloc (getWidth() * getHeight() * 2);
    cLcd::jpegYuvTo565 (getYuvBuf(), rgb565pic, getWidth(), getHeight(), getChroma());
    return new cTile ((uint8_t*)rgb565pic, 2, getWidth(), 0, 0, getWidth(), getHeight());
    }

  return nullptr;
  }
//}}}

// callbacks
//{{{
void cHwJpeg::getData (uint32_t len) {

  if (len != mBufs[mReadIndex].mSize)
    HAL_JPEG_ConfigInputBuffer (&mHandle, mBufs[mReadIndex].mBuf+len, mBufs[mReadIndex].mSize-len);

  else {
    mBufs [mReadIndex].mFull = false;
    mBufs [mReadIndex].mSize = 0;

    mReadIndex = mReadIndex ? 0 : 1;
    if (mBufs [mReadIndex].mFull)
      HAL_JPEG_ConfigInputBuffer (&mHandle, mBufs[mReadIndex].mBuf, mBufs[mReadIndex].mSize);
    else {
      HAL_JPEG_Pause (&mHandle, JPEG_PAUSE_RESUME_INPUT);
      mInPaused = true;
      }
    }
  }
//}}}
//{{{
void cHwJpeg::dataReady (uint8_t* data, uint32_t len) {
  HAL_JPEG_ConfigOutputBuffer (&mHandle, data+len, kYuvChunkSize);
  }
//}}}
//{{{
void cHwJpeg::decodeDone() {
  mDecodeDone = true;
  }
//}}}
//{{{
void cHwJpeg::jpegIrq() {
  HAL_JPEG_IRQHandler (&mHandle);
  }
//}}}
//{{{
void cHwJpeg::mdmaIrq() {
  HAL_MDMA_IRQHandler (mHandle.hdmain);
  HAL_MDMA_IRQHandler (mHandle.hdmaout);
  }
//}}}
