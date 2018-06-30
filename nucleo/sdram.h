#pragma once
//{{{
#ifdef __cplusplus
 extern "C" {
#endif
//}}}

#include "stm32h7xx_hal.h"

#define SDRAM_OK     ((uint8_t)0x00)
#define SDRAM_ERROR  ((uint8_t)0x01)

#define SDRAM_DEVICE_ADDR  ((uint32_t)0xD0000000)
#define SDRAM_DEVICE_SIZE  ((uint32_t)0x1000000)  /* SDRAM device size in Bytes (32MB)*/

uint8_t BSP_SDRAM_Init();
void BSP_SDRAM_Initialization_sequence(uint32_t RefreshCount);
void BSP_SDRAM_MspInit(SDRAM_HandleTypeDef  *hsdram, void *Params);

//{{{
#ifdef __cplusplus
}
#endif
//}}}
