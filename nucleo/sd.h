#pragma once
//{{{
#ifdef __cplusplus
 extern "C" {
#endif
//}}}

#include "../FatFs/ff_gen_drv.h"
#include "../common/stm32h7xx_nucleo_144.h"

#define BSP_SD_CardInfo HAL_SD_CardInfoTypeDef
extern const Diskio_drvTypeDef SD_Driver;

#define MSD_OK                    ((uint8_t)0x00)
#define MSD_ERROR                 ((uint8_t)0x01)
#define MSD_ERROR_SD_NOT_PRESENT  ((uint8_t)0x02)

#define SD_TRANSFER_OK            ((uint8_t)0x00)
#define SD_TRANSFER_BUSY          ((uint8_t)0x01)

#define SD_PRESENT                ((uint8_t)0x01)
#define SD_NOT_PRESENT            ((uint8_t)0x00)

uint8_t BSP_SD_Init();

uint8_t BSP_SD_GetCardState();
uint8_t BSP_SD_IsDetected();
void    BSP_SD_GetCardInfo (HAL_SD_CardInfoTypeDef *CardInfo);

uint8_t BSP_SD_ReadBlocks (uint32_t *data, uint32_t readAddr, uint32_t numBlocks);
uint8_t BSP_SD_WriteBlocks (uint32_t *data, uint32_t writeAddr, uint32_t numBlocks);

//{{{
#ifdef __cplusplus
  }
#endif
//}}}
