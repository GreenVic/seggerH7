#include <stdio.h>
#include <stdlib.h>

#include "stm32h7xx_hal.h"
#include "stm32h7xx_nucleo_144.h"

void main() {

  HAL_Init();
  BSP_LED_Init (LED_GREEN);
  BSP_LED_Init (LED_BLUE);
  BSP_LED_Init (LED_RED);

  int i;
  for (i = 0; i < 100; i++) {
    printf("Hello World %d!\n", i);
    HAL_Delay (200);
    BSP_LED_Toggle (LED_GREEN);
    BSP_LED_Toggle (LED_BLUE);
    BSP_LED_Toggle (LED_RED);
    }
  do {
    i++;
  } while (1);
}
