//// main.cpp
//{{{  includes
#include <algorithm>
#include <string>
//#include <stdio.h>
#include <ctype.h>


#include "cmsis_os.h"
#include "stm32h7xx_nucleo_144.h"
#include "heap.h"

#include "cRtc.h"
#include "cLcd.h"
#include "sd.h"
#include "jpeg.h"

#include "../fatFs/ff.h"

using namespace std;
//}}}

#define SW_JPEG
#define SW_SCALE 4

const string kHello = "stm32h7 testbed " + string(__TIME__) + " " + string(__DATE__);

// vars
FATFS fatFs;
cRtc mRtc;

cLcd* lcd = nullptr;
vector<string> mFileVec;
static SemaphoreHandle_t mLockSem;

__IO bool show = false;
__IO cTile* showTile[2] = { nullptr, nullptr };

//extern "C" { void EXTI15_10_IRQHandler() { HAL_GPIO_EXTI_IRQHandler (USER_BUTTON_PIN); } }
//{{{
//void HAL_GPIO_EXTI_Callback (uint16_t GPIO_Pin) {
  //if (GPIO_Pin == USER_BUTTON_PIN)
    //lcd->toggle();
  //}
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

  for (int k = 0; k < 16; k++) {
    vTaskDelay (k*20);
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
    }

  if (readErr == 0)
    lcd->info (COL_YELLOW, "sdRam ok " + hex((uint32_t)addr));
  else {
    string str = "errors ";
    for (int i = 15; i >= 0; i--)
      if (bitErr[i])
        str += " " + dec (bitErr[i], 2,' ');
      else
        str += " __";
    float rate = (readErr * 1000.f) / 0x00100000;
    str += "  " + dec(readErr) + " " + dec (int(rate)/10,1) + "." + dec(int(rate) % 10,1) + "%";
    lcd->info (COL_CYAN, str);
    }
  }
//}}}

//{{{
void findFiles (const string& dirPath, const string& ext) {

  DIR dir;
  if (f_opendir (&dir, dirPath.c_str()) == FR_OK) {
    while (true) {
      FILINFO filinfo;
      if ((f_readdir (&dir, &filinfo) != FR_OK) || !filinfo.fname[0])
        break;
      if (filinfo.fname[0] == '.')
        continue;

      auto filePath = dirPath + "/" + filinfo.fname;
      transform (filePath.begin(), filePath.end(), filePath.begin(), ::tolower);
      if (filinfo.fattrib & AM_DIR) {
        printf ("- findFiles dir %s\n", filePath.c_str());
        //lcd->info (" - findFiles dir" + filePath);
        findFiles (filePath, ext);
        }
      else if (filePath.size() - filePath.find (ext) == ext.size()) {
        printf ("- findFiles file %s\n", filePath.c_str());
        mFileVec.push_back (filePath);
        //lcd->info ("- findFiles file " + filePath);
        }
      else
        printf ("- ignoring %s\n", filePath.c_str());
      }
    f_closedir (&dir);
    }
  }
//}}}

//{{{
void uiThread (void* arg) {

  lcd->display (50);

  int count = 0;
  while (true) {
    if (lcd->isChanged() || (count == 100000)) {
      count = 0;
      lcd->start();
      lcd->clear (COL_BLACK);

      if (showTile[show]) {
        if (showTile[show]->mWidth > lcd->getWidth() || showTile[show]->mHeight > lcd->getHeight()) {
          uint16_t lcdWidth = lcd->getWidth() - 20;
          uint16_t lcdHeight = lcd->getHeight() - 44;
          float xscale = (float)showTile[show]->mWidth / lcdWidth;
          float yscale = (float)showTile[show]->mHeight / lcdHeight;
          if (xscale > yscale)
            lcdHeight = int (lcdHeight * yscale / xscale);
          else
            lcdWidth = int (lcdWidth * xscale / yscale);
         cPoint p ((lcd->getWidth() - lcdWidth) / 2, (lcd->getHeight() - lcdHeight) / 2);
         lcd->size ((cTile*)showTile[show], cRect (p.x, p.y, p.x + lcdWidth, p.y + lcdHeight));
          }
        else
          lcd->copy ((cTile*)showTile[show], cPoint ((lcd->getWidth() - showTile[show]->mWidth) / 2,
                                                     (lcd->getHeight() - showTile[show]->mHeight) / 2));
        }

      //{{{  draw clock
      float hourAngle;
      float minuteAngle;
      float secondAngle;
      float subSecondAngle;
      mRtc.getClockAngles (hourAngle, minuteAngle, secondAngle, subSecondAngle);

      int radius = 60;
      cPoint centre = cPoint (950, 490);
      lcd->ellipseOutline (COL_WHITE, centre, cPoint(radius, radius));

      float hourRadius = radius * 0.7f;
      lcd->line (COL_WHITE, centre, centre + cPoint (int16_t(hourRadius * sin (hourAngle)), int16_t(hourRadius * cos (hourAngle))));
      float minuteRadius = radius * 0.8f;
      lcd->line (COL_WHITE, centre, centre + cPoint (int16_t(minuteRadius * sin (minuteAngle)), int16_t(minuteRadius * cos (minuteAngle))));
      float secondRadius = radius * 0.9f;
      lcd->line (COL_RED, centre, centre + cPoint (int16_t(secondRadius * sin (secondAngle)), int16_t(secondRadius * cos (secondAngle))));

      lcd->cLcd::text (COL_WHITE, 45, mRtc.getClockTimeDateString(), cRect (550,545, 1024,600));
      //}}}
      lcd->setShowInfo (BSP_PB_GetState (BUTTON_KEY) == 0);
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

  bool hwJpeg = BSP_PB_GetState (BUTTON_KEY) == 0;

  char sdPath[4];
  if (FATFS_LinkDriver (&SD_Driver, sdPath) != 0) {
    //{{{  no driver error
    printf ("sdCard - no driver\n");
    lcd->info (COL_RED, "sdCard - no driver");
    }
    //}}}
  else if (f_mount (&fatFs, (TCHAR const*)sdPath, 1) != FR_OK) {
    //{{{  no sdCard error
    printf ("sdCard - not mounted\n");
    lcd->info (COL_RED, "sdCard - not mounted");
    }
    //}}}
  else {
    char label[20] = { 0 };
    DWORD volumeSerialNumber = 0;
    f_getlabel ("", label, &volumeSerialNumber);
    printf ("sdCard mounted label %s\n", label);
    lcd->info ("sdCard mounted label:" + string (label));

    findFiles ("", ".jpg");
    printf ("found %d piccies\n", mFileVec.size());
    lcd->info (COL_WHITE, "found " + dec(mFileVec.size()) + " piccies");

    int count = 1;
    for (auto fileName : mFileVec) {
      FILINFO filInfo;
      if (f_stat (fileName.c_str(), &filInfo))
        printf ("APP fstat fail\n");

      printf ("APP decode %s size:%d time:%d date:%d\n",
              fileName.c_str(), int(filInfo.fsize), filInfo.ftime, filInfo.fdate);
      delete showTile[!show];

      auto startTime = HAL_GetTick();
      if (hwJpeg)
        showTile[!show] = hwJpegDecode (fileName);
      else
        showTile[!show] = swJpegDecode (fileName, SW_SCALE);
      show = !show;

      if (showTile[show]) {
        printf ("APP decoded - show:%d - took %d\n", show, HAL_GetTick() - startTime);
        lcd->setTitle (dec (count++) + " of " + dec(mFileVec.size()) + " " +
                       fileName + " " +
                       dec (showTile[show]->mWidth) + "x" + dec (showTile[show]->mHeight) + " " +
                       dec ((int)(filInfo.fsize) / 1000) + "k " +
                       dec (filInfo.ftime >> 11) + ":" + dec ((filInfo.ftime >> 5) & 63) + " " +
                       dec (filInfo.fdate & 31) + ":" + dec ((filInfo.fdate >> 5) & 15) + ":" + dec ((filInfo.fdate >> 9) + 1980) +
                       " took " + dec(HAL_GetTick() - startTime) + "ms");
        vTaskDelay (200);
        }
      else {
        printf ("decode %s tile error\n", fileName.c_str());
        lcd->info ("decode load error " + fileName);
        vTaskDelay (500);
        }
      }
    }

  uint32_t offset = 0;
  while (true)
    for (int j = 4; j <= 0x3F; j++) {
      offset += HAL_GetTick();
      sdRamTest (uint16_t(offset++), (uint16_t*)(SDRAM_DEVICE_ADDR + (j * 0x00200000)), 0x00200000);
      vTaskDelay (200);
      }

  while (true)
    vTaskDelay (1000);
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

  // Enable D2 domain SRAM Clocks
  __HAL_RCC_D2SRAM1_CLK_ENABLE();
  __HAL_RCC_D2SRAM2_CLK_ENABLE();
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
                          RCC_CLOCKTYPE_PCLK1   | RCC_CLOCKTYPE_PCLK2 |
                          RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_D3PCLK1);
  rccClkInit.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  rccClkInit.SYSCLKDivider = RCC_SYSCLK_DIV1;
  rccClkInit.AHBCLKDivider = RCC_HCLK_DIV2;
  rccClkInit.APB1CLKDivider = RCC_APB1_DIV2;
  rccClkInit.APB2CLKDivider = RCC_APB2_DIV2;
  rccClkInit.APB3CLKDivider = RCC_APB3_DIV2;
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
  //}}}

  #define kClockEnable  FMC_SDRAM_CMD_TARGET_BANK2 | FMC_SDRAM_CMD_CLK_ENABLE;
  #define kPreChargeAll FMC_SDRAM_CMD_TARGET_BANK2 | FMC_SDRAM_CMD_PALL;
  #define kAutoRefresh  FMC_SDRAM_CMD_TARGET_BANK2 | FMC_SDRAM_CMD_AUTOREFRESH_MODE | ((8-1) << 5)
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
  sdramHandle.Init.SDClockPeriod      = FMC_SDRAM_CLOCK_PERIOD_3;
  sdramHandle.Init.ReadBurst          = FMC_SDRAM_RBURST_ENABLE;
  sdramHandle.Init.CASLatency         = FMC_SDRAM_CAS_LATENCY_2;
  sdramHandle.Init.ColumnBitsNumber   = FMC_SDRAM_COLUMN_BITS_NUM_11; //9
  sdramHandle.Init.RowBitsNumber      = FMC_SDRAM_ROW_BITS_NUM_13;   //12
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
  HAL_Delay (1);
  FMC_SDRAM_DEVICE->SDCMR = kAutoRefresh;
  HAL_Delay (1);
  FMC_SDRAM_DEVICE->SDCMR = kAutoRefresh;
  HAL_Delay (1);
  FMC_SDRAM_DEVICE->SDCMR = kLoadMode;
  //uint32_t refreshCount = (516 - 20) << 1;
  //FMC_SDRAM_DEVICE->SDRTR = 0;//refreshCount;
  }
//}}}
//{{{
void mpuConfig() {

  // Disable the MPU
  HAL_MPU_Disable();

  MPU_Region_InitTypeDef mpuRegion;
  mpuRegion.Enable = MPU_REGION_ENABLE;
  mpuRegion.AccessPermission = MPU_REGION_FULL_ACCESS;
  mpuRegion.TypeExtField = MPU_TEX_LEVEL0;
  mpuRegion.SubRegionDisable = 0x00;
  mpuRegion.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;

  mpuRegion.Number = MPU_REGION_NUMBER0;
  mpuRegion.BaseAddress = 0x24000000;
  mpuRegion.Size = MPU_REGION_SIZE_512KB;
  mpuRegion.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  mpuRegion.IsCacheable = MPU_ACCESS_CACHEABLE;
  mpuRegion.IsShareable = MPU_ACCESS_SHAREABLE;
  HAL_MPU_ConfigRegion (&mpuRegion);

  mpuRegion.Number = MPU_REGION_NUMBER1;
  mpuRegion.BaseAddress = 0xD0000000;
  mpuRegion.Size = MPU_REGION_SIZE_128MB;
  mpuRegion.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  mpuRegion.IsCacheable = MPU_ACCESS_CACHEABLE;
  mpuRegion.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  HAL_MPU_ConfigRegion (&mpuRegion);

  mpuRegion.Number = MPU_REGION_NUMBER2;
  mpuRegion.BaseAddress = 0x30000000;
  mpuRegion.Size = MPU_REGION_SIZE_512KB;
  mpuRegion.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  mpuRegion.IsCacheable = MPU_ACCESS_CACHEABLE;
  mpuRegion.IsShareable = MPU_ACCESS_SHAREABLE;
  HAL_MPU_ConfigRegion (&mpuRegion);

  // Enable the MPU
  HAL_MPU_Enable (MPU_PRIVILEGED_DEFAULT);
  }
//}}}

//{{{
int main() {

  HAL_Init();
  clockConfig();
  sdRamConfig();

  SCB_EnableICache();
  SCB_EnableDCache();
  mpuConfig();

  BSP_LED_Init (LED_GREEN);
  BSP_LED_Init (LED_BLUE);
  BSP_LED_Init (LED_RED);

  BSP_PB_Init (BUTTON_KEY, BUTTON_MODE_GPIO);

  mRtc.init();
  printf ("%s\n", kHello.c_str());
  mLockSem = xSemaphoreCreateMutex();

  dtcmInit (0x20000000,  0x00020000);
  sram123Init (0x30000000,  0x00048000);
  sdRamInit (SDRAM_DEVICE_ADDR + LCD_WIDTH*LCD_HEIGHT*4,  SDRAM_DEVICE_SIZE - LCD_WIDTH*LCD_HEIGHT*4);

  lcd = new cLcd ((uint16_t*)SDRAM_DEVICE_ADDR, (uint16_t*)(SDRAM_DEVICE_ADDR + LCD_WIDTH*LCD_HEIGHT*2));
  lcd->init (kHello);

  TaskHandle_t uiHandle;
  xTaskCreate ((TaskFunction_t)uiThread, "ui", 1024, 0, 4, &uiHandle);

  TaskHandle_t appHandle;
  xTaskCreate ((TaskFunction_t)appThread, "app", 8192, 0, 4, &appHandle);

  vTaskStartScheduler();

  return 0;
  }
//}}}
