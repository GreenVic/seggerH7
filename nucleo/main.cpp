// main.cpp
//{{{  includes
#include "cmsis_os.h"
#include "stm32h7xx_nucleo_144.h"
#include "cLcd.h"
//}}}
//{{{  defines
#define SDRAM_DEVICE_ADDR 0xD0000000
#define SDRAM_DEVICE_SIZE 0x08000000
//}}}
//{{{  const
const std::string kHello = std::string(__TIME__) + " " + std::string(__DATE__);

const HeapRegion_t kHeapRegions[] = {
  {(uint8_t*)(SDRAM_DEVICE_ADDR + LCD_WIDTH*LCD_HEIGHT*4), SDRAM_DEVICE_SIZE - LCD_WIDTH*LCD_HEIGHT*4 },
  { nullptr, 0 } };
//}}}

cLcd* lcd = nullptr;

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

  // Voltage scaling allows optimizing the power consumption when the device is
  // clocked below the maximum system frequency, to update the voltage scaling value
  // regarding system frequency refer to product datasheet
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
  if (HAL_RCC_OscConfig (&RCC_OscInitStruct) != HAL_OK)
    while (true);

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
  if (HAL_RCC_ClockConfig (&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
    while (true);
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
                              GPIO_PIN_8 |
                              GPIO_PIN_15;
  HAL_GPIO_Init (GPIOG, &gpio_init_structure);
  //}}}
  __HAL_RCC_FMC_CLK_ENABLE();

  SDRAM_HandleTypeDef sdramHandle;
  sdramHandle.Instance = FMC_SDRAM_DEVICE;
  sdramHandle.Init.SDBank             = FMC_SDRAM_BANK2;
  sdramHandle.Init.SDClockPeriod      = FMC_SDRAM_CLOCK_PERIOD_3;
  sdramHandle.Init.ReadBurst          = FMC_SDRAM_RBURST_ENABLE;
  sdramHandle.Init.CASLatency         = FMC_SDRAM_CAS_LATENCY_2;
  sdramHandle.Init.ColumnBitsNumber   = FMC_SDRAM_COLUMN_BITS_NUM_11;
  sdramHandle.Init.RowBitsNumber      = FMC_SDRAM_ROW_BITS_NUM_13;
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
  if (HAL_SDRAM_Init (&sdramHandle, &timing) != HAL_OK)
    printf ("HAL_SDRAM_Init failed\n");

  FMC_SDRAM_DEVICE->SDCMR = kClockEnable;
  HAL_Delay (1);
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
void displayThread (void* arg) {

  lcd->render();
  lcd->display (50);

  while (true) {
    lcd->start();
    lcd->clear (COL_BLACK);
    lcd->drawInfo();
    lcd->present();
    }
  }
//}}}
//{{{
void appThread (void* arg) {

  int i = 0;
  while (true) {
    switch (i++ % 7) {
      case 0 : lcd->info (COL_WHITE,   "Hello colin white\n"); break;
      case 1 : lcd->info (COL_RED  ,   "Hello colin red abcdefghijklmn\n"); break;
      case 2 : lcd->info (COL_GREEN,   "Hello colin green opqrstuvwxyz\n"); break;
      case 3 : lcd->info (COL_BLUE,    "Hello colin blue zxcvbnm\n"); break;
      case 4 : lcd->info (COL_MAGENTA, "Hello colin magenta 0123456789\n"); break;
      case 5 : lcd->info (COL_CYAN,    "Hello colin cyan ?><:;@'()*&\n"); break;
      case 6 : lcd->info (COL_YELLOW,  "Hello colin yellow ABCDEFGHIGJKNMONOPQRSTUVWXYZ\n"); break;
      }

    vTaskDelay (100);
    BSP_LED_Toggle (LED_BLUE);
    }
  }
//}}}
//{{{
void sdRamTestThread (void* arg) {

  int i = 0;
  int k = 0;
  while (true) {
    printf ("Ram test iteration %d\n", i++);
    for (int j = 1; j < 4; j++) {
      BSP_LED_Toggle (LED_GREEN);
      if (sdRamTest (k++, (uint16_t*)(0xD0000000 + (j * 0x02000000)), 0x02000000) == 0) {
        BSP_LED_On (LED_BLUE);
        BSP_LED_Off (LED_RED);
        }
      else {
        BSP_LED_On (LED_RED);
        BSP_LED_Off (LED_BLUE);
        }
      vTaskDelay (100);
      }
    }
  }
//}}}

int main() {

  HAL_Init();
  systemClockConfig();

  SCB_EnableICache();
  SCB_EnableDCache();
  sdRamInit();

  BSP_LED_Init (LED_GREEN);
  BSP_LED_Init (LED_BLUE);
  BSP_LED_Init (LED_RED);

  vPortDefineHeapRegions (kHeapRegions);
  lcd = new cLcd ((uint16_t*)SDRAM_DEVICE_ADDR, (uint16_t*)(SDRAM_DEVICE_ADDR + LCD_WIDTH*LCD_HEIGHT*2));
  lcd->init (kHello);

  TaskHandle_t displayHandle;
  xTaskCreate ((TaskFunction_t)displayThread, "display", 1024, 0, 4, &displayHandle);

  TaskHandle_t appHandle;
  xTaskCreate ((TaskFunction_t)appThread, "app", 1024, 0, 4, &appHandle);

  //TaskHandle_t testHandle;
  //xTaskCreate ((TaskFunction_t)sdRamTestThread, "test", 1024, 0, 4, &testHandle);
  vTaskStartScheduler();

  return 0;
  }
