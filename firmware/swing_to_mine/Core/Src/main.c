// Swing to Mine firmware.
// Current state: M3 USB HID test. The board enumerates as a keyboard + mouse on
// the micro-USB OTG port (CN5); holding the blue button holds a left click and
// lights the blue LED.
#include "stm32f4xx_hal.h"
#include "tusb.h"
#include "button.h"
#include "hid_out.h"

// Discovery LEDs on port D
#define LED_GREEN  GPIO_PIN_12
#define LED_ORANGE GPIO_PIN_13
#define LED_RED    GPIO_PIN_14
#define LED_BLUE   GPIO_PIN_15

static void clock_init(void);
static void gpio_init(void);
static void usb_init(void);

// Click source for the test. Later the mode manager and gesture decoder decide
// the buttons; only this function needs to change.
static uint8_t test_mouse_buttons(uint32_t now_ms) {
  return button_poll(now_ms) ? HID_BTN_LEFT : 0;
}

int main(void) {
  HAL_Init();
  clock_init();
  gpio_init();
  button_init();
  usb_init();

  tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
  tusb_init(0, &dev_init);

  uint32_t last_ms = 0;
  while (1) {
    tud_task();

    uint32_t now = HAL_GetTick();
    if (now != last_ms) {  // 1 kHz, matches the HID poll rate
      last_ms = now;
      hid_state_t out = {.buttons = test_mouse_buttons(now)};
      hid_send(&out);
      HAL_GPIO_WritePin(GPIOD, LED_BLUE, out.buttons ? GPIO_PIN_SET : GPIO_PIN_RESET);
      HAL_GPIO_WritePin(GPIOD, LED_GREEN, tud_mounted() ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }

    hid_out_task();
  }
}

// HSE 8 MHz -> PLL: SYSCLK 168 MHz, USB 48 MHz, APB1 42 MHz, APB2 84 MHz
static void clock_init(void) {
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitTypeDef osc = {
    .OscillatorType = RCC_OSCILLATORTYPE_HSE,
    .HSEState = RCC_HSE_ON,
    .PLL = {
      .PLLState = RCC_PLL_ON,
      .PLLSource = RCC_PLLSOURCE_HSE,
      .PLLM = 8,
      .PLLN = 336,
      .PLLP = RCC_PLLP_DIV2,
      .PLLQ = 7,
    },
  };
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) while (1) {}

  RCC_ClkInitTypeDef clk = {
    .ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2,
    .SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK,
    .AHBCLKDivider = RCC_SYSCLK_DIV1,
    .APB1CLKDivider = RCC_HCLK_DIV4,
    .APB2CLKDivider = RCC_HCLK_DIV2,
  };
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) while (1) {}
}

static void gpio_init(void) {
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  GPIO_InitTypeDef gpio = {
    .Pin = LED_GREEN | LED_ORANGE | LED_RED | LED_BLUE,
    .Mode = GPIO_MODE_OUTPUT_PP,
    .Pull = GPIO_NOPULL,
    .Speed = GPIO_SPEED_FREQ_LOW,
  };
  HAL_GPIO_Init(GPIOD, &gpio);

  // PC0 enables the OTG port's 5 V output switch (active low). We're a device,
  // so keep it off.
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_SET);
  gpio.Pin = GPIO_PIN_0;
  HAL_GPIO_Init(GPIOC, &gpio);
}

// OTG FS on CN5: PA11 = D-, PA12 = D+, PA9 = VBUS sense
static void usb_init(void) {
  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitTypeDef gpio = {
    .Pin = GPIO_PIN_11 | GPIO_PIN_12,
    .Mode = GPIO_MODE_AF_PP,
    .Pull = GPIO_NOPULL,
    .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
    .Alternate = GPIO_AF10_OTG_FS,
  };
  HAL_GPIO_Init(GPIOA, &gpio);

  gpio.Pin = GPIO_PIN_9;
  gpio.Mode = GPIO_MODE_INPUT;
  HAL_GPIO_Init(GPIOA, &gpio);

  __HAL_RCC_USB_OTG_FS_CLK_ENABLE();

  tud_configure_dwc2_t cfg = CFG_TUD_CONFIGURE_DWC2_DEFAULT;
  cfg.vbus_sensing = true;
  tud_configure(0, TUD_CFGID_DWC2, &cfg);
}
