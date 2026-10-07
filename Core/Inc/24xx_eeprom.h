/**
 * @file 24xx_eeprom.h
 * @brief EEPROM control byte utility for 24XX EEPROM series.
 *
 * Provides utility functions for working with 24XX series EEPROM devices.
 * This includes initialization, read and write operations using DMA.
 */

#ifndef _24XX_EEPROM_H
#define _24XX_EEPROM_H

#include <stdint.h>
#include <stdbool.h>

// Definitions
#define EEPROM_DRIVER_TX_BUF_SZ  128     /**< Size of the transmit buffer. */
#define EEPROM_DRIVER_RX_BUF_SZ  1024    /**< Size of the receive buffer. */
#define EEPROM_CTRL_MASK_RW      0x01    /**< Read/Write control bit mask. */
#define EEPROM_CTRL_MASK_A0      0x02    /**< A0 address bit mask. */
#define EEPROM_CTRL_MASK_A1      0x04    /**< A1 address bit mask. */
#define EEPROM_CTRL_MASK_B0      0x08    /**< B0 address bit mask. */
#define EEPROM_CTRL_MASK_CODE    0xA0    /**< Control byte mask code for EEPROM. */
#define EEPROM_PAGE_SIZE         0x7F    /**< Maximum number of bytes in a single TX. */
#define EEPROM_MAX_ADDR_SIZE     0xFFFF  /**< Max address value for a single block (2^16-1). */
#define EEPROM_XFER_TIMEOUT_MS   50      /**< Max time for one DMA transfer (1KB @ 200KHz is ~46ms). */
#define EEPROM_WRITE_CYCLE_MS    10      /**< Max internal write cycle time (24xx datasheet: 5ms). */
#define EEPROM_MAX_ATTEMPTS      3       /**< Attempts per transaction, with a bus recovery between them. */

typedef enum {
    EEPROM_FAILURE, /**< Indicates a failure in EEPROM operation. */
    EEPROM_SUCCESS  /**< Indicates a successful EEPROM operation. */
} EepromReturn;

typedef struct {
    bool a0;
    bool a1;
    volatile bool tx_inflight;
    volatile bool rx_inflight;
    volatile bool error;      /**< Set by the I2C error callback; the transaction is retried after a bus recovery. */
    uint32_t bus_recoveries;  /**< Number of I2C bus recoveries performed. */
    uint8_t tx_buf[EEPROM_DRIVER_TX_BUF_SZ]; /**< Transmit buffer. */
    uint8_t rx_buf[EEPROM_DRIVER_RX_BUF_SZ]; /**< Receive buffer. */
} Eeprom24xx;

/**
 * @brief Initializes an EEPROM instance.
 *
 * Sets up the EEPROM structure and initializes internal states.
 *
 * @param eeprom Pointer to the Eeprom24xx structure.
 * @param a0 State of address line A0.
 * @param a1 State of address line A1.
 * @return EepromReturn Status of the initialization (success or failure).
 */
EepromReturn EepromInit(Eeprom24xx* eeprom, bool a0, bool a1);

/**
 * @brief Flushes the transmit buffer of the EEPROM.
 *
 * Clears the contents of the transmit buffer in preparation for new data.
 *
 * @param eeprom Pointer to the Eeprom24xx structure.
 * @return EepromReturn Status of the flush operation (success or failure).
 */
EepromReturn EepromFlushTxBuff(Eeprom24xx* eeprom);

/**
 * @brief Writes tx_buf[0..len) to the EEPROM and waits for the write cycle.
 *
 * Blocking, main loop context only (needs SysTick for its timeouts). The
 * write must not cross a 128 byte page. A failed transaction (bus error,
 * NAK, timeout) is retried after an I2C bus recovery.
 *
 * @param eeprom Pointer to the Eeprom24xx structure.
 * @param b0 State of address bit B0 for addressing.
 * @param eeprom_address The EEPROM address where data writing should begin.
 * @param len Number of bytes to read.
 * @return EepromReturn Status of the write operation (success or failure).
 */
EepromReturn EepromWrite(Eeprom24xx* eeprom, bool b0, uint16_t eeprom_address, size_t len);

/**
 * @brief Reads len bytes from the EEPROM into rx_buf.
 *
 * Blocking, main loop context only. Retried like EepromWrite().
 *
 * @param eeprom Pointer to the Eeprom24xx structure.
 * @param b0 State of address bit B0 for addressing.
 * @param eeprom_address The EEPROM address from where to start reading.
 * @param len Number of bytes to read.
 * @return EepromReturn Status of the read operation (success or failure).
 */
EepromReturn EepromRead(Eeprom24xx* eeprom, bool b0, uint16_t eeprom_address, size_t len);

/**
 * @brief Frees a hung I2C bus (a slave holding SDA low after an interrupted
 *        transfer) by clocking SCL until SDA is released, sends a STOP and
 *        re-initializes I2C1 and its DMA streams.
 *
 * @param eeprom Pointer to the Eeprom24xx structure.
 */
void EepromBusRecover(Eeprom24xx* eeprom);

#endif // _24XX_EEPROM_H