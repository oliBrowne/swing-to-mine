#include "stm32f4xx_hal.h"
#include "tusb.h"

void SysTick_Handler(void) {
  HAL_IncTick();
}

void OTG_FS_IRQHandler(void) {
  tud_int_handler(0);
}

void HardFault_Handler(void) {
  __disable_irq();
  while (1) {
  }
}
