#include "stm32h7xx_it.h"
#include "stm32h7xx_nucleo_144.h"

void NMI_Handler() {}
void MemManage_Handler() { while (1) { } }
void BusFault_Handler() { while (1) { } }
void UsageFault_Handler() { while (1) { } }
void SVC_Handler() { }
void DebugMon_Handler() { }
void PendSV_Handler() { }

void SysTick_Handler() {
  HAL_IncTick();
  }

void EXTI15_10_IRQHandler() {
  HAL_GPIO_EXTI_IRQHandler(USER_BUTTON_PIN);
  }
