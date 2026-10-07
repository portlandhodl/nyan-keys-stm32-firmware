/**
 * NyanOS health - watchdog, reset cause and fault capture
 * @author Reese Russell
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "nyan_health.h"

#define NYAN_FAULT_MAGIC 0x4E59414EU /* "NYAN" */

typedef struct {
    uint32_t magic;
    uint32_t count;  /**< Faults recorded since power on */
    uint32_t type;
    uint32_t pc;
    uint32_t lr;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t mmfar;
    uint32_t bfar;
} NyanFaultInfo;

// Survives system and watchdog resets (not zeroed by the startup code)
static NyanFaultInfo nyan_fault __attribute__((section(".noinit")));
static uint32_t nyan_reset_flags;

static const char *const nyan_fault_names[] = {
    "none", "HardFault", "MemManage", "BusFault", "UsageFault", "NMI", "Error_Handler"
};

void NyanHealthInit(void)
{
    // Latch and clear the reset cause
    nyan_reset_flags = RCC->CSR;
    RCC->CSR |= RCC_CSR_RMVF;

    if (nyan_fault.magic != NYAN_FAULT_MAGIC)
        memset(&nyan_fault, 0, sizeof(nyan_fault));

    // Keep the watchdog from biting while the core is halted by a debugger
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;

    // LSI (32KHz) / 64 = 500Hz
    IWDG->KR = 0xCCCCU;
    IWDG->KR = 0x5555U;
    IWDG->PR = IWDG_PR_PR_2;
    IWDG->RLR = (NYAN_WATCHDOG_TIMEOUT_MS / 2U) - 1U;
    while (IWDG->SR != 0U) {}
    NyanWatchdogFeed();
}

void NyanFaultRecord(uint32_t *frame, uint32_t type, uint32_t lr)
{
    __disable_irq();

    if (nyan_fault.magic != NYAN_FAULT_MAGIC) {
        memset(&nyan_fault, 0, sizeof(nyan_fault));
        nyan_fault.magic = NYAN_FAULT_MAGIC;
    }
    nyan_fault.count++;
    nyan_fault.type  = type;
    nyan_fault.pc    = frame ? frame[6] : lr;
    nyan_fault.lr    = frame ? frame[5] : lr;
    nyan_fault.cfsr  = SCB->CFSR;
    nyan_fault.hfsr  = SCB->HFSR;
    nyan_fault.mmfar = SCB->MMFAR;
    nyan_fault.bfar  = SCB->BFAR;
    __DSB();

    // All status LEDs on, then reset - continuous operation is critical
    GPIOD->BSRR = Nyan_Keys_LED0_Pin | Nyan_Keys_LED1_Pin | Nyan_Keys_LED2_Pin |
                  Nyan_Keys_LED3_Pin | Nyan_Keys_LED4_Pin;
    NVIC_SystemReset();
    while (1) {}
}

size_t NyanHealthDescribe(char *buf, size_t len)
{
    int n = snprintf(buf, len, "Reset cause:%s%s%s%s%s%s%s\r\n",
                     (nyan_reset_flags & RCC_CSR_PORRSTF)  ? " power-on" : "",
                     (nyan_reset_flags & RCC_CSR_BORRSTF)  ? " brown-out" : "",
                     (nyan_reset_flags & RCC_CSR_PINRSTF)  ? " pin" : "",
                     (nyan_reset_flags & RCC_CSR_SFTRSTF)  ? " software" : "",
                     (nyan_reset_flags & RCC_CSR_IWDGRSTF) ? " watchdog" : "",
                     (nyan_reset_flags & RCC_CSR_WWDGRSTF) ? " window-watchdog" : "",
                     (nyan_reset_flags & RCC_CSR_LPWRRSTF) ? " low-power" : "");
    if (n < 0 || (size_t)n >= len)
        return (n < 0) ? 0 : len - 1;

    if (nyan_fault.magic != NYAN_FAULT_MAGIC || nyan_fault.count == 0) {
        n += snprintf(buf + n, len - n, "Faults since power on: 0\r\n");
    } else {
        const char *name = nyan_fault.type < (sizeof(nyan_fault_names) / sizeof(nyan_fault_names[0]))
                         ? nyan_fault_names[nyan_fault.type] : "unknown";
        n += snprintf(buf + n, len - n,
                      "Faults since power on: %lu\r\n"
                      "Last fault: %s pc=0x%08lx lr=0x%08lx cfsr=0x%08lx hfsr=0x%08lx mmfar=0x%08lx bfar=0x%08lx\r\n",
                      (unsigned long)nyan_fault.count, name,
                      (unsigned long)nyan_fault.pc, (unsigned long)nyan_fault.lr,
                      (unsigned long)nyan_fault.cfsr, (unsigned long)nyan_fault.hfsr,
                      (unsigned long)nyan_fault.mmfar, (unsigned long)nyan_fault.bfar);
    }
    return ((size_t)n >= len) ? len - 1 : (size_t)n;
}

/**
 * Heap growth for newlib malloc - bounded by the end of SRAM so a large
 * allocation fails cleanly instead of running into other memory.
 */
void *_sbrk(ptrdiff_t incr)
{
    extern uint8_t end;
    extern uint8_t _heap_limit;
    static uint8_t *heap_end = NULL;
    uint8_t *prev;

    if (heap_end == NULL)
        heap_end = &end;
    if (heap_end + incr > &_heap_limit) {
        errno = ENOMEM;
        return (void *)-1;
    }
    prev = heap_end;
    heap_end += incr;
    return prev;
}
