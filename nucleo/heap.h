// heap.h
#pragma once
#include <string>
//{{{
#ifdef __cplusplus
 extern "C" {
#endif
//}}}

uint8_t* dtcmAlloc (size_t bytes);
void dtcmFree (void* p);
size_t getDtcmSize();
size_t getDtcmFree();
size_t getDtcmMinFree();

size_t getSramSize();
size_t getSramFree();
size_t getSramMinFree();

uint8_t* sram123Alloc (size_t bytes);
void sram123Free (void* p);
size_t getSram123Size();
size_t getSram123Free();
size_t getSram123MinFree();

uint8_t* sdRamAlloc (size_t size, const std::string& tag);
void sdRamFree (void* p);
size_t getSdRamSize();
size_t getSdRamFree();
size_t getSdRamMinFree();

//{{{
#ifdef __cplusplus
}
#endif
//}}}
