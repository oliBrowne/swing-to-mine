/*
 * Startup code for the model test (STM32F407VG). Based on ../capture's
 * startup.c, with two additions the model needs:
 *   - turn on the FPU before anything else (the code is built hard-float);
 *   - run C++ constructors (ExecuTorch registers its kernels in them).
 */
#include <stdint.h>

extern int main(void);
void SysTick_Handler(void);      /* board.c */

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern void (*__preinit_array_start[])(void), (*__preinit_array_end[])(void);
extern void (*__init_array_start[])(void), (*__init_array_end[])(void);

#define SCB_CPACR (*(volatile uint32_t *)0xE000ED88u)

void Reset_Handler(void)
{
    SCB_CPACR |= (0xFu << 20);                       /* CP10 + CP11 full access = FPU on */
    __asm volatile("dsb\n isb");

    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;           /* copy .data from flash to RAM */
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;   /* zero .bss */

    for (void (**f)(void) = __preinit_array_start; f < __preinit_array_end; ++f) (*f)();
    for (void (**f)(void) = __init_array_start; f < __init_array_end; ++f) (*f)();

    main();
    while (1) { }
}

/* Any interrupt we did not expect lands here. Hang so a debugger can see it. */
void Default_Handler(void)
{
    while (1) { }
}

/* C library hook some toolchains call from the constructor loop; nothing to do. */
void _init(void) { }
void _fini(void) { }

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
