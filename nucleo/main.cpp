//// main.cpp
//{{{  includes
#include <algorithm>
#include <string>
#include <ctype.h>

#include "cmsis_os.h"
#include "stm32h7xx_nucleo_144.h"
#include "heap.h"

#include "cRtc.h"
#include "cLcd.h"
#include "sd.h"
#include "jpeg.h"
#include "lsm303c.h"

#include "../fatFs/ff.h"
#include "../common/cTraceVec.h"
#include "agg.h"

using namespace std;
//}}}
//{{{
enum {
  width  = 500,
  height = 400
  };
//}}}

//{{{
void draw_ellipse (cRasteriser& ras, float x, float y, float rx, float ry) {

  ras.moveTod (x + rx, y);

  for (int i = 1; i < 360; i++) {
    float a = float(i) * 3.1415926 / 180.0;
    ras.lineTod (x + cos(a) * rx, y + sin(a) * ry);
    }
  }
//}}}
//{{{
void draw_line (cRasteriser& ras, float x1, float y1, float x2, float y2, float width) {

  float dx = x2 - x1;
  float dy = y2 - y1;
  float d = sqrt(dx*dx + dy*dy);

  dx = width * (y2 - y1) / d;
  dy = width * (x2 - x1) / d;

  ras.moveTod (x1 - dx,  y1 + dy);
  ras.lineTod (x2 - dx,  y2 + dy);
  ras.lineTod (x2 + dx,  y2 - dy);
  ras.lineTod (x1 + dx,  y1 - dy);
  }
//}}}

#define SW_JPEG
#define SW_SCALE 4
#define FMC_PERIOD  FMC_SDRAM_CLOCK_PERIOD_2

const string kHello = "stm32h7 testbed " + string(__TIME__) + " " + string(__DATE__);

// vars
FATFS fatFs;
cRtc* mRtc;

cLcd* lcd = nullptr;
vector<string> mFileVec;
int gCount = 0;

__IO bool gShow = false;
__IO cTile* showTile[2] = { nullptr, nullptr };

cTraceVec mTraceVec;
int16_t la[3] = { 0 };
int16_t mf[3] = { 0 };

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

  cTarget target (lcd->getDrawBuf(), lcd->getWidth(), lcd->getHeight());
  cRenderer<tRgb565Span> renderer (target);
  cRasteriser rasteriser;

  lcd->display (70);

  int count = 0;
  while (true) {
    if (lcd->isChanged() || (count == 1000)) {
      count = 0;
      lcd->start();
      lcd->clear (COL_BLACK);

      const cRect titleRect (0,0, lcd->getWidth() * gCount / mFileVec.size(),22);
      lcd->grad (COL_BLUE, COL_GREY, COL_GREY, COL_BLACK, titleRect);

      if (showTile[gShow]) {
        if (showTile[gShow]->mWidth > lcd->getWidth() || showTile[gShow]->mHeight > lcd->getHeight()) {
          uint16_t lcdWidth = lcd->getWidth() - 20;
          uint16_t lcdHeight = lcd->getHeight() - 44;
          float xscale = (float)showTile[gShow]->mWidth / lcdWidth;
          float yscale = (float)showTile[gShow]->mHeight / lcdHeight;
          if (xscale > yscale)
            lcdHeight = int (lcdHeight * yscale / xscale);
          else
            lcdWidth = int (lcdWidth * xscale / yscale);
         cPoint p ((lcd->getWidth() - lcdWidth) / 2, (lcd->getHeight() - lcdHeight) / 2);
         lcd->size ((cTile*)showTile[gShow], cRect (p.x, p.y, p.x + lcdWidth, p.y + lcdHeight));
          }
        else
          lcd->copy ((cTile*)showTile[gShow], cPoint ((lcd->getWidth() - showTile[gShow]->mWidth) / 2,
                                                     (lcd->getHeight() - showTile[gShow]->mHeight) / 2));
        }

      lcd->setShowInfo (BSP_PB_GetState (BUTTON_KEY) == 0);
      lcd->drawInfo();

      mTraceVec.draw (lcd, 20, 450);
      //{{{  draw clock
      target.setBuffer (lcd->getDrawBuf());
      draw_ellipse (rasteriser, 950, 490, 60, 60);
      rasteriser.render (renderer, tRgba (255, 255, 255, 128));

      float hourAngle;
      float minuteAngle;
      float secondAngle;
      float subSecondAngle;
      mRtc->getClockAngles (hourAngle, minuteAngle, secondAngle, subSecondAngle);

      cPoint centre = cPoint (950, 490);
      float radius = 60.0f;
      float hourRadius = radius * 0.7f;
      float minuteRadius = radius * 0.8f;
      float secondRadius = radius * 0.95f;

      draw_line (rasteriser, centre.x, centre.y,
                 centre.x + (hourRadius * sin (hourAngle)),
                 centre.y + (hourRadius * cos (hourAngle)), 3.0f);
      rasteriser.render (renderer, tRgba (255,255,255,255));

      draw_line (rasteriser, centre.x, centre.y,
                 centre.x + (minuteRadius * sin (minuteAngle)),
                 centre.y + (minuteRadius * cos (minuteAngle)), 2.0f);
      rasteriser.render (renderer, tRgba (255,255,255,192));

      draw_line (rasteriser, centre.x, centre.y,
                 centre.x + (secondRadius * sin (secondAngle)),
                 centre.y + (secondRadius * cos (secondAngle)), 2.0f);
      rasteriser.render (renderer, tRgba (255,0,0,180));

      lcd->cLcd::text (COL_BLACK, 45, mRtc->getClockTimeDateString(), cRect (567,552, 1024,600));
      lcd->cLcd::text (COL_WHITE, 45, mRtc->getClockTimeDateString(), cRect (567,552, 1024,600) + cPoint(-2,-2));
      //}}}

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
    printf ("mounted label %s\n", label);
    lcd->info ("mounted " + string (label));

    findFiles ("", ".jpg");
    printf ("%d piccies\n", mFileVec.size());
    lcd->setTitle (string(label) + " " + dec (mFileVec.size()) + " piccies");

    for (auto fileName : mFileVec) {
      gCount++;
      FILINFO filInfo;
      if (f_stat (fileName.c_str(), &filInfo))
        printf ("APP fstat fail\n");

      printf ("APP decode %s size:%d time:%d date:%d\n",
              fileName.c_str(), int(filInfo.fsize), filInfo.ftime, filInfo.fdate);

      auto startTime = HAL_GetTick();
      delete showTile[gShow];
      showTile[gShow] = hwJpeg ? hwJpegDecode (fileName) : swJpegDecode (fileName, SW_SCALE);
      gShow = !gShow;

      if (showTile[gShow]) {
        printf ("APP decoded - show:%d - took %d\n", gShow, HAL_GetTick() - startTime);
        lcd->setTitle (fileName + " " +
                       dec (showTile[gShow]->mWidth) + "x" + dec (showTile[gShow]->mHeight) + " " +
                       dec ((int)(filInfo.fsize) / 1000) + "k " +
                       dec (filInfo.ftime >> 11, 2, '0') + ":" +
                       dec ((filInfo.ftime >> 5) & 0x3F, 2, '0') + ":" +
                       dec ((filInfo.ftime & 0x1F) * 2, 2, '0') + " " +
                       dec (filInfo.fdate & 0x1F) + "." +
                       dec ((filInfo.fdate >> 5) & 0xF) + "." +
                       dec ((filInfo.fdate >> 9) + 1980) + " " +
                       dec (HAL_GetTick() - startTime) + "ms");
        vTaskDelay (100);
        }
      }
    //char stats [250];
    //vTaskList (stats);
    //printf ("%s", stats);
    }

  //uint32_t offset = 0;
  //while (true)
  //  for (int j = 4; j <= 0x3F; j++) {
  //    offset += HAL_GetTick();
  //    sdRamTest (uint16_t(offset++), (uint16_t*)(SDRAM_DEVICE_ADDR + (j * 0x00200000)), 0x00200000);
  //    vTaskDelay (200);
  //    }

  //  accel
  //lsm303c_init();
  while (true) {
   // while (lsm303c_la_ready()) {
   //   lsm303c_la (la);
   //   mTraceVec.addSample (0, la[0]);
    //  mTraceVec.addSample (1, la[1]);
    //  mTraceVec.addSample (2, la[2]);
    //  lcd->change();
    //  }

    //lcd->info (COL_YELLOW, "MF x:" + dec(mf[0]) + " y:" + dec(mf[1]) + " z:" + dec(mf[2]));
    //while (lsm303c_mf_ready())
    //  lsm303c_mf (mf);
    vTaskDelay (2);
    }
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
  rccOscInit.PLL.PLLQ = 4;
  rccOscInit.PLL.PLLR = 2;
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
  HAL_RCC_ClockConfig (&rccClkInit, FLASH_LATENCY_2);
  //HAL_RCC_ClockConfig (&rccClkInit, FLASH_LATENCY_4);

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
  sdramHandle.Init.SDClockPeriod      = FMC_PERIOD;
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

  // sram axi
  mpuRegion.Number = MPU_REGION_NUMBER0;
  mpuRegion.BaseAddress = 0x24000000;
  mpuRegion.Size = MPU_REGION_SIZE_512KB;
  mpuRegion.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  mpuRegion.IsCacheable = MPU_ACCESS_CACHEABLE;
  mpuRegion.IsShareable = MPU_ACCESS_SHAREABLE;
  HAL_MPU_ConfigRegion (&mpuRegion);

  // sdram
  mpuRegion.Number = MPU_REGION_NUMBER1;
  mpuRegion.BaseAddress = 0xD0000000;
  mpuRegion.Size = MPU_REGION_SIZE_128MB;
  mpuRegion.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  mpuRegion.IsCacheable = MPU_ACCESS_CACHEABLE;
  mpuRegion.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  HAL_MPU_ConfigRegion (&mpuRegion);

  // sram123
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

  BSP_LED_Init (LED_RED);
  BSP_PB_Init (BUTTON_KEY, BUTTON_MODE_GPIO);

  printf ("%s\n", kHello.c_str());
  mTraceVec.addTrace (1024, 1, 3);

  mRtc = new cRtc();
  mRtc->init();

  lcd = new cLcd();
  lcd->init (kHello);

  TaskHandle_t uiHandle;
  xTaskCreate ((TaskFunction_t)uiThread, "ui", 1024, 0, 4, &uiHandle);

  TaskHandle_t appHandle;
  xTaskCreate ((TaskFunction_t)appThread, "app", 4096, 0, 4, &appHandle);

  vTaskStartScheduler();

  return 0;
  }
//}}}
