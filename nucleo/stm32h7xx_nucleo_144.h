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

#define NUCLEO_SPIx                              SPI1
#define NUCLEO_SPIx_CLK_ENABLE()                 __HAL_RCC_SPI1_CLK_ENABLE()
#define NUCLEO_SPIx_SCK_AF                       GPIO_AF5_SPI1
#define NUCLEO_SPIx_SCK_GPIO_PORT                GPIOA
#define NUCLEO_SPIx_SCK_PIN                      GPIO_PIN_5
#define NUCLEO_SPIx_SCK_GPIO_CLK_ENABLE()        __HAL_RCC_GPIOA_CLK_ENABLE()
#define NUCLEO_SPIx_SCK_GPIO_CLK_DISABLE()       __HAL_RCC_GPIOA_CLK_DISABLE()
#define NUCLEO_SPIx_MISO_MOSI_AF                 GPIO_AF5_SPI1
#define NUCLEO_SPIx_MISO_MOSI_GPIO_PORT          GPIOA
#define NUCLEO_SPIx_MISO_MOSI_GPIO_CLK_ENABLE()  __HAL_RCC_GPIOA_CLK_ENABLE()
#define NUCLEO_SPIx_MISO_MOSI_GPIO_CLK_DISABLE() __HAL_RCC_GPIOA_CLK_DISABLE()
#define NUCLEO_SPIx_MISO_PIN                     GPIO_PIN_6
#define NUCLEO_SPIx_MOSI_PIN                     GPIO_PIN_7
#define NUCLEO_SPIx_TIMEOUT_MAX                  1000
#define NUCLEO_SPIx_CS_GPIO_PORT                 GPIOD
#define NUCLEO_SPIx_CS_PIN                       GPIO_PIN_14
#define NUCLEO_SPIx_CS_GPIO_CLK_ENABLE()         __HAL_RCC_GPIOD_CLK_ENABLE()
#define NUCLEO_SPIx_CS_GPIO_CLK_DISABLE()        __HAL_RCC_GPIOD_CLK_DISABLE()

#define SPIx__CS_LOW()  HAL_GPIO_WritePin (NUCLEO_SPIx_CS_GPIO_PORT, NUCLEO_SPIx_CS_PIN, GPIO_PIN_RESET)
#define SPIx__CS_HIGH() HAL_GPIO_WritePin (NUCLEO_SPIx_CS_GPIO_PORT, NUCLEO_SPIx_CS_PIN, GPIO_PIN_SET)
#define SD_CS_LOW()     HAL_GPIO_WritePin (SD_CS_GPIO_PORT, SD_CS_PIN, GPIO_PIN_RESET)
#define SD_CS_HIGH()    HAL_GPIO_WritePin (SD_CS_GPIO_PORT, SD_CS_PIN, GPIO_PIN_SET)

#define SD_CS_PIN                                GPIO_PIN_14
#define SD_CS_GPIO_PORT                          GPIOF
#define SD_CS_GPIO_CLK_ENABLE()                  __HAL_RCC_GPIOF_CLK_ENABLE()
#define SD_CS_GPIO_CLK_DISABLE()                 __HAL_RCC_GPIOF_CLK_DISABLE()

void BSP_LED_Init(Led_TypeDef Led);
void BSP_LED_DeInit(Led_TypeDef Led);
void BSP_LED_On(Led_TypeDef Led);
void BSP_LED_Off(Led_TypeDef Led);
void BSP_LED_Toggle(Led_TypeDef Led);

void BSP_PB_Init(Button_TypeDef Button, ButtonMode_TypeDef ButtonMode);
void BSP_PB_DeInit(Button_TypeDef Button);
uint32_t BSP_PB_GetState(Button_TypeDef Button);

//{{{
#ifdef __cplusplus
  }
#endif
//}}}
