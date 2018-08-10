// jpeg.h
#pragma once
#include <string>

class cTile;

cTile* hwJpegDecode (const std::string& fileName);
cTile* swJpegDecode (const std::string& fileName, int scale);
