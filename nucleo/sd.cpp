// sd.cpp
#include "sd.h"
#include "cLcd.h"
#include "cmsis_os.h"

#define QUEUE_SIZE      10
#define READ_CPLT_MSG   1
#define WRITE_CPLT_MSG  2
#define SD_TIMEOUT 1000

SD_HandleTypeDef gSdHandle;
DMA_HandleTypeDef gDmaRxHandle;
DMA_HandleTypeDef gDmaTxHandle;

#define SD_DEFAULT_BLOCK_SIZE 512

static volatile DSTATUS gStat = STA_NOINIT;
osMessageQId gSdQueueId;

//{{{
void HAL_SD_TxCpltCallback (SD_HandleTypeDef* hsd) {
  osMessagePut (gSdQueueId, WRITE_CPLT_MSG, osWaitForever);
  }
//}}}
//{{{
void HAL_SD_RxCpltCallback (SD_HandleTypeDef* hsd) {
  osMessagePut (gSdQueueId, READ_CPLT_MSG, osWaitForever);
  }
//}}}
extern "C" { void SDMMC1_IRQHandler() { HAL_SD_IRQHandler (&gSdHandle); } }

//{{{
uint8_t isDetected() {
  return SD_PRESENT;
  }
//}}}
//{{{
uint8_t getCardState() {
  return HAL_SD_GetCardState (&gSdHandle) == HAL_SD_CARD_TRANSFER ? SD_TRANSFER_OK : SD_TRANSFER_BUSY;
  }
//}}}
//{{{
void getCardInfo (HAL_SD_CardInfoTypeDef* cardInfo) {
  HAL_SD_GetCardInfo (&gSdHandle, cardInfo);
  }
//}}}
//{{{
DSTATUS checkStatus (BYTE lun) {

  gStat = STA_NOINIT;

  if (getCardState() == MSD_OK)
    gStat &= ~STA_NOINIT;

  return gStat;
  }
//}}}

//{{{
DSTATUS SD_initialize (BYTE lun) {

  gStat = STA_NOINIT;

  /* uSD device interface configuration */
  gSdHandle.Instance = SDMMC1;
  //gSdHandle.Init.ClockBypass         = SDMMC_CLOCK_BYPASS_DISABLE;
  gSdHandle.Init.ClockDiv            = 1;
  gSdHandle.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
  gSdHandle.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
  gSdHandle.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
  gSdHandle.Init.BusWide             = SDMMC_BUS_WIDE_4B;

  if (isDetected() != SD_PRESENT)
    return MSD_ERROR_SD_NOT_PRESENT;

  __HAL_RCC_SDMMC1_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  GPIO_InitTypeDef gpio_init_structure;
  gpio_init_structure.Mode = GPIO_MODE_AF_PP;
  gpio_init_structure.Pull = GPIO_PULLUP;
  gpio_init_structure.Speed = GPIO_SPEED_FREQ_HIGH;

  // GPIOC config D0-3 - PC8-11
  gpio_init_structure.Alternate = GPIO_AF12_SDIO1;
  gpio_init_structure.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;
  HAL_GPIO_Init (GPIOC, &gpio_init_structure);

  // GPIOD config Cmd - PD2
  gpio_init_structure.Pin = GPIO_PIN_2;
  HAL_GPIO_Init(GPIOD, &gpio_init_structure);

  // GPIOC config Clk - PC12
  gpio_init_structure.Pin = GPIO_PIN_12;
  gpio_init_structure.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio_init_structure.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &gpio_init_structure);

  __HAL_RCC_SDMMC1_FORCE_RESET();
  __HAL_RCC_SDMMC1_RELEASE_RESET();

  // NVIC configuration for SDIO interrupts
  HAL_NVIC_SetPriority (SDMMC1_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ (SDMMC1_IRQn);

  if (HAL_SD_Init (&gSdHandle) != HAL_OK)
    cLcd::mLcd->debug (COL_RED, "HAL_SD_Init failed");

  osMessageQDef (sdQueue, QUEUE_SIZE, uint16_t);
  gSdQueueId = osMessageCreate (osMessageQ (sdQueue), NULL);

  gStat = checkStatus (lun);

  return gStat;
  }
//}}}
//{{{
DSTATUS SD_status (BYTE lun) {
  return checkStatus (lun);
  }
//}}}
//{{{
DRESULT SD_read (BYTE lun, BYTE* buff, DWORD sector, UINT count) {

  #ifdef LCD_DEBUG
    cLcd::mLcd->debug (COL_GREEN, "readBlocks " + hex (uint32_t(buff)) + " " + dec (sector) + " " + dec(count));
  #endif

  if (HAL_SD_ReadBlocks_DMA (&gSdHandle, buff, sector, count) == HAL_OK) {
    osEvent event = osMessageGet (gSdQueueId, SD_TIMEOUT);
    if (event.status == osEventMessage) {
      if (event.value.v == READ_CPLT_MSG) {
        uint32_t timer = osKernelSysTick();
        while (timer < osKernelSysTick() + SD_TIMEOUT) {
          if (HAL_SD_GetCardState (&gSdHandle) == HAL_SD_CARD_TRANSFER)
            return RES_OK;
          osDelay (1);
          }
        }
      }
    }

  return RES_ERROR;
  }
//}}}
//{{{
DRESULT SD_write (BYTE lun, const BYTE* buff, DWORD sector, UINT count) {

  if (HAL_SD_WriteBlocks_DMA (&gSdHandle, (BYTE*)buff, sector, count) == HAL_OK) {
    auto event = osMessageGet (gSdQueueId, SD_TIMEOUT);
    if (event.status == osEventMessage) {
      if (event.value.v == WRITE_CPLT_MSG) {
        auto ticks2 = osKernelSysTick();
        while (ticks2 < osKernelSysTick() + SD_TIMEOUT) {
          if (HAL_SD_GetCardState (&gSdHandle) == HAL_SD_CARD_TRANSFER)
            return  RES_OK;
          osDelay (1);
          }
        }
      }
    }

  return RES_ERROR;
  }
//}}}
//{{{
DRESULT SD_ioctl (BYTE lun, BYTE cmd, void* buff) {

  if (gStat & STA_NOINIT)
    return RES_NOTRDY;

  BSP_SD_CardInfo CardInfo;
  switch (cmd) {
    /* Make sure that no pending write process */
    case CTRL_SYNC :
      return RES_OK;

    /* Get number of sectors on the disk (DWORD) */
    case GET_SECTOR_COUNT :
      getCardInfo(&CardInfo);
      *(DWORD*)buff = CardInfo.LogBlockNbr;
      return RES_OK;

    /* Get R/W sector size (WORD) */
    case GET_SECTOR_SIZE :
      getCardInfo(&CardInfo);
      *(WORD*)buff = CardInfo.LogBlockSize;
      return RES_OK;

    /* Get erase block size in unit of sector (DWORD) */
    case GET_BLOCK_SIZE :
      getCardInfo(&CardInfo);
      *(DWORD*)buff = CardInfo.LogBlockSize / SD_DEFAULT_BLOCK_SIZE;
      return RES_OK;

    default:
      return RES_PARERR;
    }
  }
//}}}

//{{{
const Diskio_drvTypeDef SD_Driver = {
  SD_initialize,
  SD_status,
  SD_read,
  SD_write,
  SD_ioctl,
  };
//}}}
