#include "button.h"
#include "stm32f4xx_hal.h"

#define DEBOUNCE_MS 20

static bool stable;
static bool last_raw;
static uint32_t last_change_ms;

void button_init(void) {
  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitTypeDef gpio = {
    .Pin = GPIO_PIN_0,
    .Mode = GPIO_MODE_INPUT,
    .Pull = GPIO_NOPULL,  // the board has its own pull-down
  };
  HAL_GPIO_Init(GPIOA, &gpio);
}

bool button_poll(uint32_t now_ms) {
  bool raw = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET;
  if (raw != last_raw) {
    last_raw = raw;
    last_change_ms = now_ms;
  } else if (raw != stable && now_ms - last_change_ms >= DEBOUNCE_MS) {
    stable = raw;
  }
  return stable;
}
