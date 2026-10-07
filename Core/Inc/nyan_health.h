/**
 * @file nyan_health.h
 * @brief Independent watchdog, reset cause and fault capture for NyanOS.
 *
 * Fault handlers and Error_Handler() record what happened in a RAM section
 * that the startup code does not clear, then reset. After the reset the
 * record and the reset cause are reported by the getinfo command.
 */

#ifndef NYAN_HEALTH_H
#define NYAN_HEALTH_H

#include <stddef.h>
#include <stdint.h>
#include <main.h>

#define NYAN_WATCHDOG_TIMEOUT_MS 4000 /**< IWDG timeout at the nominal 32KHz LSI */

/**
 * @enum NyanFaultType
 * @brief Source of a recorded fault.
 */
typedef enum {
    NYAN_FAULT_NONE,
    NYAN_FAULT_HARD,
    NYAN_FAULT_MEMMANAGE,
    NYAN_FAULT_BUS,
    NYAN_FAULT_USAGE,
    NYAN_FAULT_NMI,
    NYAN_FAULT_ERROR_HANDLER
} NyanFaultType;

/**
 * @brief Starts the independent watchdog (frozen while the core is halted
 *        by a debugger) and latches the reset cause. Call once at boot.
 */
void NyanHealthInit(void);

/**
 * @brief Reloads the independent watchdog.
 */
static inline void NyanWatchdogFeed(void)
{
    IWDG->KR = 0xAAAAU;
}

/**
 * @brief Records a fault and resets. Called by the fault handler shims with
 *        the exception stack frame (NULL from Error_Handler()).
 * @param frame Stacked r0-r3, r12, lr, pc, xpsr of the faulting context.
 * @param type What faulted.
 * @param lr Caller address, used when there is no exception frame.
 */
void NyanFaultRecord(uint32_t *frame, uint32_t type, uint32_t lr) __attribute__((noreturn));

/**
 * @brief Formats the reset cause and the last recorded fault.
 * @param buf Output buffer.
 * @param len Size of buf.
 * @return Number of characters written (excluding the terminator).
 */
size_t NyanHealthDescribe(char *buf, size_t len);

/**
 * @brief Body for a naked fault handler: passes the active stack pointer
 *        (the exception frame) to NyanFaultRecord().
 */
#define NYAN_FAULT_HANDLER_BODY(type)          \
    __asm volatile(                            \
        "tst lr, #4            \n"             \
        "ite eq                \n"             \
        "mrseq r0, msp         \n"             \
        "mrsne r0, psp         \n"             \
        "mov r1, %0            \n"             \
        "mov r2, lr            \n"             \
        "b NyanFaultRecord     \n"             \
        : : "i" (type))

#endif // NYAN_HEALTH_H
