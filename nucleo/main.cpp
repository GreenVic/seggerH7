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
#define SDRAM_DEVICE_SIZE 0x01000000

const HeapRegion_t kHeapRegions[] = {
  {(uint8_t*)(0xD0F00000), 0x00100000 },
  { nullptr, 0 } };
//}}}
#define RAM_TEST

cLcd* lcd = nullptr;
uint8_t* mSdRamAlloc = (uint8_t*)SDRAM_DEVICE_ADDR;
vector<string> mFileVec;
vector<cTile*> mTileVec;
//{{{  jpeg vars
JPEG_HandleTypeDef jpegHandle;

//{{{  struct tJpegBufs
typedef struct {
  bool mFull;
  uint8_t* mBuf;
  uint32_t mSize;
  } tJpegBufs;
//}}}
uint8_t jpegBuf0 [4096];
uint8_t jpegBuf1 [4096];
tJpegBufs jpegBufs [2] = {
  { false, jpegBuf0, 0 },
  { false, jpegBuf1, 0 }
  };
__IO uint32_t readIndex = 0;
__IO uint32_t writeIndex = 0;
__IO bool jpegInPaused = false;
__IO bool jpegDecodeDone = false;

const uint32_t kJpegYuvChunkSize = 0x10000;
//}}}

extern "C" { void JPEG_IRQHandler() { HAL_JPEG_IRQHandler (&jpegHandle); }  }
extern "C" { void MDMA_IRQHandler() { HAL_MDMA_IRQHandler (jpegHandle.hdmain); HAL_MDMA_IRQHandler (jpegHandle.hdmaout); }  }
extern "C" { void EXTI15_10_IRQHandler() { HAL_GPIO_EXTI_IRQHandler (USER_BUTTON_PIN); } }
//{{{
void HAL_GPIO_EXTI_Callback (uint16_t GPIO_Pin) {
  if (GPIO_Pin == USER_BUTTON_PIN)
    lcd->toggle();
  }
//}}}

//{{{
class cRtc {
public:
  //{{{
  void init() {

    // Configue LSE as RTC clock source
    RCC_OscInitTypeDef rccOscInitStruct = {0};
    rccOscInitStruct.OscillatorType =  RCC_OSCILLATORTYPE_LSI | RCC_OSCILLATORTYPE_LSE;
    rccOscInitStruct.PLL.PLLState = RCC_PLL_NONE;
    rccOscInitStruct.LSEState = RCC_LSE_ON;
    rccOscInitStruct.LSIState = RCC_LSI_OFF;
    if (HAL_RCC_OscConfig (&rccOscInitStruct))
      printf ("HAL_RCC_OscConfig failed\n");

    RCC_PeriphCLKInitTypeDef periphClkInitStruct = {0};
    periphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    periphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig (&periphClkInitStruct))
      printf ("HAL_RCCEx_PeriphCLKConfig failed\n");
    __HAL_RCC_RTC_ENABLE();

    // Configure LSE RTC prescaler and RTC data registers
    writeProtectDisable();
    if (enterInitMode()) {
      //{{{  init rtc
      RTC->CR = RTC_HOURFORMAT_24;
      RTC->PRER = (uint32_t)(0x00FF);
      RTC->PRER |= (uint32_t)(0x7F << 16U);

      // Exit Initialization mode
      RTC->ISR &= (uint32_t)~RTC_ISR_INIT;

      // If CR_BYPSHAD bit = 0, wait for synchro else this check is not needed
      if ((RTC->CR & RTC_CR_BYPSHAD) == RESET)
        if (!waitForSynchro())
          printf ("timeout waiting for synchro\n");

      //RTC->TAFCR &= (uint32_t)~RTC_TAFCR_ALARMOUTTYPE;
      //RTC->TAFCR |= (uint32_t)RTC_OUTPUT_TYPE_OPENDRAIN;
      }
      //}}}
    writeProtectEnable();

    loadDateTime();
    uint32_t clockDateTimeValue = mDateTime.getValue();

    cDateTime buildDateTime (mBuildDate, mBuildTime);
    uint32_t buildDateTimeValue = buildDateTime.getValue();
    if (clockDateTimeValue < buildDateTimeValue + kBuildSecs) {
      // set clockDateTime from buildDateTime
      mDateTime.setFromValue (buildDateTimeValue + kBuildSecs);
      printf ("cRtc::init set clock < build %d < %d\n", clockDateTimeValue, buildDateTimeValue);
      saveDateTime();
      mClockSet = true;
      }
    }
  //}}}

  //{{{
  bool getClockSet() {
    return mClockSet;
    }
  //}}}
  //{{{
  void getClockAngles (float& hours, float& minutes, float& seconds, float& subSeconds) {

    loadDateTime();

    hours = (1.f - ((mDateTime.Hours + (mDateTime.Minutes / 60.f)) / 6.f)) * kPi;
    minutes = (1.f - ((mDateTime.Minutes + (mDateTime.Seconds / 60.f))/ 30.f)) * kPi;
    seconds =  (1.f - (mDateTime.Seconds / 30.f)) * kPi;
    subSeconds =  (1.f - ((255 - mDateTime.SubSeconds) / 128.f)) * kPi;
    }
  //}}}
  //{{{
  std::string getClockTimeString() {

    return mDateTime.getTimeString();
    }
  //}}}
  //{{{
  std::string getClockTimeDateString() {

    return mDateTime.getTimeDateString();
    }
  //}}}
  //{{{
  std::string getBuildTimeDateString() {

    return mBuildTime + " "  + mBuildDate;
    }
  //}}}

private:
  //{{{
  uint8_t getBcdFromByte (uint8_t byte) {
    return ((byte / 10) << 4) | (byte % 10);
    }
  //}}}
  //{{{
  uint8_t getByteFromBcd (uint8_t bcd) {
    return (((bcd & 0xF0) >> 4) * 10) + (bcd & 0x0F);
    }
  //}}}

  //{{{
  void loadDateTime() {

    mDateTime.SubSeconds = RTC->SSR;
    mDateTime.SecondFraction = RTC->PRER & RTC_PRER_PREDIV_S;

    uint32_t tr = RTC->TR;
    mDateTime.TimeFormat = (tr & RTC_TR_PM) >> 16U;
    mDateTime.Hours = getByteFromBcd ((tr & (RTC_TR_HT | RTC_TR_HU)) >> 16U);
    mDateTime.Minutes = getByteFromBcd ((tr & (RTC_TR_MNT | RTC_TR_MNU)) >> 8U);
    mDateTime.Seconds = getByteFromBcd (tr & (RTC_TR_ST | RTC_TR_SU));

    uint32_t dr = RTC->DR;
    mDateTime.Year = getByteFromBcd ((dr & (RTC_DR_YT | RTC_DR_YU)) >> 16U);
    mDateTime.WeekDay = (dr & RTC_DR_WDU) >> 13U;
    mDateTime.Month = getByteFromBcd ((dr & (RTC_DR_MT | RTC_DR_MU)) >> 8U);
    mDateTime.Date = getByteFromBcd (dr & (RTC_DR_DT | RTC_DR_DU));
    }
  //}}}
  //{{{
  void saveDateTime() {

    writeProtectDisable();
    if (enterInitMode()) {
      if ((RTC->CR & RTC_CR_FMT) == (uint32_t)RESET)
        mDateTime.TimeFormat = 0x00U;

      if ((mDateTime.Month & 0x10U) == 0x10U)
        mDateTime.Month = (uint8_t)((mDateTime.Month & (uint8_t)~(0x10U)) + (uint8_t)0x0AU);

      // Set the RTC_DR register
      uint32_t tmp = (getBcdFromByte (mDateTime.Year) << 16) |
                     (getBcdFromByte (mDateTime.Month) << 8) |
                      getBcdFromByte (mDateTime.Date) |
                     (mDateTime.WeekDay << 13);
      RTC->DR = (uint32_t)(tmp & RTC_DR_RESERVED_MASK);

      // Set the RTC_TR register
      tmp = ((getBcdFromByte (mDateTime.Hours) << 16) |
             (getBcdFromByte (mDateTime.Minutes) << 8) |
              getBcdFromByte (mDateTime.Seconds) |
            (mDateTime.TimeFormat) << 16);
      RTC->TR = (uint32_t)(tmp & RTC_TR_RESERVED_MASK);

      // Clear the bits to be configured
      RTC->CR &= (uint32_t)~RTC_CR_BCK;

      // Configure the RTC_CR register
      RTC->CR |= mDateTime.DayLightSaving | mDateTime.StoreOperation;

      // Exit Initialization mode
      RTC->ISR &= (uint32_t)~RTC_ISR_INIT;

      if ((RTC->CR & RTC_CR_BYPSHAD) == RESET)
        if (!waitForSynchro())
          printf ("setDateTime - timeout waiting for synchro\n");
      }

    writeProtectEnable();
    }
  //}}}

  //{{{
  void writeProtectDisable() {
    RTC->WPR = 0xCAU;
    RTC->WPR = 0x53U;
    }
  //}}}
  //{{{
  bool enterInitMode() {

    // Check if the Initialization mode is set
    if ((RTC->ISR & RTC_ISR_INITF) == (uint32_t)RESET) {
      // Set the Initialization mode
      RTC->ISR = (uint32_t)RTC_INIT_MASK;

      /* Get tick */
      uint32_t tickstart = HAL_GetTick();

      // Wait till RTC is in INIT state and if Time out is reached exit
      while ((RTC->ISR & RTC_ISR_INITF) == (uint32_t)RESET)
        if ((HAL_GetTick() - tickstart ) > RTC_TIMEOUT_VALUE)
          return false;
      }

    return true;
    }
  //}}}
  //{{{
  bool waitForSynchro() {

    // Clear RSF flag
    RTC->ISR &= (uint32_t)RTC_RSF_MASK;

    uint32_t tickstart = HAL_GetTick();

    // Wait the registers to be synchronised
    while ((RTC->ISR & RTC_ISR_RSF) == (uint32_t)RESET)
      if ((HAL_GetTick() - tickstart ) > RTC_TIMEOUT_VALUE)
        return false;

    return true;
    }
  //}}}
  //{{{
  void writeProtectEnable() {
    RTC->WPR = 0xFFU;
    }
  //}}}

  const float kPi = 3.1415926f;
  const int kBuildSecs = 11;
  const std::string mBuildTime = __TIME__;
  const std::string mBuildDate = __DATE__;

  //{{{
  class cDateTime {
  public:
    cDateTime() {}
    //{{{
    cDateTime (const std::string& buildDateStr, const std::string& buildTimeStr) {

      // buildDateStr - dd:mmm:yyyy
      Date = ((buildDateStr[4] == ' ') ? 0 : buildDateStr[4] - 0x30) * 10 + (buildDateStr[5] -0x30);
      Year = (buildDateStr[9] - 0x30) * 10 + (buildDateStr[10] -0x30);

      Month = 0;
      for (int i = 0; i < 12; i++)
        if ((buildDateStr[0] == *kMonth[i]) && (buildDateStr[1] == *(kMonth[i]+1)) && (buildDateStr[2] == *(kMonth[i]+2))) {
          Month = i;
          break;
          }

      // buildTimeStr - hh:mm:ss
      Hours = (buildTimeStr[0]  - 0x30) * 10 + (buildTimeStr[1] -0x30);
      Minutes = (buildTimeStr[3] - 0x30) * 10 + (buildTimeStr[4] -0x30);
      Seconds = (buildTimeStr[6] - 0x30) * 10 + (buildTimeStr[7] -0x30);
      }
    //}}}

    //{{{
    uint32_t getValue() {
      return ((((Year*12 + Month)*31 + Date)*24 + Hours)*60 + Minutes)*60 + Seconds;
      }
    //}}}
    //{{{
    std::string getTimeString() {
      return dec(Hours,2) + ":" + dec(Minutes,2) + ":" + dec(Seconds,2);
             //dec(SubSeconds) + " " + dec(SecondFraction);
      }
    //}}}
    //{{{
    std::string getDateString() {
      return std::string(kMonth[Month]) + " " + dec(Date,2) + " " + dec(2000 + Year,4);
      }
    //}}}
    //{{{
    std::string getTimeDateString() {
      return dec(Hours,2) + ":" + dec(Minutes,2) + ":" + dec(Seconds,2) + " " +
             kMonth[Month] + " " + dec(Date,2) + " " + dec(2000 + Year,4);
             //dec(SubSeconds) + " " + dec(SecondFraction);
      }
    //}}}

    //{{{
    void setFromValue (uint32_t value) {
      TimeFormat = RTC_HOURFORMAT12_AM;
      DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
      StoreOperation = RTC_STOREOPERATION_RESET;

      Seconds = value % 60;
      value /= 60;
      Minutes = value % 60;
      value /= 60;
      Hours = value % 24;
      value /= 24;
      Date = value % 31;
      value /= 31;
      Month = value % 12;
      value /= 12;
      Year = value;

      WeekDay = RTC_WEEKDAY_FRIDAY;  // wrong
      }
    //}}}

    uint8_t Year;
    uint8_t Month;
    uint8_t WeekDay;
    uint8_t Date;
    uint8_t Hours;
    uint8_t Minutes;
    uint8_t Seconds;
    uint8_t TimeFormat;
    uint32_t SubSeconds;
    uint32_t SecondFraction;
    uint32_t DayLightSaving;
    uint32_t StoreOperation;

  private:
    const char* kMonth[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    };
  //}}}
  cDateTime mDateTime;

  RTC_HandleTypeDef mRtcHandle;
  bool mClockSet = false;
  };
//}}}
cRtc mRtc;
uint8_t* jpegYuvBuf = nullptr;


//{{{
uint8_t* sdRamAlloc (uint32_t bytes) {
  auto alloc = mSdRamAlloc;
  mSdRamAlloc += bytes;
  return alloc;
  }
//}}}

//{{{
void HAL_JPEG_MspInit (JPEG_HandleTypeDef* jpegHandlePtr) {

  static MDMA_HandleTypeDef hmdmaIn;
  static MDMA_HandleTypeDef hmdmaOut;

  __HAL_RCC_JPGDECEN_CLK_ENABLE();
  __HAL_RCC_MDMA_CLK_ENABLE();

  // Input MDMA
  hmdmaIn.Init.Priority       = MDMA_PRIORITY_HIGH;
  hmdmaIn.Init.Endianness     = MDMA_LITTLE_ENDIANNESS_PRESERVE;
  hmdmaIn.Init.SourceInc      = MDMA_SRC_INC_BYTE;
  hmdmaIn.Init.DestinationInc = MDMA_DEST_INC_DISABLE;
  hmdmaIn.Init.SourceDataSize = MDMA_SRC_DATASIZE_BYTE;
  hmdmaIn.Init.DestDataSize   = MDMA_DEST_DATASIZE_WORD;
  hmdmaIn.Init.DataAlignment  = MDMA_DATAALIGN_PACKENABLE;
  hmdmaIn.Init.SourceBurst    = MDMA_SOURCE_BURST_32BEATS;
  hmdmaIn.Init.DestBurst      = MDMA_DEST_BURST_16BEATS;
  hmdmaIn.Init.SourceBlockAddressOffset = 0;
  hmdmaIn.Init.DestBlockAddressOffset = 0;

  // use JPEG Input FIFO Threshold as a trigger for the MDMA
  // Set the MDMA HW trigger to JPEG Input FIFO Threshold flag
  // Set MDMA buffer size to JPEG FIFO threshold size 32bytes 8words
  hmdmaIn.Init.Request = MDMA_REQUEST_JPEG_INFIFO_TH;
  hmdmaIn.Init.TransferTriggerMode = MDMA_BUFFER_TRANSFER;
  hmdmaIn.Init.BufferTransferLength = 32;
  hmdmaIn.Instance = MDMA_Channel7;
  __HAL_LINKDMA (jpegHandlePtr, hdmain, hmdmaIn);
  HAL_MDMA_DeInit (&hmdmaIn);
  HAL_MDMA_Init (&hmdmaIn);

  // output MDMA
  hmdmaOut.Init.Priority       = MDMA_PRIORITY_VERY_HIGH;
  hmdmaOut.Init.Endianness     = MDMA_LITTLE_ENDIANNESS_PRESERVE;
  hmdmaOut.Init.SourceInc      = MDMA_SRC_INC_DISABLE;
  hmdmaOut.Init.DestinationInc = MDMA_DEST_INC_BYTE;
  hmdmaOut.Init.SourceDataSize = MDMA_SRC_DATASIZE_WORD;
  hmdmaOut.Init.DestDataSize   = MDMA_DEST_DATASIZE_BYTE;
  hmdmaOut.Init.DataAlignment  = MDMA_DATAALIGN_PACKENABLE;
  hmdmaOut.Init.SourceBurst    = MDMA_SOURCE_BURST_32BEATS;
  hmdmaOut.Init.DestBurst      = MDMA_DEST_BURST_32BEATS;
  hmdmaOut.Init.SourceBlockAddressOffset = 0;
  hmdmaOut.Init.DestBlockAddressOffset = 0;

  // use JPEG Output FIFO Threshold as a trigger for the MDMA
  // Set the MDMA HW trigger to JPEG Output FIFO Threshold flag
  // Set MDMA buffer size to JPEG FIFO threshold size 32bytes 8words
  hmdmaOut.Init.Request = MDMA_REQUEST_JPEG_OUTFIFO_TH;
  hmdmaOut.Init.TransferTriggerMode = MDMA_BUFFER_TRANSFER;
  hmdmaOut.Init.BufferTransferLength = 32;
  hmdmaOut.Instance = MDMA_Channel6;
  HAL_MDMA_DeInit (&hmdmaOut);
  HAL_MDMA_Init (&hmdmaOut);
  __HAL_LINKDMA (jpegHandlePtr, hdmaout, hmdmaOut);

  HAL_NVIC_SetPriority (MDMA_IRQn, 0x08, 0x0F);
  HAL_NVIC_EnableIRQ (MDMA_IRQn);

  HAL_NVIC_SetPriority (JPEG_IRQn, 0x07, 0x0F);
  HAL_NVIC_EnableIRQ (JPEG_IRQn);
  }
//}}}
//{{{
void HAL_JPEG_GetDataCallback (JPEG_HandleTypeDef* jpegHandlePtr, uint32_t len) {

  //printf ("getData %d\n", len);

  if (len != jpegBufs[readIndex].mSize)
    HAL_JPEG_ConfigInputBuffer (jpegHandlePtr, jpegBufs[readIndex].mBuf+len, jpegBufs[readIndex].mSize-len);
  else {
    jpegBufs [readIndex].mFull = false;
    jpegBufs [readIndex].mSize = 0;

    readIndex = readIndex ? 0 : 1;
    if (jpegBufs [readIndex].mFull)
      HAL_JPEG_ConfigInputBuffer (jpegHandlePtr, jpegBufs[readIndex].mBuf, jpegBufs[readIndex].mSize);
    else {
      HAL_JPEG_Pause (jpegHandlePtr, JPEG_PAUSE_RESUME_INPUT);
      jpegInPaused = true;
      }
    }
  }
//}}}
//{{{
void HAL_JPEG_DataReadyCallback (JPEG_HandleTypeDef* jpegHandlePtr, uint8_t* data, uint32_t len) {

  //printf ("dataReady %x %d\n", data, len);

  HAL_JPEG_ConfigOutputBuffer (jpegHandlePtr, data+len, kJpegYuvChunkSize);
  //lcd->info (COL_GREEN, "HAL_JPEG_DataReadyCallback " + hex(uint32_t(data)) + ":" + hex(len));
  //lcd->changed();
  }
//}}}
//{{{
void HAL_JPEG_DecodeCpltCallback (JPEG_HandleTypeDef* jpegHandlePtr) {

  //printf ("decodeCplt\n");

  jpegDecodeDone = true;
  }
//}}}
//{{{
void HAL_JPEG_InfoReadyCallback (JPEG_HandleTypeDef* jpegHandlePtr, JPEG_ConfTypeDef* info) {

  //printf ("infoReady %d %dx%d\n", info->ChromaSubsampling, info->ImageWidth, info->ImageHeight);

  lcd->info (COL_YELLOW, "infoReady " + dec (info->ChromaSubsampling, 1, '0') +  ":" +
                                        dec (info->ImageWidth) + "x" + dec (info->ImageHeight));
  lcd->changed();
  }
//}}}
//{{{
void HAL_JPEG_ErrorCallback (JPEG_HandleTypeDef* jpegHandlePtr) {

  //printf ("jpegError\n");

  lcd->info (COL_RED, "jpegError");
  lcd->changed();
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
 //  FMC_SDCKE1  PB5
 //  FMC_SDNE1   PB6

 //  FMC_A0:A5   PF0:5
 //  FMC_A6:A9   PF12:15
 //  FMC_A10:A12 PG0:2
 //  FMC_BA0:BA1 PG4:5

 //  FMC_SDCLK   PG8
 //  FMC_SDNCAS  PG15
 //  FMC_SDNRAS  PF11
 //  FMC_SDNWE   PC0

 //  FMC_NBL0    PE0
 //  FMC_NBL1    PE1

 //  FMC_D0:D1   PD14:15
 //  FMC_D2:D3   PD0:1
 //  FMC_D4:D12  PE7:15
 //  FMC_D13:D15 PD8:10
 //}}}

  //{{{  defines
  #define SDRAM_MODEREG_BURST_LENGTH_1             ((uint16_t)0x0000)
  #define SDRAM_MODEREG_BURST_LENGTH_2             ((uint16_t)0x0001)
  #define SDRAM_MODEREG_BURST_LENGTH_4             ((uint16_t)0x0002)
  #define SDRAM_MODEREG_BURST_LENGTH_8             ((uint16_t)0x0004)
  #define SDRAM_MODEREG_CAS_LATENCY_2              ((uint16_t)0x0020)
  #define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE     ((uint16_t)0x0200)

  #define REFRESH_COUNT                            ((uint32_t)0x0603)
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
                              GPIO_PIN_15;
  HAL_GPIO_Init (GPIOG, &gpio_init_structure);

  gpio_init_structure.Pin   = GPIO_PIN_8;
  gpio_init_structure.Pull  = GPIO_NOPULL;
  gpio_init_structure.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
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
  HAL_Delay (1);
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
  mpuRegion.Size = MPU_REGION_SIZE_16MB;
  mpuRegion.AccessPermission = MPU_REGION_FULL_ACCESS;
  mpuRegion.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  mpuRegion.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
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
void sdRamTest (uint16_t offset, uint16_t* addr, uint32_t len) {

  uint16_t data = offset;
  auto writeAddress = addr;
  for (uint32_t j = 0; j < len/2; j++)
    *writeAddress++ = data++;

  uint32_t readOk = 0;
  uint32_t readErr = 0;
  uint32_t bitErr[16] = {0};
  auto readAddress = addr;
  for (uint32_t j = 0; j < len / 2; j++) {
    uint16_t readWord1 = *readAddress++;
    if ((readWord1 & 0xFFFF) == ((j+offset) & 0xFFFF))
      readOk++;
    else {
      readErr++;
      uint32_t bit = 1;
      for (int i = 0; i < 16; i++) {
        if ((readWord1 & bit) != ((j+offset) & bit))
          bitErr[i] += 1;
        bit *= 2;
        }
      }
    }

  if (readErr != 0) {
    //lcd->info (COL_YELLOW, "sdRam ok " + hex((uint32_t)addr));
    string str = "errors ";
    for (int i = 15; i >= 0; i--)
      if (bitErr[i])
        str += " " + dec (bitErr[i], 4,' ');
      else
        str += " ____";
    float rate = (readErr * 1000.f) / 0x00100000;
    str += "  " + dec(readErr) + " " + dec (int(rate)/10,1) + "." + dec(int(rate) % 10,1) + "%";
    lcd->info (COL_CYAN, str);
    lcd->changed();
    }

  vTaskDelay (200);
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
  lcd->info (COL_YELLOW, "loadFile " + fileName + " bytes:" + dec ((int)(filInfo.fsize)) + " " +
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
    lcd->info (COL_RED, "loadJpegSw buf fail");
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

    auto rgb565pic = (uint16_t*)sdRamAlloc (mCinfo.output_width * mCinfo.output_height*2);
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
    lcd->info (COL_YELLOW, dec(mCinfo.image_width) + "x" + dec(mCinfo.image_height) + " " +
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

  auto startTime = HAL_GetTick();

  FIL jpegFile;
  if (f_open (&jpegFile, fileName.c_str(), FA_READ) == FR_OK) {
    readIndex = 0;
    writeIndex = 0;
    jpegInPaused = 0;
    jpegDecodeDone = false;

    jpegHandle.Instance = JPEG;
    HAL_JPEG_Init (&jpegHandle);

    if (f_read (&jpegFile, jpegBufs[0].mBuf, 4096, &jpegBufs[0].mSize) == FR_OK)
      jpegBufs[0].mFull = true;
    if (f_read (&jpegFile, jpegBufs[1].mBuf, 4096, &jpegBufs[1].mSize) == FR_OK)
      jpegBufs[1].mFull = true;

    if (!jpegYuvBuf)
      jpegYuvBuf = (uint8_t*)sdRamAlloc (400*272*3);

    HAL_JPEG_Decode_DMA (&jpegHandle, jpegBufs[0].mBuf, jpegBufs[0].mSize, jpegYuvBuf, kJpegYuvChunkSize);

    while (!jpegDecodeDone) {
      if (!jpegBufs[writeIndex].mFull) {
        if (f_read (&jpegFile, jpegBufs[writeIndex].mBuf, 4096, &jpegBufs[writeIndex].mSize) == FR_OK)
          jpegBufs[writeIndex].mFull = true;
        if (jpegInPaused && (writeIndex == readIndex)) {
          jpegInPaused = false;
          HAL_JPEG_ConfigInputBuffer (&jpegHandle, jpegBufs[readIndex].mBuf, jpegBufs[readIndex].mSize);
          HAL_JPEG_Resume (&jpegHandle, JPEG_PAUSE_RESUME_INPUT);
          }
        writeIndex = writeIndex ? 0 : 1;
        }
      else
        vTaskDelay(1);
      }
    f_close (&jpegFile);

    JPEG_ConfTypeDef info;
    HAL_JPEG_GetInfo (&jpegHandle, &info);
    lcd->info (COL_YELLOW, "loadJpeg " + fileName +
                           " took " + dec (HAL_GetTick() - startTime) + " " +
                           dec (info.ChromaSubsampling, 1, '0') +  ":" +
                           dec (info.ImageWidth) + "x" + dec (info.ImageHeight));
    lcd->changed();

    printf ("loadJpegHw image %dx%d\n", info.ImageWidth, info.ImageHeight);

    auto rgb565pic = (uint16_t*)sdRamAlloc (info.ImageWidth * info.ImageHeight * 2);
    lcd->jpegYuvTo565 (jpegYuvBuf, rgb565pic, info.ImageWidth, info.ImageHeight, info.ChromaSubsampling);

    return new cTile ((uint8_t*)rgb565pic, 2, info.ImageWidth, 0,0, info.ImageWidth, info.ImageHeight);
    }
  else {
    printf ("loadJpegHw fail\n");
    return nullptr;
    }
  }
//}}}

//{{{
void uiThread (void* arg) {

  lcd->display (80);

  int tick = 0;
  int count = 0;
  while (true) {
    if (lcd->changed() || (count == 500)) {
      tick++;
      count = 100;
      lcd->start();
      lcd->clear (COL_BLACK);

      int item = 0;
      int rows = sqrt ((float)mTileVec.size()) + 1;
      //printf ("uiThread %d %d %d\n", tick, mTileVec.size(), rows);

      for (auto tile : mTileVec) {
        if (true || ((tile->mWidth <= (1024/rows)) && (tile->mHeight <= (600/rows))))
          lcd->copy (tile, cPoint ((item % rows) * (1024/rows), (item /rows) * (600/rows)));
        else
          lcd->size (tile, cRect ((item % rows) * (1024/rows), (item /rows) * (600/rows),
                                  ((item % rows)+1) * (1024/rows), ((item /rows)+1) * (600/rows)));
        item++;
        }

      float hourAngle;
      float minuteAngle;
      float secondAngle;
      float subSecondAngle;
      mRtc.getClockAngles (hourAngle, minuteAngle, secondAngle, subSecondAngle);

      int radius = 60;
      cPoint centre = cPoint (950, 490);
      lcd->ellipse (COL_WHITE, centre, cPoint(radius, radius));
      lcd->ellipse (COL_BLACK, centre, cPoint(radius-2, radius-2));
      float hourRadius = radius * 0.7f;
      lcd->line (COL_WHITE, centre, centre + cPoint (int16_t(hourRadius * sin (hourAngle)), int16_t(hourRadius * cos (hourAngle))));
      float minuteRadius = radius * 0.8f;
      lcd->line (COL_WHITE, centre, centre + cPoint (int16_t(minuteRadius * sin (minuteAngle)), int16_t(minuteRadius * cos (minuteAngle))));
      float secondRadius = radius * 0.9f;
      lcd->line (COL_RED, centre, centre + cPoint (int16_t(secondRadius * sin (secondAngle)), int16_t(secondRadius * cos (secondAngle))));

      lcd->cLcd::text (COL_WHITE, 45, mRtc.getClockTimeDateString(), cRect (550,545, 1024,600));

      lcd->drawInfo();
      lcd->present();
      }
    else {
      count++;
      vTaskDelay (1);
      }
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

    auto startTime = HAL_GetTick();
    for (auto file : mFileVec) {
    //auto file = mFileVec.front(); {
      auto tile = loadJpegHw (file);
      //auto tile = loadJpegSw (file, 1);
      if (tile)
        mTileVec.push_back (tile);
      else
        lcd->info ("tile error " + file);
      lcd->changed();
      taskYIELD();
      }
    lcd->info (COL_WHITE, "appThread - loadFiles took " + dec(HAL_GetTick() - startTime));
    }

  #ifdef RAM_TEST
    uint32_t k = 0;
    while (true)
      for (int j = 8; j < 15; j++) {
        k += HAL_GetTick();
        sdRamTest (uint16_t(k++), (uint16_t*)(SDRAM_DEVICE_ADDR + (j * 0x00100000)), 0x00100000);
        }
   #endif

  while (true) {
    //for (int i = 30; i < 100; i++) { lcd->display (i); vTaskDelay (20); }
    //for (int i = 100; i > 30; i--) { lcd->display (i); vTaskDelay (20); }
    vTaskDelay (1000);
    }
  }
//}}}

//{{{
int main() {

  HAL_Init();
  clockConfig();
  sdRamConfig();

  // something still using rtos heap
  vPortDefineHeapRegions (kHeapRegions);

  //HAL_SetFMCMemorySwappingConfig (FMC_SWAPBMAP_SDRAM_SRAM);
  //mpuConfig();
  SCB_EnableICache();
  //SCB_EnableDCache();

  BSP_LED_Init (LED_GREEN);
  BSP_LED_Init (LED_BLUE);
  BSP_LED_Init (LED_RED);
  BSP_PB_Init (BUTTON_KEY, BUTTON_MODE_EXTI);

  mRtc.init();

  lcd = new cLcd ((uint16_t*)sdRamAlloc (LCD_WIDTH*LCD_HEIGHT*2),
                  (uint16_t*)sdRamAlloc (LCD_WIDTH*LCD_HEIGHT*2));
  lcd->init (kHello);

  TaskHandle_t uiHandle;
  xTaskCreate ((TaskFunction_t)uiThread, "ui", 1024, 0, 4, &uiHandle);

  TaskHandle_t appHandle;
  xTaskCreate ((TaskFunction_t)appThread, "app", 8192, 0, 4, &appHandle);

  vTaskStartScheduler();

  return 0;
  }
//}}}
