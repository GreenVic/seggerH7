#pragma once

#define SDRAM_DEVICE_ADDR 0xD0000000
#define SDRAM_DEVICE_SIZE 0x01000000

//{{{
#ifdef __cplusplus
 extern "C" {
#endif
//}}}
uint8_t* sdRamAlloc (uint32_t bytes);
//{{{
#ifdef __cplusplus
}
#endif
//}}}
