#pragma once

#define DTCM_ADDR 0x20000000
#define DTCM_SIZE 0x00020000

#define SRAM123_ADDR 0x30000000
#define SRAM123_SIZE 0x00048000

#define SDRAM_DEVICE_ADDR 0xD0000000
#define SDRAM_DEVICE_SIZE 0x08000000

//{{{
#ifdef __cplusplus
 extern "C" {
#endif
//}}}

void dtcmInit (uint32_t start, uint32_t size);
uint8_t* dtcmAlloc (size_t bytes);
void dtcmFree (void* p);

void sram123Init (uint32_t start, uint32_t size);
uint8_t* sram123Alloc (size_t bytes);
uint8_t* sram123AllocInt (size_t bytes);
void sram123Free (void* p);

void sdRamInit (uint32_t start, uint32_t size);
void* sdRamAlloc (size_t size);
void* sdRamAllocInt (size_t size);
void sdRamFree (void* p);

//{{{
#ifdef __cplusplus
}
#endif
//}}}
