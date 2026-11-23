/* Startup for the controller firmware: ../../model_test/src/startup.c plus a
 * full interrupt table, because USB needs its interrupt (OTG_FS = IRQ 67). */
#include <stdint.h>

extern int main(void);
void SysTick_Handler(void);      /* ../../model_test/src/board.c */
void OTG_FS_IRQHandler(void);    /* hid_out.c */

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern void (*__preinit_array_start[])(void), (*__preinit_array_end[])(void);
extern void (*__init_array_start[])(void), (*__init_array_end[])(void);

#define SCB_CPACR (*(volatile uint32_t *)0xE000ED88u)

void Reset_Handler(void)
{
    SCB_CPACR |= (0xFu << 20);                       /* FPU on (built hard-float) */
    __asm volatile("dsb\n isb");

    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;           /* .data from flash */
    for (dst = &_sbss; dst < &_ebss;) *dst++ = 0;    /* zero .bss */

    for (void (**f)(void) = __preinit_array_start; f < __preinit_array_end; ++f) (*f)();
    for (void (**f)(void) = __init_array_start; f < __init_array_end; ++f) (*f)();

    main();
    while (1) { }
}

/* Anything unexpected lands here: all four LEDs on, hang (a debugger shows where). */
void Default_Handler(void)
{
    *(volatile uint32_t *)0x40020C18u = 0xFu << 12;  /* GPIOD BSRR: PD12-15 on */
    while (1) { }
}

void _init(void) { }
void _fini(void) { }

#define N_IRQ 82                                     /* STM32F407 peripheral interrupts */
#define OTG_FS_IRQ 67

__attribute__((section(".isr_vector"), used))
const void *const vector_table[16 + N_IRQ] = {
    [0] = &_estack,
    [1] = Reset_Handler,
    [2] = Default_Handler,     /* NMI */
    [3] = Default_Handler,     /* HardFault */
    [4] = Default_Handler,     /* MemManage */
    [5] = Default_Handler,     /* BusFault */
    [6] = Default_Handler,     /* UsageFault */
    [11] = Default_Handler,    /* SVCall */
    [12] = Default_Handler,    /* DebugMonitor */
    [14] = Default_Handler,    /* PendSV */
    [15] = SysTick_Handler,    /* 1 ms tick */
    [16 ... 16 + N_IRQ - 1] = Default_Handler,
    [16 + OTG_FS_IRQ] = OTG_FS_IRQHandler,
};
