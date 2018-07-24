// sd.cpp
#include "sd.h"
#include "cLcd.h"

#include "cmsis_os.h"
//#define LCD_DEBUG
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
//extern "C" { void DMA2_Stream3_IRQHandler() { HAL_DMA_IRQHandler (gSdHandle.hdmarx); } }
//extern "C" { void DMA2_Stream6_IRQHandler() { HAL_DMA_IRQHandler (gSdHandle.hdmatx); } }

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
  //gSdHandle.Init.ClockDiv            = SDMMC_TRANSFER_CLK_DIV;
  gSdHandle.Init.ClockDiv            = 10;
  gSdHandle.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
  gSdHandle.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
  gSdHandle.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
  gSdHandle.Init.BusWide             = SDMMC_BUS_WIDE_1B;

  if (isDetected() != SD_PRESENT)
    return MSD_ERROR_SD_NOT_PRESENT;

  __HAL_RCC_SDMMC1_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  GPIO_InitTypeDef gpio_init_structure;
  gpio_init_structure.Mode = GPIO_MODE_AF_PP;
  gpio_init_structure.Pull = GPIO_PULLUP;
  gpio_init_structure.Speed = GPIO_SPEED_FREQ_HIGH;

  // GPIOC config D0 - PC8, D1 - PC9, D2 - PC10, D3 - PC11  
  gpio_init_structure.Alternate = GPIO_AF12_SDIO1;
  gpio_init_structure.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;
  HAL_GPIO_Init(GPIOC, &gpio_init_structure);

  // GPIOD config CMD - PD2
  gpio_init_structure.Pin = GPIO_PIN_2;
  HAL_GPIO_Init(GPIOD, &gpio_init_structure);

  // CK - PC12
  gpio_init_structure.Pin = GPIO_PIN_12;
  gpio_init_structure.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio_init_structure.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &gpio_init_structure);

  __HAL_RCC_SDMMC1_FORCE_RESET();
  __HAL_RCC_SDMMC1_RELEASE_RESET();

  // NVIC configuration for SDIO interrupts
  HAL_NVIC_SetPriority (SDMMC1_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ (SDMMC1_IRQn);

  //{{{  DMA rx parameters
  //gDmaRxHandle.Instance                 = DMA2_Stream3;
  //gDmaRxHandle.Init.Channel             = DMA_CHANNEL_4;
  //gDmaRxHandle.Init.Direction           = DMA_PERIPH_TO_MEMORY;
  //gDmaRxHandle.Init.PeriphInc           = DMA_PINC_DISABLE;
  //gDmaRxHandle.Init.MemInc              = DMA_MINC_ENABLE;
  //gDmaRxHandle.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
  //gDmaRxHandle.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
  //gDmaRxHandle.Init.Mode                = DMA_PFCTRL;
  //gDmaRxHandle.Init.Priority            = DMA_PRIORITY_VERY_HIGH;
  //gDmaRxHandle.Init.FIFOMode            = DMA_FIFOMODE_ENABLE;
  //gDmaRxHandle.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL;
  //gDmaRxHandle.Init.MemBurst            = DMA_MBURST_INC4;
  //gDmaRxHandle.Init.PeriphBurst         = DMA_PBURST_INC4;

  //__HAL_LINKDMA (&gSdHandle, hdmarx, gDmaRxHandle);
  //HAL_DMA_DeInit (&gDmaRxHandle);
  //HAL_DMA_Init (&gDmaRxHandle);
  //}}}
  //{{{  DMA tx parameters
  //gDmaTxHandle.Instance                 = DMA2_Stream6;
  //gDmaTxHandle.Init.Channel             = DMA_CHANNEL_4;
  //gDmaTxHandle.Init.Direction           = DMA_MEMORY_TO_PERIPH;
  //gDmaTxHandle.Init.PeriphInc           = DMA_PINC_DISABLE;
  //gDmaTxHandle.Init.MemInc              = DMA_MINC_ENABLE;
  //gDmaTxHandle.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
  //gDmaTxHandle.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
  //gDmaTxHandle.Init.Mode                = DMA_PFCTRL;
  //gDmaTxHandle.Init.Priority            = DMA_PRIORITY_VERY_HIGH;
  //gDmaTxHandle.Init.FIFOMode            = DMA_FIFOMODE_ENABLE;
  //gDmaTxHandle.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL;
  //gDmaTxHandle.Init.MemBurst            = DMA_MBURST_INC4;
  //gDmaTxHandle.Init.PeriphBurst         = DMA_PBURST_INC4;

  //__HAL_LINKDMA (&gSdHandle, hdmatx, gDmaTxHandle);
  //HAL_DMA_DeInit (&gDmaTxHandle);
  //HAL_DMA_Init (&gDmaTxHandle);
  //}}}

  //HAL_NVIC_SetPriority (DMA2_Stream3_IRQn, 6, 0);
  //HAL_NVIC_EnableIRQ (DMA2_Stream3_IRQn);
  //HAL_NVIC_SetPriority (DMA2_Stream6_IRQn, 6, 0);
  //HAL_NVIC_EnableIRQ (DMA2_Stream6_IRQn);

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
