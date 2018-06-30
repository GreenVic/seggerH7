// main.cpp
#include <stdio.h>
#include <stdlib.h>
#include "stm32h7xx_hal.h"
#include "stm32h7xx_nucleo_144.h"
#include "sdram.h"

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
  // regarding system frequency refer to product datasheet.  */
  __HAL_PWR_VOLTAGESCALING_CONFIG (PWR_REGULATOR_VOLTAGE_SCALE1);

  while (!__HAL_PWR_GET_FLAG (PWR_FLAG_VOSRDY)) {}

  // Enable D2 domain SRAM3 Clock (0x30040000 AXI)
  __HAL_RCC_D2SRAM3_CLK_ENABLE();

  // enable HSE Oscillator and activate PLL with HSE as source
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

  // select PLL as system clock source and configure  bus clocks dividers
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
void sdRamTest (uint32_t fromValue, uint32_t toValue, uint16_t* addr, uint32_t len) {

  for (uint32_t i = fromValue; i <= toValue; i++) {
    uint16_t data = i;
    auto writeAddress = addr;
    for (uint32_t j = 0; j < len/2; j++)
      *writeAddress++ = data++;

    uint32_t readOk = 0;
    uint32_t readErr = 0;
    auto readAddress = addr;
    for (uint32_t j = 0; j < len / 2; j++) {
      uint16_t readWord1 = *readAddress++;
      if (readWord1 == ((j+i) & 0xFFFF))
        readOk++;
      else {
        if (readErr < 4)
          printf ("- error %p %02x %d - r:%04x != %04x\n", readAddress, i, readErr, readWord1, (j+i) & 0xFFFF);
        readErr++;
        }
      }
    printf ("%p i:%x ok:%x error:%x %d\n", addr, i, readOk, readErr, (readOk * 100) / (len/2));
    //lcd->info ("ok " +
    //           hex ((uint32_t)addr) + " " +
    //           dec (i) + " " +
    //           hex (readOk) + " " +
    //           hex (readErr) + " " +
    //           dec ((readOk * 100) / (len/2)));
    }
  }
//}}}

int main() {

  HAL_Init();
  systemClockConfig();

  SCB_EnableICache();
  SCB_EnableDCache();
  BSP_SDRAM_Init();

  BSP_LED_Init (LED_GREEN);
  BSP_LED_Init (LED_BLUE);
  BSP_LED_Init (LED_RED);

  int i;
  while (true) {
    printf ("Hello World %d!\n", i++);
    HAL_Delay (100);
    BSP_LED_Toggle (LED_GREEN);
    HAL_Delay (100);
    BSP_LED_Toggle (LED_BLUE);
    HAL_Delay (100);
    BSP_LED_Toggle (LED_RED);
    sdRamTest (i, i, (uint16_t*)0xD0000000, 0x01000000);
    }

  return 0;
  }
