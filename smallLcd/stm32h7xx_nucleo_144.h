#pragma once
//{{{
#ifdef __cplusplus
  extern "C" {
#endif
//}}}

#include "stm32h7xx_hal.h"

typedef enum { LED1 = 0, LED_GREEN = LED1, LED2 = 1, LED_BLUE = LED2, LED3 = 2, LED_RED = LED3 } Led_TypeDef;
typedef enum { BUTTON_USER = 0, /* Alias */ BUTTON_KEY = BUTTON_USER } Button_TypeDef;

typedef enum { BUTTON_MODE_GPIO = 0, BUTTON_MODE_EXTI = 1 } ButtonMode_TypeDef;

#define LEDn                                     3

#define BUTTONn                                  1
#define USER_BUTTON_PIN                          GPIO_PIN_13

#define OTG_FS1_OVER_CURRENT_PIN                 GPIO_PIN_7
#define OTG_FS1_OVER_CURRENT_PORT                GPIOG
#define OTG_FS1_OVER_CURRENT_PORT_CLK_ENABLE()   __HAL_RCC_GPIOG_CLK_ENABLE()

#define OTG_FS1_POWER_SWITCH_PIN                 GPIO_PIN_6
#define OTG_FS1_POWER_SWITCH_PORT                GPIOG
#define OTG_FS1_POWER_SWITCH_PORT_CLK_ENABLE()   __HAL_RCC_GPIOG_CLK_ENABLE()

void BSP_LED_Init (Led_TypeDef Led);
void BSP_LED_On (Led_TypeDef Led);
void BSP_LED_Off (Led_TypeDef Led);
void BSP_LED_Toggle (Led_TypeDef Led);

void BSP_PB_Init (Button_TypeDef Button, ButtonMode_TypeDef ButtonMode);
uint32_t BSP_PB_GetState (Button_TypeDef Button);

//{{{
#ifdef __cplusplus
  }
#endif
//}}}
