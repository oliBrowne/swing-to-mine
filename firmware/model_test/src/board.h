/* Minimal board support for the model test: 168 MHz clock, LEDs, 1 ms tick. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CORE_HZ 168000000u

void board_init(void);              /* clocks (8 MHz crystal -> 168 MHz), flash cache, LEDs, SysTick */
void led_set(uint8_t mask);         /* bit 0 green PD12, 1 orange PD13, 2 red PD14, 3 blue PD15 */
uint32_t board_ms(void);
void board_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif
