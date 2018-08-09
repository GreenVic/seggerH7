// cHwJpeg.h
#pragma once
#include <stdint.h>
#include <string>
#include "stm32h7xx_nucleo_144.h"

class cTile;
class cHwJpeg {
public:
  cHwJpeg();

  uint32_t getWidth() { return mInfo.ImageWidth; }
  uint32_t getHeight() { return mInfo.ImageHeight; }
  uint32_t getChroma() { return mInfo.ChromaSubsampling; }
  uint8_t* getYuvBuf() { return mYuvBuf; }

  cTile* decode (const std::string& fileName);

  // callbacks
  void getData (uint32_t len);
  void dataReady (uint8_t* data, uint32_t len);
  void decodeDone();
  void jpegIrq();
  void mdmaIrq();

private:
  const uint32_t kYuvChunkSize = 0x10000;

  JPEG_HandleTypeDef mHandle;
  uint8_t* mYuvBuf = nullptr;
  JPEG_ConfTypeDef mInfo;

  //{{{  struct tBufs
  typedef struct {
    bool mFull;
    uint8_t* mBuf;
    uint32_t mSize;
    } tBufs;
  //}}}
  uint8_t mBuf0 [4096];
  uint8_t mBuf1 [4096];
  tBufs mBufs[2] = { { false, mBuf0, 0 }, { false, mBuf1, 0 } };

  __IO uint32_t mReadIndex = 0;
  __IO uint32_t mWriteIndex = 0;

  __IO bool mInPaused = false;
  __IO bool mDecodeDone = false;
  };
