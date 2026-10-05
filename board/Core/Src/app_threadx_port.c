/* Hand-written board adapter for Eclipse ThreadX GNU Cortex-M4. */
#include "main.h"
#include "tx_api.h"

extern void *_tx_thread_system_stack_ptr;
extern void *_tx_initialize_unused_memory;
extern uint32_t g_pfnVectors[];
extern uint8_t _end;
extern void _tx_timer_interrupt(void);

void _tx_initialize_low_level(void)
{
    __disable_irq();
    _tx_initialize_unused_memory = &_end; /* Application uses static storage only. */
    _tx_thread_system_stack_ptr = (void *)(uintptr_t)g_pfnVectors[0];
    SCB->VTOR = (uint32_t)(uintptr_t)g_pfnVectors;
    /* GNU Cortex-M4 scheduler supplies PendSV_Handler and saves FP context. */
    NVIC_SetPriority(PendSV_IRQn, 15);
    NVIC_SetPriority(SVCall_IRQn, 15);
    NVIC_SetPriority(SysTick_IRQn, 4);
    SysTick->LOAD = SystemCoreClock / TX_TIMER_TICKS_PER_SECOND - 1U;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
    __DSB();
    __ISB();
    /* First ThreadX schedule enables interrupts; HAL tick remains on TIM6. */
}

void app_rtos_tick(void) { _tx_timer_interrupt(); }
