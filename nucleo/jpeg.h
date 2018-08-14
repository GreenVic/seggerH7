// jpeg.h
#pragma once
#include <string>
#include <stdint.h>
#include "../Fatfs/ff.h"

class cTile;

extern "C" { size_t read_file (FIL* file, uint8_t* buf, uint32_t sizeofbuf); }
extern "C" { size_t write_file (FIL* file, uint8_t* buf, uint32_t sizeofbuf); }

cTile* hwJpegDecode (const std::string& fileName);
cTile* swJpegDecode (const std::string& fileName, int scale);
