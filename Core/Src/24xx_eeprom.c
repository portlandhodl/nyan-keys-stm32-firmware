/**
 * @auth: Portland.HODL
 * An 24xx EEPROM library designed around DMA access
 */

#include <string.h>

#include "i2c.h"
#include "24xx_eeprom.h"


EepromReturn EepromInit(Eeprom24xx* eeprom, bool a0, bool a1)   
{
    eeprom->a0 = a0;
    eeprom->a1 = a1;
    eeprom->tx_inflight = false;
    eeprom->rx_inflight = false;
    eeprom->error = false;
    eeprom->bus_recoveries = 0;

    memset((void*)eeprom->tx_buf, 0, sizeof(eeprom->tx_buf));
    memset((void*)eeprom->rx_buf, 0, sizeof(eeprom->rx_buf));

    return EEPROM_SUCCESS;
}

EepromReturn EepromFlushTxBuff(Eeprom24xx* eeprom)
{
    memset((void*)eeprom->tx_buf, 0, sizeof(eeprom->tx_buf));

    return EEPROM_SUCCESS;
}

uint8_t EepromCreateControlByte(Eeprom24xx* eeprom, bool read, bool b0)
{
    uint8_t ctrl_byte = EEPROM_CTRL_MASK_CODE;

    if(read)
        ctrl_byte += EEPROM_CTRL_MASK_RW;
    if(eeprom->a0)
        ctrl_byte += EEPROM_CTRL_MASK_A0;
    if(eeprom->a1)
        ctrl_byte += EEPROM_CTRL_MASK_A1;
    if(b0)
        ctrl_byte += EEPROM_CTRL_MASK_B0;

    return ctrl_byte;
}

/**
 * Wait for a DMA transaction started by the HAL. The I2C error callback sets
 * eeprom->error instead of the completion callback clearing the flag.
 */
static bool EepromWaitDone(Eeprom24xx* eeprom, volatile bool* inflight)
{
    uint32_t start = HAL_GetTick();
    while (*inflight) {
        if (eeprom->error || (HAL_GetTick() - start) >= EEPROM_XFER_TIMEOUT_MS)
            return false;
    }
    return !eeprom->error;
}

/**
 * Acknowledge polling: the 24xx NAKs its address until the internal write
 * cycle is complete.
 */
static bool EepromWaitWriteCycle(Eeprom24xx* eeprom, bool b0)
{
    uint32_t start = HAL_GetTick();
    do {
        if (HAL_I2C_IsDeviceReady(&hi2c1, EepromCreateControlByte(eeprom, false, b0), 1, 2) == HAL_OK)
            return true;
    } while ((HAL_GetTick() - start) < EEPROM_WRITE_CYCLE_MS);
    return false;
}

static void EepromBusDelay(void)
{
    // ~5us half period - well below the 24xx's 400KHz limit
    for (volatile uint32_t i = 0; i < 300U; ++i) {}
}

void EepromBusRecover(Eeprom24xx* eeprom)
{
    GPIO_InitTypeDef gpio = {0};

    // Releases the DMA streams and the peripheral (aborts anything in flight)
    HAL_I2C_DeInit(&hi2c1);
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // PB6 = SCL, PB7 = SDA, both open drain released high
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_SET);
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);
    EepromBusDelay();

    // Clock out whatever byte the slave is stuck in until it lets go of SDA
    for (int i = 0; i < 9 && HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) == GPIO_PIN_RESET; ++i) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
        EepromBusDelay();
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
        EepromBusDelay();
    }

    // STOP: SDA low -> high while SCL is high
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
    EepromBusDelay();
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
    EepromBusDelay();
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
    EepromBusDelay();
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
    EepromBusDelay();

    // Hand the pins back to the peripheral (MspInit re-links the DMA streams)
    MX_I2C1_Init();

    eeprom->tx_inflight = false;
    eeprom->rx_inflight = false;
    eeprom->error = false;
    eeprom->bus_recoveries++;
}

EepromReturn EepromWrite(Eeprom24xx* eeprom, bool b0, uint16_t eeprom_address, size_t len)
{
    if (len == 0 || len > EEPROM_DRIVER_TX_BUF_SZ)
        return EEPROM_FAILURE;

    for (int attempt = 0; attempt < EEPROM_MAX_ATTEMPTS; ++attempt) {
        eeprom->error = false;
        eeprom->tx_inflight = true;
        if (HAL_I2C_Mem_Write_DMA(&hi2c1, EepromCreateControlByte(eeprom, false, b0), eeprom_address, I2C_MEMADD_SIZE_16BIT, (uint8_t*)&eeprom->tx_buf[0], len) == HAL_OK &&
            EepromWaitDone(eeprom, &eeprom->tx_inflight) &&
            EepromWaitWriteCycle(eeprom, b0)) {
            return EEPROM_SUCCESS;
        }
        EepromBusRecover(eeprom);
    }
    return EEPROM_FAILURE;
}

EepromReturn EepromRead(Eeprom24xx* eeprom, bool b0, uint16_t eeprom_address, size_t len)
{
    if (len == 0 || len > EEPROM_DRIVER_RX_BUF_SZ)
        return EEPROM_FAILURE;

    for (int attempt = 0; attempt < EEPROM_MAX_ATTEMPTS; ++attempt) {
        memset((void*)eeprom->rx_buf, 0, sizeof(eeprom->rx_buf));
        eeprom->error = false;
        eeprom->rx_inflight = true;
        if (HAL_I2C_Mem_Read_DMA(&hi2c1, EepromCreateControlByte(eeprom, true, b0), eeprom_address, I2C_MEMADD_SIZE_16BIT, (uint8_t*)&eeprom->rx_buf[0], len) == HAL_OK &&
            EepromWaitDone(eeprom, &eeprom->rx_inflight)) {
            return EEPROM_SUCCESS;
        }
        EepromBusRecover(eeprom);
    }
    return EEPROM_FAILURE;
}
