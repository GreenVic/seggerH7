// cHwJpeg.h
#pragma once
#include <stdint.h>
#include <string>

class cTile;
class cHwJpeg {
public:
  cHwJpeg();

  uint32_t getWidth();
  uint32_t getHeight();
  uint32_t getChroma();

  cTile* decode (const std::string& fileName);
  };
