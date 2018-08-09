// cHwJpeg.h
#pragma once
#include <stdint.h>
#include <string>

class cTile;
class cHwJpeg {
public:
  cTile* decode (const std::string& fileName);
  };
