/* Board support for the model test, talking to registers directly like
 * ../capture/src/main.c (addresses from the STM32F407 reference manual, RM0090). */
#include "board.h"

#define REG(addr) (*(volatile uint32_t *)(addr))

#define RCC_CR        REG(0x40023800u)
#define RCC_PLLCFGR   REG(0x40023804u)
#define RCC_CFGR      REG(0x40023808u)
#define RCC_AHB1ENR   REG(0x40023830u)
#define FLASH_ACR     REG(0x40023C00u)
#define GPIOD_MODER   REG(0x40020C00u)
#define GPIOD_BSRR    REG(0x40020C18u)
#define SYST_CSR      REG(0xE000E010u)
#define SYST_RVR      REG(0xE000E014u)
#define SYST_CVR      REG(0xE000E018u)

static volatile uint32_t ticks_ms;

void SysTick_Handler(void) { ticks_ms++; }

uint32_t board_ms(void) { return ticks_ms; }

void board_delay_ms(uint32_t ms)
{
    uint32_t start = ticks_ms;
    while (ticks_ms - start < ms) { }
}

void led_set(uint8_t mask)
{
    uint32_t on = (uint32_t)(mask & 0xFu) << 12;
    uint32_t off = (~(uint32_t)mask & 0xFu) << 12;
    GPIOD_BSRR = on | (off << 16);
}

void board_init(void)
{
    /* 8 MHz crystal (HSE) on the Discovery board */
    RCC_CR |= (1u << 16);                              /* HSEON */
    while (!(RCC_CR & (1u << 17))) { }                 /* HSERDY */

    /* 5 wait states at 168 MHz / 3.3 V, prefetch + instruction and data caches on */
    FLASH_ACR = 5u | (1u << 8) | (1u << 9) | (1u << 10);

    /* PLL: 8 MHz / 8 * 336 / 2 = 168 MHz (PLLQ = 7 -> 48 MHz for USB later) */
    RCC_PLLCFGR = 8u | (336u << 6) | (0u << 16) | (1u << 22) | (7u << 24);
    RCC_CR |= (1u << 24);                              /* PLLON */
    while (!(RCC_CR & (1u << 25))) { }                 /* PLLRDY */

    /* AHB 168 MHz, APB1 /4 = 42 MHz, APB2 /2 = 84 MHz, switch to PLL */
    RCC_CFGR = (5u << 10) | (4u << 13) | 2u;
    while ((RCC_CFGR & (3u << 2)) != (2u << 2)) { }

    /* LEDs PD12-PD15 as outputs, all off */
    RCC_AHB1ENR |= (1u << 3);                          /* GPIOD clock */
    GPIOD_MODER = (GPIOD_MODER & ~(0xFFu << 24)) | (0x55u << 24);
    led_set(0);

    /* SysTick: 1 ms */
    SYST_RVR = CORE_HZ / 1000u - 1u;
    SYST_CVR = 0;
    SYST_CSR = 7u;                                     /* core clock, interrupt, enable */
}
