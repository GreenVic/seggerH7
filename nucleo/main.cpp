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
FATFS SDFatFs;
char SDPath[4];

vector<string> mFileVec;
vector<cTile*> mTileVec;

extern "C" { void EXTI15_10_IRQHandler() { HAL_GPIO_EXTI_IRQHandler (USER_BUTTON_PIN); } }
//{{{
void HAL_GPIO_EXTI_Callback (uint16_t GPIO_Pin) {
  if (GPIO_Pin == USER_BUTTON_PIN)
    lcd->toggle();
  }
//}}}

//{{{
void systemClockConfig() {
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
  RCC_OscInitTypeDef RCC_OscInitStruct;
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.HSIState = RCC_HSI_OFF;
  RCC_OscInitStruct.CSIState = RCC_CSI_OFF;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 400;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  HAL_RCC_OscConfig (&RCC_OscInitStruct);

  // select PLL system clock source. config bus clocks dividers
  RCC_ClkInitTypeDef RCC_ClkInitStruct;
  RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_SYSCLK  | RCC_CLOCKTYPE_HCLK |
                                 RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_PCLK1 |
                                 RCC_CLOCKTYPE_PCLK2   | RCC_CLOCKTYPE_D3PCLK1);
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;
  HAL_RCC_ClockConfig (&RCC_ClkInitStruct, FLASH_LATENCY_4);

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
void sdRamInit() {
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
void statFile (const string& fileName) {

  //printf ("statFile %s\n", fileName.c_str());
  FILINFO filInfo;
  if (f_stat (fileName.c_str(), &filInfo)) {
    lcd->info (COL_RED, "statFile " + fileName + " not found");
    lcd->change();
    return;
    }
  else {
    lcd->info ("statFile " + fileName + " bytes:" + dec ((int)(filInfo.fsize)) + " " +
               dec (filInfo.ftime >> 11) + ":" + dec ((filInfo.ftime >> 5) & 63) + " " +
               dec (filInfo.fdate & 31) + ":" + dec ((filInfo.fdate >> 5) & 15) + ":" + dec ((filInfo.fdate >> 9) + 1980));
    lcd->change();
    }

  FIL file;
  if (f_open (&file, fileName.c_str(), FA_READ)) {
    lcd->info (COL_RED, "statFile " + fileName + " f_open failed");
    lcd->change();
    return;
    }

  // malloc load
  if (true) {
    auto buf = (uint8_t*)malloc (filInfo.fsize);
    if (buf) {
      UINT bytesRead = 0;
      f_read (&file, buf, (UINT)filInfo.fsize, &bytesRead);
      if (bytesRead != filInfo.fsize)
        lcd->info (COL_RED, "statFile buf size fail " + dec (bytesRead) + " " + dec (filInfo.fsize));

      free (buf);
      }
    else
      lcd->info (COL_RED, "statFile buf malloc fail");
    }

  if (false) {
    // pvPortMalloc load
    auto buf1 = (uint8_t*)pvPortMalloc (filInfo.fsize);
    if (buf1) {
      //f_rewind (&file);
      UINT bytesRead = 0;
      //f_read (&file, buf1, (UINT)filInfo.fsize, &bytesRead);
      //if (bytesRead != filInfo.fsize)
      //  lcd->info (COL_RED, "statFile buf1 size fail " + dec (bytesRead) + " " + dec (filInfo.fsize));
      vPortFree (buf1);
      }
    else
      lcd->info (COL_RED, "statFile buf1 pvPortMalloc fail");
    }
  lcd->change();


  f_close (&file);
  }
//}}}
//{{{
cTile* loadFile (const string& fileName, int scale) {

  FILINFO filInfo;
  if (f_stat (fileName.c_str(), &filInfo)) {
    lcd->info (COL_RED, fileName + " not found");
    return nullptr;
    }
  lcd->info ("loadFile " + fileName + " bytes:" + dec ((int)(filInfo.fsize)) + " " +
             dec (filInfo.ftime >> 11) + ":" + dec ((filInfo.ftime >> 5) & 63) + " " +
             dec (filInfo.fdate & 31) + ":" + dec ((filInfo.fdate >> 5) & 15) + ":" + dec ((filInfo.fdate >> 9) + 1980));

  FIL gFile;
  if (f_open (&gFile, fileName.c_str(), FA_READ)) {
    lcd->info (COL_RED, fileName + " not opened");
    return nullptr;
    }

  auto buf = (uint8_t*)pvPortMalloc (filInfo.fsize);
  if (!buf)
    lcd->info (COL_RED, "buf fail");

  UINT bytesRead = 0;
  f_read (&gFile, buf, (UINT)filInfo.fsize, &bytesRead);
  f_close (&gFile);

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

    auto rgb565pic = (uint16_t*)pvPortMalloc (mCinfo.output_width * mCinfo.output_height * 2);
    auto tile = new cTile ((uint8_t*)rgb565pic, 2, mCinfo.output_width, 0,0, mCinfo.output_width, mCinfo.output_height);

    auto rgbLine = (uint8_t*)malloc (mCinfo.output_width * 3);
    while (mCinfo.output_scanline < mCinfo.output_height) {
      jpeg_read_scanlines (&mCinfo, &rgbLine, 1);
      lcd->rgb888to565 (rgbLine, rgb565pic + ((mCinfo.output_scanline-1) * mCinfo.output_width), mCinfo.output_width);
      }
    free (rgbLine);

    vPortFree (buf);
    jpeg_finish_decompress (&mCinfo);

    lcd->info (COL_YELLOW, "loaded " + dec(mCinfo.image_width) + "x" + dec(mCinfo.image_height) + " " +
                                       dec(mCinfo.output_width) + "x" + dec(mCinfo.output_height));
    jpeg_destroy_decompress (&mCinfo);

    return tile;
    }
  else {
    lcd->info (COL_RED, "loadFile read failed");
    vPortFree (buf);
    return nullptr;
    }
  }
//}}}

//{{{
void displayThread (void* arg) {

  lcd->render();
  lcd->display (60);

  while (true) {
    if (true || lcd->changed()) {
      lcd->start();
      lcd->clear (COL_BLACK);

      //int items = mTileVec.size();
      //int rows = int(sqrt (float(items))) + 1;
      //int count = 0;
      //for (auto tile : mTileVec) {
      //  lcd->copy (tile, cPoint  (
      //              (lcd->getWidth() / rows) * (count % rows) + (lcd->getWidth() / rows - tile->mWidth) / 2,
      //              (lcd->getHeight() / rows) * (count / rows) + (lcd->getHeight() / rows - tile->mHeight) / 2));
      //  count++;
      //  }

      lcd->drawInfo();
      lcd->present();
      vTaskDelay (20);
      }
    }
  }
//}}}
//{{{
void appThread (void* arg) {

  //BSP_PB_Init (BUTTON_KEY, BUTTON_MODE_GPIO);
  lcd->info (COL_WHITE,   "Hello colin white\n");
  lcd->info (COL_RED  ,   "Hello colin red abcdefghijklmn\n");
  lcd->info (COL_GREEN,   "Hello colin green  opqrstuvwxyz\n");
  lcd->info (COL_BLUE,    "Hello colin blue zxcvbnm\n");
  lcd->info (COL_MAGENTA, "Hello colin magenta 0123456789\n");
  lcd->info (COL_CYAN,    "Hello colin cyan ?><:;@'()*&\n");
  lcd->info (COL_YELLOW,  "Hello colin yellow ABCDEFGHIGJKNMONOPQRSTUVWXYZ\n");

  printf ("Hello colin white\n");

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
    // get label
    char label[20] = {0};
    DWORD volumeSerialNumber = 0;
    f_getlabel ("", label, &volumeSerialNumber);
    lcd->info ("sdCard mounted label:" + string (label));
    lcd->changed();

    findFiles ("", ".jpg");

    //simpleTest();
    auto startTime = HAL_GetTick();
    for (auto file : mFileVec)
      statFile (file);
    lcd->info (COL_YELLOW, "statFile took " + dec (HAL_GetTick() - startTime));
    //mTileVec.push_back (loadFile (file, 4));
    }

  while (true) {
    for (int i = 0; i < 100; i += 2) {
      //lcd->info ("fade " + dec (i));
      lcd->display (i);
      vTaskDelay (50);
      }
    for (int i = 100; i > 10; i -= 2) {
      //lcd->info ("fade " + dec (i));
      lcd->display (i);
      vTaskDelay (50);
      }
    }

  vTaskDelay (200000);
  }
//}}}
//{{{
void sdRamTestThread (void* arg) {

  int i = 0;
  int k = 0;
  while (true) {
    printf ("Ram test iteration %d\n", i++);
    for (int j = 3; j < 16; j++) {
      BSP_LED_Toggle (LED_BLUE);
      if (sdRamTest (k++, (uint16_t*)(0x70000000 + (j * 0x00100000)), 0x00100000) == 0) {
        BSP_LED_Off (LED_RED);
        lcd->info (COL_RED, "sdCard - ok " + dec (j));
        }
      else  {
        BSP_LED_On (LED_RED);
        lcd->info (COL_RED, "sdCard - fail");
        }
      vTaskDelay (100);
      }
    }
  }
//}}}

int main() {

  HAL_Init();
  systemClockConfig();

  sdRamInit();
  //HAL_SetFMCMemorySwappingConfig (FMC_SWAPBMAP_SDRAM_SRAM);
  SCB_EnableICache();
  SCB_EnableDCache();
  mpuConfig();

  BSP_LED_Init (LED_GREEN);
  BSP_LED_Init (LED_BLUE);
  BSP_LED_Init (LED_RED);
  BSP_PB_Init (BUTTON_KEY, BUTTON_MODE_EXTI);

  vPortDefineHeapRegions (kHeapRegions);
  lcd = new cLcd ((uint16_t*)SDRAM_DEVICE_ADDR, (uint16_t*)(SDRAM_DEVICE_ADDR + LCD_WIDTH*LCD_HEIGHT*2));
  lcd->init (kHello);

  TaskHandle_t displayHandle;
  xTaskCreate ((TaskFunction_t)displayThread, "display", 2048, 0, 4, &displayHandle);

  TaskHandle_t appHandle;
  xTaskCreate ((TaskFunction_t)appThread, "app", 8192, 0, 4, &appHandle);

  TaskHandle_t testHandle;
  //xTaskCreate ((TaskFunction_t)sdRamTestThread, "test", 1024, 0, 4, &testHandle);
  vTaskStartScheduler();

  return 0;
  }
