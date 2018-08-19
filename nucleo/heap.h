#pragma once

//{{{
#ifdef __cplusplus
 extern "C" {
#endif
//}}}

uint8_t* dtcmAlloc (size_t bytes);
void dtcmFree (void* p);
size_t getDtcmFreeSize();
size_t getDtcmMinFreeSize();

uint8_t* sramAlloc (size_t bytes);
void sramFree (void* p);
size_t getSramFreeSize();
size_t getSramMinFreeSize();

uint8_t* sram123Alloc (size_t bytes);
void sram123Free (void* p);
size_t getSram123FreeSize();
size_t getSram123MinFreeSize();

uint8_t* sdRamAlloc (size_t size);
uint8_t* sdRamAllocInt (size_t size);
void sdRamFree (void* p);
size_t getSdRamFreeSize();
size_t getSdRamMinFreeSize();

//{{{
#ifdef __cplusplus
}
#endif
//}}}
