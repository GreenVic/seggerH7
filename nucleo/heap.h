#pragma once

#define SDRAM_DEVICE_ADDR 0xD0000000
#define SDRAM_DEVICE_SIZE 0x01000000

//{{{
#ifdef __cplusplus
 extern "C" {
#endif
//}}}

uint8_t* dtcmAlloc (size_t bytes);
uint8_t* sram123Alloc (size_t bytes);

void sdRamInit (uint32_t start, uint32_t size);
void* sdRamAlloc (size_t xWantedSize);
void sdRamFree (void* p);

//{{{
#ifdef __cplusplus
}
#endif
//}}}
