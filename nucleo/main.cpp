// main.cpp
//{{{  includes
#include "cmsis_os.h"
#include "stm32h7xx_nucleo_144.h"

#include "cLcd.h"
#include "sd.h"

#include "../fatFs/ff.h"
#include "jpeglib.h"

using namespace std;
//}}}
//{{{  const
const string kHello = "*stm32h7 testbed " + string(__TIME__) + " " + string(__DATE__);

#define SDRAM_DEVICE_ADDR 0xD0000000
#define SDRAM_DEVICE_SIZE 0x01000000  // 0x01000000

const HeapRegion_t kHeapRegions[] = {
  {(uint8_t*)(SDRAM_DEVICE_ADDR + LCD_WIDTH*LCD_HEIGHT*4), SDRAM_DEVICE_SIZE - LCD_WIDTH*LCD_HEIGHT*4 },
  { nullptr, 0 } };
//}}}

cLcd* lcd = nullptr;
uint16_t* mSdRamAlloc = (uint16_t*)SDRAM_DEVICE_ADDR;
vector<string> mFileVec;
vector<cTile*> mTileVec;
//{{{  jpeg vars
#define JPEG_BUFFER_EMPTY  0
#define JPEG_BUFFER_FULL   1

#define CHUNK_SIZE_IN  ((uint32_t)(4096))
#define CHUNK_SIZE_OUT ((uint32_t)(64 * 1024))

#define NB_INPUT_DATA_BUFFERS  2
#define NB_OUTPUT_DATA_BUFFERS 2

JPEG_HandleTypeDef JPEG_Handle;
uint32_t jpegYuvBuf = 0;
uint32_t jpegYuvPtr = 0;

FIL* jpegFile;

uint8_t JPEG_Data_InBuffer0 [CHUNK_SIZE_IN];
uint8_t JPEG_Data_InBuffer1 [CHUNK_SIZE_IN];
uint32_t JPEG_IN_Read_BufferIndex = 0;
uint32_t JPEG_IN_Write_BufferIndex = 0;

//{{{  struct JPEG_Data_BufferTypeDef
typedef struct {
  uint8_t State;
  uint8_t* DataBuffer;
  uint32_t DataBufferSize;
  } JPEG_Data_BufferTypeDef;
//}}}
JPEG_Data_BufferTypeDef Jpeg_IN_BufferTab [NB_INPUT_DATA_BUFFERS] = {
  {JPEG_BUFFER_EMPTY, JPEG_Data_InBuffer0, 0},
  {JPEG_BUFFER_EMPTY, JPEG_Data_InBuffer1, 0}
  };

bool jpegDecodeDone = false;
__IO bool jpegInputPaused = 0;

//}}}

extern "C" { void JPEG_IRQHandler() { HAL_JPEG_IRQHandler (&JPEG_Handle); }  }
extern "C" { void MDMA_IRQHandler() { HAL_MDMA_IRQHandler (JPEG_Handle.hdmain); HAL_MDMA_IRQHandler (JPEG_Handle.hdmaout); }  }
extern "C" { void EXTI15_10_IRQHandler() { HAL_GPIO_EXTI_IRQHandler (USER_BUTTON_PIN); } }
//{{{
void HAL_GPIO_EXTI_Callback (uint16_t GPIO_Pin) {
  if (GPIO_Pin == USER_BUTTON_PIN)
    lcd->toggle();
  }
//}}}

//{{{
uint16_t* sdRamAlloc (uint32_t words) {
  auto alloc = mSdRamAlloc;
  mSdRamAlloc += words;
  return alloc;
  }
//}}}

//{{{
uint32_t JPEG_Decode_DMA (JPEG_HandleTypeDef* hjpeg, FIL* file, uint32_t DestAddress) {

  jpegFile = file;
  jpegYuvPtr = DestAddress;

  // Read from JPG file and fill input buffers
  for (uint32_t i = 0; i < NB_INPUT_DATA_BUFFERS; i++) {
    if (f_read (jpegFile, Jpeg_IN_BufferTab[i].DataBuffer, CHUNK_SIZE_IN,
                       (UINT*)(&Jpeg_IN_BufferTab[i].DataBufferSize)) == FR_OK)
      Jpeg_IN_BufferTab[i].State = JPEG_BUFFER_FULL;
    else
      printf ("JPEG_Decode_DMA read failed\n");
    }

  HAL_JPEG_Decode_DMA (hjpeg, Jpeg_IN_BufferTab[0].DataBuffer, Jpeg_IN_BufferTab[0].DataBufferSize,
                       (uint8_t*)jpegYuvPtr, CHUNK_SIZE_OUT);
  return 0;
  }
//}}}
//{{{
bool jpegInputHandler (JPEG_HandleTypeDef* hjpeg) {

  if (!jpegDecodeDone) {
    if (Jpeg_IN_BufferTab[JPEG_IN_Write_BufferIndex].State == JPEG_BUFFER_EMPTY) {
      if (f_read (jpegFile, Jpeg_IN_BufferTab[JPEG_IN_Write_BufferIndex].DataBuffer, CHUNK_SIZE_IN,
                           (UINT*)(&Jpeg_IN_BufferTab[JPEG_IN_Write_BufferIndex].DataBufferSize)) == FR_OK)
        Jpeg_IN_BufferTab[JPEG_IN_Write_BufferIndex].State = JPEG_BUFFER_FULL;
      else
        printf ("jpegInputHandler read failed\n");

      if (jpegInputPaused  && (JPEG_IN_Write_BufferIndex == JPEG_IN_Read_BufferIndex)) {
        jpegInputPaused = false;
        HAL_JPEG_ConfigInputBuffer (hjpeg,Jpeg_IN_BufferTab[JPEG_IN_Read_BufferIndex].DataBuffer,
                                    Jpeg_IN_BufferTab[JPEG_IN_Read_BufferIndex].DataBufferSize);
        HAL_JPEG_Resume (hjpeg, JPEG_PAUSE_RESUME_INPUT);
        }

      JPEG_IN_Write_BufferIndex++;
      if (JPEG_IN_Write_BufferIndex >= NB_INPUT_DATA_BUFFERS)
        JPEG_IN_Write_BufferIndex = 0;
      }
    return false;
    }
  else
    return true;
  }
//}}}
//{{{
void HAL_JPEG_GetDataCallback (JPEG_HandleTypeDef* hjpeg, uint32_t NbDecodedData) {

  if (NbDecodedData == Jpeg_IN_BufferTab[JPEG_IN_Read_BufferIndex].DataBufferSize) {
    Jpeg_IN_BufferTab [JPEG_IN_Read_BufferIndex].State = JPEG_BUFFER_EMPTY;
    Jpeg_IN_BufferTab [JPEG_IN_Read_BufferIndex].DataBufferSize = 0;

    JPEG_IN_Read_BufferIndex++;
    if (JPEG_IN_Read_BufferIndex >= NB_INPUT_DATA_BUFFERS)
      JPEG_IN_Read_BufferIndex = 0;

    if (Jpeg_IN_BufferTab [JPEG_IN_Read_BufferIndex].State == JPEG_BUFFER_EMPTY) {
      HAL_JPEG_Pause (hjpeg, JPEG_PAUSE_RESUME_INPUT);
      jpegInputPaused = true;
      }
    else
      HAL_JPEG_ConfigInputBuffer (hjpeg, Jpeg_IN_BufferTab[JPEG_IN_Read_BufferIndex].DataBuffer,
                                         Jpeg_IN_BufferTab[JPEG_IN_Read_BufferIndex].DataBufferSize);
    }
  else
    HAL_JPEG_ConfigInputBuffer (hjpeg, Jpeg_IN_BufferTab[JPEG_IN_Read_BufferIndex].DataBuffer + NbDecodedData,
                                       Jpeg_IN_BufferTab[JPEG_IN_Read_BufferIndex].DataBufferSize - NbDecodedData);
  }
//}}}
//{{{
void HAL_JPEG_DataReadyCallback (JPEG_HandleTypeDef* hjpeg, uint8_t* pDataOut, uint32_t OutDataLength) {

  // Update JPEG encoder output buffer address
  jpegYuvPtr += OutDataLength;
  HAL_JPEG_ConfigOutputBuffer (hjpeg, (uint8_t*)jpegYuvPtr, CHUNK_SIZE_OUT);
  }
//}}}
void HAL_JPEG_DecodeCpltCallback (JPEG_HandleTypeDef* hjpeg) { jpegDecodeDone = true; }
void HAL_JPEG_InfoReadyCallback (JPEG_HandleTypeDef* hjpeg, JPEG_ConfTypeDef* pInfo) {}
void HAL_JPEG_ErrorCallback (JPEG_HandleTypeDef* hjpeg) {}
//{{{
void HAL_JPEG_MspInit(JPEG_HandleTypeDef *hjpeg) {

  static MDMA_HandleTypeDef hmdmaIn;
  static MDMA_HandleTypeDef hmdmaOut;

  /* Enable JPEG clock */
  __HAL_RCC_JPGDECEN_CLK_ENABLE();

  /* Enable MDMA clock */
  __HAL_RCC_MDMA_CLK_ENABLE();

  HAL_NVIC_SetPriority (JPEG_IRQn, 0x07, 0x0F);
  HAL_NVIC_EnableIRQ (JPEG_IRQn);

  /* Input MDMA */
  hmdmaIn.Init.Priority           = MDMA_PRIORITY_HIGH;
  hmdmaIn.Init.Endianness         = MDMA_LITTLE_ENDIANNESS_PRESERVE;
  hmdmaIn.Init.SourceInc          = MDMA_SRC_INC_BYTE;
  hmdmaIn.Init.DestinationInc     = MDMA_DEST_INC_DISABLE;
  hmdmaIn.Init.SourceDataSize     = MDMA_SRC_DATASIZE_BYTE;
  hmdmaIn.Init.DestDataSize       = MDMA_DEST_DATASIZE_WORD;
  hmdmaIn.Init.DataAlignment      = MDMA_DATAALIGN_PACKENABLE;
  hmdmaIn.Init.SourceBurst        = MDMA_SOURCE_BURST_32BEATS;
  hmdmaIn.Init.DestBurst          = MDMA_DEST_BURST_16BEATS;
  hmdmaIn.Init.SourceBlockAddressOffset = 0;
  hmdmaIn.Init.DestBlockAddressOffset  = 0;

  /*Using JPEG Input FIFO Threshold as a trigger for the MDMA*/
  hmdmaIn.Init.Request = MDMA_REQUEST_JPEG_INFIFO_TH; /* Set the MDMA HW trigger to JPEG Input FIFO Threshold flag*/
  hmdmaIn.Init.TransferTriggerMode = MDMA_BUFFER_TRANSFER;
  hmdmaIn.Init.BufferTransferLength = 32; /*Set the MDMA buffer size to the JPEG FIFO threshold size i.e 32 bytes (8 words)*/

  hmdmaIn.Instance = MDMA_Channel7;

  /* Associate the DMA handle */
  __HAL_LINKDMA(hjpeg, hdmain, hmdmaIn);

  /* DeInitialize the DMA Stream */
  HAL_MDMA_DeInit(&hmdmaIn);
  /* Initialize the DMA stream */
  HAL_MDMA_Init(&hmdmaIn);


  /* Output MDMA */
  /* Set the parameters to be configured */
  hmdmaOut.Init.Priority        = MDMA_PRIORITY_VERY_HIGH;
  hmdmaOut.Init.Endianness      = MDMA_LITTLE_ENDIANNESS_PRESERVE;
  hmdmaOut.Init.SourceInc       = MDMA_SRC_INC_DISABLE;
  hmdmaOut.Init.DestinationInc  = MDMA_DEST_INC_BYTE;
  hmdmaOut.Init.SourceDataSize  = MDMA_SRC_DATASIZE_WORD;
  hmdmaOut.Init.DestDataSize    = MDMA_DEST_DATASIZE_BYTE;
  hmdmaOut.Init.DataAlignment   = MDMA_DATAALIGN_PACKENABLE;
  hmdmaOut.Init.SourceBurst     = MDMA_SOURCE_BURST_32BEATS;
  hmdmaOut.Init.DestBurst       = MDMA_DEST_BURST_32BEATS;
  hmdmaOut.Init.SourceBlockAddressOffset = 0;
  hmdmaOut.Init.DestBlockAddressOffset  = 0;


  /*Using JPEG Output FIFO Threshold as a trigger for the MDMA*/
  hmdmaOut.Init.Request              = MDMA_REQUEST_JPEG_OUTFIFO_TH; /* Set the MDMA HW trigger to JPEG Output FIFO Threshold flag*/
  hmdmaOut.Init.TransferTriggerMode  = MDMA_BUFFER_TRANSFER;
  hmdmaOut.Init.BufferTransferLength = 32; /*Set the MDMA buffer size to the JPEG FIFO threshold size i.e 32 bytes (8 words)*/

  hmdmaOut.Instance = MDMA_Channel6;
  /* DeInitialize the DMA Stream */
  HAL_MDMA_DeInit (&hmdmaOut);
  /* Initialize the DMA stream */
  HAL_MDMA_Init (&hmdmaOut);

  /* Associate the DMA handle */
  __HAL_LINKDMA (hjpeg, hdmaout, hmdmaOut);

  HAL_NVIC_SetPriority (MDMA_IRQn, 0x08, 0x0F);
  HAL_NVIC_EnableIRQ (MDMA_IRQn);
  }
//}}}


//{{{
void clockConfig() {
//   System Clock       = PLL (HSE BYPASS)
//   SYSCLK(Hz)         = 400000000 (CPU Clock)
//   HCLK(Hz)           = 200000000 (AXI and AHBs Clock)
//   AHB Prescaler      = 2
//   D1 APB3 Prescaler  = 2 (APB3 Clock  100MHz)
//   D2 APB1 Prescaler  = 2 (APB1 Clock  100MHz)
//   D2 APB2 Prescaler  = 2 (APB2 Clock  100MHz)
//   D3 APB4 Prescaler  = 2 (APB4 Clock  100MHz)
//   HSE Frequency(Hz)  = 8000000
//   PLL_M              = 4
//   PLL_N              = 400
//   PLL_P              = 2
//   PLL_Q              = 4
//   PLL_R              = 2
//   VDD(V)             = 3.3
//   Flash Latency(WS)  = 4

  MODIFY_REG (PWR->CR3, PWR_CR3_SCUEN, 0);

  // Voltage scaling optimises power consumption when clocked below maximum system frequency
  __HAL_PWR_VOLTAGESCALING_CONFIG (PWR_REGULATOR_VOLTAGE_SCALE1);
  while (!__HAL_PWR_GET_FLAG (PWR_FLAG_VOSRDY)) {}

  // Enable D2 domain SRAM3 Clock (0x30040000 AXI)
  __HAL_RCC_D2SRAM3_CLK_ENABLE();

  // enable HSE Oscillator, activate PLL HSE source
  RCC_OscInitTypeDef rccOscInit;
  rccOscInit.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  rccOscInit.HSEState = RCC_HSE_BYPASS;
  rccOscInit.HSIState = RCC_HSI_OFF;
  rccOscInit.CSIState = RCC_CSI_OFF;
  rccOscInit.PLL.PLLState = RCC_PLL_ON;
  rccOscInit.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  rccOscInit.PLL.PLLM = 4;
  rccOscInit.PLL.PLLN = 400;
  rccOscInit.PLL.PLLP = 2;
  rccOscInit.PLL.PLLR = 2;
  rccOscInit.PLL.PLLQ = 4;
  rccOscInit.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  rccOscInit.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  HAL_RCC_OscConfig (&rccOscInit);

  // select PLL system clock source. config bus clocks dividers
  RCC_ClkInitTypeDef rccClkInit;
  rccClkInit.ClockType = (RCC_CLOCKTYPE_SYSCLK  | RCC_CLOCKTYPE_HCLK |
                          RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_PCLK1 |
                          RCC_CLOCKTYPE_PCLK2   | RCC_CLOCKTYPE_D3PCLK1);
  rccClkInit.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  rccClkInit.SYSCLKDivider = RCC_SYSCLK_DIV1;
  rccClkInit.AHBCLKDivider = RCC_HCLK_DIV2;
  rccClkInit.APB3CLKDivider = RCC_APB3_DIV2;
  rccClkInit.APB1CLKDivider = RCC_APB1_DIV2;
  rccClkInit.APB2CLKDivider = RCC_APB2_DIV2;
  rccClkInit.APB4CLKDivider = RCC_APB4_DIV2;
  HAL_RCC_ClockConfig (&rccClkInit, FLASH_LATENCY_4);

  // PLL3_VCO In  = HSE_VALUE / PLL3M = 1 Mhz
  // PLL3_VCO Out = PLL3_VCO In * PLL3N = 100 Mhz
  // PLLLCDCLK    = PLL3_VCO Out / PLL3R = 100/4 = 25Mhz
  // LTDC clock   = PLLLCDCLK = 25Mhz
  RCC_PeriphCLKInitTypeDef rccPeriphClkInit;
  rccPeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
  rccPeriphClkInit.PLL3.PLL3M = 8;
  rccPeriphClkInit.PLL3.PLL3N = 100;
  rccPeriphClkInit.PLL3.PLL3R = 4;
  rccPeriphClkInit.PLL3.PLL3P = 2;
  rccPeriphClkInit.PLL3.PLL3Q = 7;
  HAL_RCCEx_PeriphCLKConfig (&rccPeriphClkInit);
  }
//}}}
//{{{
void sdRamConfig() {
 //{{{  pins
 //  FMC_SDCKE1  PB05
 //  FMC_SDNE1   PB06
 //
 //  FMC_A0:A5   PF00:PF05
 //  FMC_A6:A9   PF12:PF15
 //  FMC_A10:A12 PG00:PG02
 //  FMC_BA0:BA1 PG04:PG05
 //
 //  FMC_SDCLK   PG08
 //  FMC_SDNCAS  PG15
 //  FMC_SDNRAS  PF11
 //  FMC_SDNWE   PC00
 //
 //  FMC_D0:D1   PD14:PD15
 //  FMC_D2:D3   PD00:PD01
 //  FMC_D4:D12  PE07:PE15
 //  FMC_D13:D15 PD08:PD10
 //
 //  FMC_NBL0    PE00
 //  FMC_NBL1    PE01
 //}}}

  //{{{  defines
  #define REFRESH_COUNT                    ((uint32_t)0x0603)
  #define SDRAM_TIMEOUT                    ((uint32_t)0xFFFF)

  #define SDRAM_MODEREG_BURST_LENGTH_1             ((uint16_t)0x0000)
  #define SDRAM_MODEREG_BURST_LENGTH_2             ((uint16_t)0x0001)
  #define SDRAM_MODEREG_BURST_LENGTH_4             ((uint16_t)0x0002)
  #define SDRAM_MODEREG_BURST_LENGTH_8             ((uint16_t)0x0004)
  #define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL      ((uint16_t)0x0000)
  #define SDRAM_MODEREG_BURST_TYPE_INTERLEAVED     ((uint16_t)0x0008)

  #define SDRAM_MODEREG_CAS_LATENCY_2              ((uint16_t)0x0020)
  #define SDRAM_MODEREG_CAS_LATENCY_3              ((uint16_t)0x0030)

  #define SDRAM_MODEREG_OPERATING_MODE_STANDARD    ((uint16_t)0x0000)
  #define SDRAM_MODEREG_WRITEBURST_MODE_PROGRAMMED ((uint16_t)0x0000)
  #define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE     ((uint16_t)0x0200)
  //}}}

  #define kClockEnable  FMC_SDRAM_CMD_TARGET_BANK2 | FMC_SDRAM_CMD_CLK_ENABLE;
  #define kPreChargeAll FMC_SDRAM_CMD_TARGET_BANK2 | FMC_SDRAM_CMD_PALL;
  #define kAutoRefresh  FMC_SDRAM_CMD_TARGET_BANK2 | FMC_SDRAM_CMD_AUTOREFRESH_MODE | ((4-1) << 5)
  #define kLoadMode     FMC_SDRAM_CMD_TARGET_BANK2 | FMC_SDRAM_CMD_LOAD_MODE| \
                        ((SDRAM_MODEREG_WRITEBURST_MODE_SINGLE | \
                          SDRAM_MODEREG_CAS_LATENCY_2 | \
                          SDRAM_MODEREG_BURST_LENGTH_8) << 9)

  //{{{  GPIO config
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  GPIO_InitTypeDef gpio_init_structure;
  gpio_init_structure.Mode      = GPIO_MODE_AF_PP;
  gpio_init_structure.Pull      = GPIO_PULLUP;
  gpio_init_structure.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio_init_structure.Alternate = GPIO_AF12_FMC;

  gpio_init_structure.Pin   = GPIO_PIN_5 | GPIO_PIN_6;
  HAL_GPIO_Init (GPIOB, &gpio_init_structure);

  gpio_init_structure.Pin   = GPIO_PIN_0;
  HAL_GPIO_Init (GPIOC, &gpio_init_structure);

  gpio_init_structure.Pin   = GPIO_PIN_0  | GPIO_PIN_1 |
                              GPIO_PIN_8  | GPIO_PIN_9 | GPIO_PIN_10 |
                              GPIO_PIN_14 | GPIO_PIN_15;
  HAL_GPIO_Init (GPIOD, &gpio_init_structure);

  gpio_init_structure.Pin   = GPIO_PIN_0  | GPIO_PIN_1  |
                              GPIO_PIN_7  | GPIO_PIN_8  | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
                              GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
  HAL_GPIO_Init (GPIOE, &gpio_init_structure);

  gpio_init_structure.Pin   = GPIO_PIN_0 | GPIO_PIN_1  | GPIO_PIN_2  | GPIO_PIN_3  | GPIO_PIN_4 |
                              GPIO_PIN_5 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
  HAL_GPIO_Init (GPIOF, &gpio_init_structure);

  gpio_init_structure.Pin   = GPIO_PIN_0 | GPIO_PIN_1  | GPIO_PIN_2  | GPIO_PIN_4  | GPIO_PIN_5 |
                              GPIO_PIN_8 |
                              GPIO_PIN_15;
  HAL_GPIO_Init (GPIOG, &gpio_init_structure);
  //}}}
  __HAL_RCC_FMC_CLK_ENABLE();

  SDRAM_HandleTypeDef sdramHandle;
  sdramHandle.Instance = FMC_SDRAM_DEVICE;
  sdramHandle.Init.SDBank             = FMC_SDRAM_BANK2;
  sdramHandle.Init.SDClockPeriod      = FMC_SDRAM_CLOCK_PERIOD_2;
  sdramHandle.Init.ReadBurst          = FMC_SDRAM_RBURST_ENABLE;
  sdramHandle.Init.CASLatency         = FMC_SDRAM_CAS_LATENCY_2;
  sdramHandle.Init.ColumnBitsNumber   = FMC_SDRAM_COLUMN_BITS_NUM_9;  // 11
  sdramHandle.Init.RowBitsNumber      = FMC_SDRAM_ROW_BITS_NUM_12;    // 13
  sdramHandle.Init.MemoryDataWidth    = FMC_SDRAM_MEM_BUS_WIDTH_16;
  sdramHandle.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
  sdramHandle.Init.WriteProtection    = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
  sdramHandle.Init.ReadPipeDelay      = FMC_SDRAM_RPIPE_DELAY_0;

  FMC_SDRAM_TimingTypeDef timing;
  timing.LoadToActiveDelay    = 2;
  timing.ExitSelfRefreshDelay = 7;
  timing.SelfRefreshTime      = 4;
  timing.RowCycleDelay        = 7;
  timing.WriteRecoveryTime    = 2;
  timing.RPDelay              = 2;
  timing.RCDDelay             = 2;
  if (HAL_SDRAM_Init (&sdramHandle, &timing))
    printf ("HAL_SDRAM_Init fail\n");

  FMC_SDRAM_DEVICE->SDCMR = kClockEnable;
  HAL_Delay (2);
  FMC_SDRAM_DEVICE->SDCMR = kPreChargeAll;
  FMC_SDRAM_DEVICE->SDCMR = kAutoRefresh;
  FMC_SDRAM_DEVICE->SDCMR = kLoadMode;
  FMC_SDRAM_DEVICE->SDRTR = REFRESH_COUNT << 1;
  }
//}}}
//{{{
void mpuConfig() {

  // Disable the MPU
  HAL_MPU_Disable();

  // Configure MPU for sdram
  MPU_Region_InitTypeDef mpuRegion;
  mpuRegion.Enable = MPU_REGION_ENABLE;
  mpuRegion.BaseAddress = 0xD0000000;
  mpuRegion.Size = MPU_REGION_SIZE_128MB;
  mpuRegion.AccessPermission = MPU_REGION_FULL_ACCESS;
  mpuRegion.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  mpuRegion.IsCacheable = MPU_ACCESS_CACHEABLE;
  mpuRegion.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  mpuRegion.Number = MPU_REGION_NUMBER0;
  mpuRegion.TypeExtField = MPU_TEX_LEVEL0;
  mpuRegion.SubRegionDisable = 0x00;
  mpuRegion.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  HAL_MPU_ConfigRegion (&mpuRegion);

  // Enable the MPU
  HAL_MPU_Enable (MPU_PRIVILEGED_DEFAULT);
  }
//}}}

//{{{
uint32_t sdRamTest (int offset, uint16_t* addr, uint32_t len) {

  uint32_t readOk = 0;
  uint32_t readErr = 0;

  uint16_t data = offset;
  auto writeAddress = addr;
  for (uint32_t j = 0; j < len/2; j++)
    *writeAddress++ = data++;

  auto readAddress = addr;
  for (uint32_t j = 0; j < len / 2; j++) {
    uint16_t readWord1 = *readAddress++;
    if ((readWord1 & 0xFFFF) == ((j+offset) & 0xFFFF))
      readOk++;
    else {
      if (readErr < 4)
        printf ("- error %p %02x %d - r:%04x != %04x\n",
                readAddress, offset, readErr, readWord1 & 0xFFFF, (j+offset) & 0xFFFF);
      readErr++;
      }

    }

  printf ("%p i:%x ok:%x error:%x %d\n", addr, offset, readOk, readErr, (readOk * 100) / (len/2));
  return readErr;
  }
//}}}
//{{{
uint32_t simpleSdRamTest (int offset, uint16_t* addr, uint32_t len) {

  uint32_t readOk = 0;
  uint32_t readErr = 0;

  uint16_t data = offset;
  auto writeAddress = addr;
  for (uint32_t j = 0; j < len/2; j++)
    *writeAddress++ = data++;

  auto readAddress = addr;
  for (uint32_t j = 0; j < len / 2; j++) {
    uint16_t readWord1 = *readAddress++;
    if ((readWord1 & 0xFFFF) == ((j+offset) & 0xFFFF))
      readOk++;
    else
      readErr++;
    }

  return readErr;
  }
//}}}
//{{{
void simpleTest() {

  int i = 0;
  int k = 0;
  while (true) {
    for (int j = 3; j < 16; j++) {
      uint32_t errors = simpleSdRamTest (k++, (uint16_t*)(0x70000000 + (j * 0x00100000)), 0x00100000);
      if (errors == 0) {
        lcd->info (COL_YELLOW, "ram - ok " + dec (j,2));
        lcd->changed();
        }
      else  {
        float rate = (errors * 1000.f) / 0x00100000;
        lcd->info (COL_CYAN, "ram " + dec (j,2) + " fail - err:" +
                             dec(errors) + " " + dec (int(rate)/10,1) + "." + dec(int(rate) % 10,1) + "%");
        lcd->changed();
        }
      vTaskDelay (100);
      }
    }
  }
//}}}

//{{{
void findFiles (const string& dirPath, const string ext) {

  DIR dir;
  if (f_opendir (&dir, dirPath.c_str()) == FR_OK) {
    while (true) {
      FILINFO filinfo;
      if (f_readdir (&dir, &filinfo) != FR_OK || !filinfo.fname[0])
        break;
      if (filinfo.fname[0] == '.')
        continue;

      string filePath = dirPath + "/" + filinfo.fname;
      if (filinfo.fattrib & AM_DIR) {
        printf ("- findFiles dir %s\n", filePath.c_str());
        lcd->info (" - findFiles dir" + filePath);
        lcd->change();
        findFiles (filePath, ext);
        }
      else {
        auto found = filePath.find (ext);
        if (found == filePath.size() - 4) {
          printf ("findFiles %s\n", filePath.c_str());
          lcd->info ("findFiles " + filePath);
          lcd->change();
          mFileVec.push_back (filePath);
          }
        }
      }

    f_closedir (&dir);
    }
  }
//}}}
//{{{
cTile* loadJpegSw (const string& fileName, int scale) {

  FILINFO filInfo;
  if (f_stat (fileName.c_str(), &filInfo)) {
    lcd->info (COL_RED, fileName + " not found");
    return nullptr;
    }
  lcd->info ("loadFile " + fileName + " bytes:" + dec ((int)(filInfo.fsize)) + " " +
             dec (filInfo.ftime >> 11) + ":" + dec ((filInfo.ftime >> 5) & 63) + " " +
             dec (filInfo.fdate & 31) + ":" + dec ((filInfo.fdate >> 5) & 15) + ":" + dec ((filInfo.fdate >> 9) + 1980));
  lcd->changed();

  FIL gFile;
  if (f_open (&gFile, fileName.c_str(), FA_READ)) {
    lcd->info (COL_RED, fileName + " not opened");
    lcd->changed();
    return nullptr;
    }

  auto buf = (uint8_t*)malloc (filInfo.fsize);
  if (!buf)  {
    lcd->info (COL_RED, "buf fail");
    lcd->changed();
    return nullptr;
    }

  auto startTime = HAL_GetTick();
  UINT bytesRead = 0;
  f_read (&gFile, buf, (UINT)filInfo.fsize, &bytesRead);
  f_close (&gFile);
  auto loadTook = HAL_GetTick() - startTime;

  if (bytesRead > 0) {
    struct jpeg_error_mgr jerr;
    struct jpeg_decompress_struct mCinfo;
    mCinfo.err = jpeg_std_error (&jerr);
    jpeg_create_decompress (&mCinfo);

    jpeg_mem_src (&mCinfo, buf, bytesRead);
    jpeg_read_header (&mCinfo, TRUE);

    mCinfo.dct_method = JDCT_FLOAT;
    mCinfo.out_color_space = JCS_RGB;
    mCinfo.scale_num = 1;
    mCinfo.scale_denom = scale;
    jpeg_start_decompress (&mCinfo);

    auto rgb565pic = sdRamAlloc (mCinfo.output_width * mCinfo.output_height);
    auto tile = new cTile ((uint8_t*)rgb565pic, 2, mCinfo.output_width, 0,0, mCinfo.output_width, mCinfo.output_height);

    auto rgbLine = (uint8_t*)malloc (mCinfo.output_width * 3);
    while (mCinfo.output_scanline < mCinfo.output_height) {
      jpeg_read_scanlines (&mCinfo, &rgbLine, 1);
      lcd->rgb888to565 (rgbLine, rgb565pic + ((mCinfo.output_scanline-1) * mCinfo.output_width), mCinfo.output_width);
      }
    free (rgbLine);

    jpeg_finish_decompress (&mCinfo);
    auto allTook = HAL_GetTick() - startTime;

    free (buf);
    lcd->info (COL_WHITE, "done " + dec(mCinfo.image_width) + "x" + dec(mCinfo.image_height) + " " +
                                    dec(mCinfo.output_width) + "x" + dec(mCinfo.output_height) + " " +
                                    dec(loadTook) + ":" + dec(allTook));
    lcd->changed();
    jpeg_destroy_decompress (&mCinfo);
    return tile;
    }
  else {
    free (buf);
    lcd->info (COL_RED, "loadFile read failed");
    lcd->changed();
    return nullptr;
    }
  }
//}}}
//{{{
cTile* loadJpegHw (const string& fileName) {

  JPEG_Handle.Instance = JPEG;
  HAL_JPEG_Init (&JPEG_Handle);

  auto startTime = HAL_GetTick();
  FIL JPEG_File;
  if (f_open (&JPEG_File, fileName.c_str(), FA_READ) == FR_OK) {
    JPEG_Decode_DMA (&JPEG_Handle, &JPEG_File, jpegYuvBuf);

    int count = 0;
    bool jpegProcessingDone = false;
    do {
      count++;
      jpegProcessingDone = jpegInputHandler (&JPEG_Handle);
      } while (jpegProcessingDone == 0);
    f_close (&JPEG_File);

    JPEG_ConfTypeDef jpegInfo;
    HAL_JPEG_GetInfo (&JPEG_Handle, &jpegInfo);
    lcd->info (COL_YELLOW,
               "loadJpeg " + fileName +
               " took " + dec (HAL_GetTick() - startTime) + ":" + dec(count) +
               " chroma:" + dec (jpegInfo.ChromaSubsampling) +
               " " + dec (jpegInfo.ImageWidth) + "x" + dec (jpegInfo.ImageHeight));
    lcd->changed();

    printf ("jpeg image %d %d\n",  jpegInfo.ImageWidth, jpegInfo.ImageHeight);

    auto rgb565pic = sdRamAlloc (jpegInfo.ImageWidth * jpegInfo.ImageHeight * 2);
    auto tile = new cTile ((uint8_t*)rgb565pic, 2, jpegInfo.ImageWidth, 0,0,
                           jpegInfo.ImageWidth, jpegInfo.ImageHeight);
    lcd->yuvTo565 ((uint32_t*)jpegYuvBuf, (uint32_t*)rgb565pic,
                   jpegInfo.ImageWidth, jpegInfo.ImageHeight, jpegInfo.ChromaSubsampling);
    return tile;
    }
  else
    return nullptr;
  }
//}}}

//{{{
void uiThread (void* arg) {

  while (true) {
    if (lcd->changed()) {
      lcd->start();
      lcd->clear (COL_BLACK);

      int count = 1;
      for (auto tile : mTileVec) {
        lcd->copy (tile, cPoint (count* 50, count * 50));
        count++;
        }

      lcd->drawInfo();
      lcd->present();
      }
    else
      vTaskDelay (40);
    }
  }
//}}}
//{{{
void appThread (void* arg) {

  FATFS SDFatFs;
  char SDPath[4];

  if (FATFS_LinkDriver (&SD_Driver, SDPath) != 0) {
    lcd->info (COL_RED, "sdCard - no driver");
    lcd->changed();
    }
  else if (f_mount (&SDFatFs, (TCHAR const*)SDPath, 1) != FR_OK) {
    printf ("sdCard - not mounted\n");
    lcd->info (COL_RED, "sdCard - not mounted");
    lcd->changed();
    }
  else {
    char label[20] = {0};
    DWORD volumeSerialNumber = 0;
    f_getlabel ("", label, &volumeSerialNumber);
    lcd->info ("sdCard mounted label:" + string (label));
    lcd->changed();

    findFiles ("", ".jpg");

    jpegYuvBuf = (uint32_t)sdRamAlloc (LCD_WIDTH*LCD_HEIGHT*4);

    auto startTime = HAL_GetTick();
    //for (auto file : mFileVec) {
    auto file = mFileVec.front();
      {
      //auto tile = loadFileSw (file, 1);
      auto tile = loadJpegHw (file);
      if (tile)
        mTileVec.push_back (tile);
      else
        lcd->info ("tile error " + file);
      lcd->changed();
      }
    lcd->info (COL_WHITE, "appThread - loadFiles took " + dec(HAL_GetTick() - startTime));
    }

  while (true) {
    for (int i = 30; i < 60; i++) { lcd->display (i); vTaskDelay (20); }
    for (int i = 60; i > 30; i--) { lcd->display (i); vTaskDelay (20); }
    }
  }
//}}}

//{{{
int main() {

  HAL_Init();
  clockConfig();
  sdRamConfig();
  //HAL_SetFMCMemorySwappingConfig (FMC_SWAPBMAP_SDRAM_SRAM);
  SCB_EnableICache();
  //SCB_EnableDCache();
  //mpuConfig();

  BSP_LED_Init (LED_GREEN);
  BSP_LED_Init (LED_BLUE);
  BSP_LED_Init (LED_RED);
  BSP_PB_Init (BUTTON_KEY, BUTTON_MODE_EXTI);

  vPortDefineHeapRegions (kHeapRegions);
  lcd = new cLcd (sdRamAlloc (LCD_WIDTH*LCD_HEIGHT), sdRamAlloc (LCD_WIDTH*LCD_HEIGHT));
  lcd->init (kHello);

  TaskHandle_t uiHandle;
  xTaskCreate ((TaskFunction_t)uiThread, "ui", 1024, 0, 3, &uiHandle);

  TaskHandle_t appHandle;
  xTaskCreate ((TaskFunction_t)appThread, "app", 8192, 0, 4, &appHandle);

  vTaskStartScheduler();

  return 0;
  }
//}}}
