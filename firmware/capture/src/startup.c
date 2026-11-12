/*
 * Minimal startup code for the STM32F407VG.
 *
 * When the chip powers on it reads the vector table at the start of flash:
 * the first entry is the starting stack pointer, the second is the address of
 * Reset_Handler. Reset_Handler copies initialized variables into RAM, zeroes
 * the rest, and calls main().
 */
#include <stdint.h>

extern int main(void);
void SysTick_Handler(void);   /* defined in main.c */

/* These symbols come from the linker script (stm32f407vg.ld). */
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;

void Reset_Handler(void)
{
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;      /* copy .data from flash to RAM */
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0; /* zero .bss */
    main();
    while (1) { }
}

/* Any interrupt we did not expect lands here. Hang so a debugger can see it. */
void Default_Handler(void)
{
    while (1) { }
}

/* Cortex-M4 core exceptions only: this program uses no peripheral interrupts. */
__attribute__((section(".isr_vector"), used))
const void *const vector_table[16] = {
    &_estack,          /* 0: initial stack pointer */
    Reset_Handler,     /* 1: reset */
    Default_Handler,   /* 2: NMI */
    Default_Handler,   /* 3: HardFault */
    Default_Handler,   /* 4: MemManage */
    Default_Handler,   /* 5: BusFault */
    Default_Handler,   /* 6: UsageFault */
    0, 0, 0, 0,        /* 7-10: reserved */
    Default_Handler,   /* 11: SVCall */
    Default_Handler,   /* 12: DebugMonitor */
    0,                 /* 13: reserved */
    Default_Handler,   /* 14: PendSV */
    SysTick_Handler,   /* 15: SysTick (1 ms tick) */
};
