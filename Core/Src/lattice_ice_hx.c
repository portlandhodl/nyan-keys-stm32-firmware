/**
 * Lattice ICE40HX NyanOS - Nyan Keys Driver
 * @author Reese Russell
 */

#include <stdlib.h>
#include <string.h>

#include "spi.h"
#include "24xx_eeprom.h"
#include "iceuncompr.h"
#include "lattice_ice_hx.h"
#include "nyan_eeprom_map.h"
#include "nyan_health.h"

static bool FPGAConfigDone(void)
{
    return HAL_GPIO_ReadPin(Nyan_FPGA_Config_Done_GPIO_Port, Nyan_FPGA_Config_Done_Pin) == GPIO_PIN_SET;
}

FPGAReturn FPGAInit(LatticeIceHX* fpga)
{
    // Every buffer handed to a DMA engine here is static (DTCM, never cached)
    // and the bitstream itself is only touched by the CPU, so the D-cache can
    // stay on.
    fpga->configured = false;
    if (FPGAGetBitstreamCompressedSize(fpga) != FPGA_SUCCESS || FPGAGetBitstreamData(fpga) != FPGA_SUCCESS) {
        free(fpga->p_bitstream_compressed);
        fpga->p_bitstream_compressed = NULL;
        return FPGA_FAILURE;
    }
    NyanWatchdogFeed();
    // First lets set the CRESET_B Low for more than 200ns and make sure the slave select is low
    HAL_GPIO_WritePin(SPI4_SS_GPIO_Port, SPI4_SS_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(FPGA_config_nrst_GPIO_Port, FPGA_config_nrst_Pin, GPIO_PIN_RESET);
    HAL_Delay(1); // Much longer than the needed 200ns but easy to implement
    HAL_GPIO_WritePin(FPGA_config_nrst_GPIO_Port, FPGA_config_nrst_Pin, GPIO_PIN_SET);
    HAL_Delay(3); // This lets the internal configuration memory clear
    // Now we need to pull the slave select high and send 8 dummy cycles
    HAL_GPIO_WritePin(SPI4_SS_GPIO_Port, SPI4_SS_Pin, GPIO_PIN_SET);
    const uint8_t lattice_dummy_bits = 0x00;
    HAL_SPI_Transmit(&hspi4, (uint8_t *)&lattice_dummy_bits, 1, 100);
    HAL_GPIO_WritePin(SPI4_SS_GPIO_Port, SPI4_SS_Pin, GPIO_PIN_RESET);
    // Uncompress and write the bitstream - This happens all in one file to keep the ram footprint low.
    WriteUncomprBitstream(&ice_uncompr, fpga->p_bitstream_compressed, fpga->bitstream_compressed_size);
    NyanWatchdogFeed();
    // We must free up the compressed memory used by the bitstream
    free(fpga->p_bitstream_compressed);
    fpga->p_bitstream_compressed = NULL;
    // Wait for the FPGA to raise c_done - bounded so NyanOS still boots when the
    // FPGA never configures (blank EEPROM, unpopulated/damaged FPGA). The main
    // loop retries configuration in the background when this times out.
    // CDONE is polled directly: the TIM1 IRQ only mirrors it into configured.
    uint32_t config_wait_start = HAL_GetTick();
    while(!FPGAConfigDone() && (HAL_GetTick() - config_wait_start) < FPGA_CONFIG_TIMEOUT_MS){
    }
    if(!FPGAConfigDone()) {
        // Give up for now - let the caller retry later.
        return FPGA_FAILURE;
    }
    // Send over the remaining dummy bytes 49 of them at minim, we will send 80 to be safe.
    for(uint8_t dummy_byte = 0; dummy_byte < 10; ++dummy_byte) {
        HAL_SPI_Transmit(&hspi4, (uint8_t *)&lattice_dummy_bits, 1, 100);
    }
    fpga->configured = true;
    return FPGA_SUCCESS;
}

FPGAReturn FPGAGetBitstreamData(LatticeIceHX* fpga)
{
    uint32_t size = fpga->bitstream_compressed_size;

    if (size == 0)
        return FPGA_FAILURE;

    // Reallocate the memory needed to hold the compressed bitstream
    uint8_t* temp_ptr = realloc(fpga->p_bitstream_compressed, size);
    if (temp_ptr == NULL) {
        // Free the original memory as its contents are not needed
        free(fpga->p_bitstream_compressed);
        fpga->p_bitstream_compressed = NULL; // Avoid dangling pointer
        return FPGA_FAILURE;
    }
    // Update the pointer as realloc was successful
    fpga->p_bitstream_compressed = temp_ptr;

    // Now lets start reading blocks of EEPROM in to the STM32F723's RAM
    for (uint32_t offset = 0; offset < size; offset += EEPROM_DRIVER_TX_BUF_SZ) {
        uint32_t chunk = size - offset;
        if (chunk > EEPROM_DRIVER_TX_BUF_SZ)
            chunk = EEPROM_DRIVER_TX_BUF_SZ;
        if (EepromRead(&nos_eeprom, true, (uint16_t)(ADDR_FPGA_BITSTREAM + offset), chunk) != EEPROM_SUCCESS)
            return FPGA_FAILURE;
        // Only the bytes that belong to the bitstream - the last chunk is usually partial
        memcpy(&fpga->p_bitstream_compressed[offset], nos_eeprom.rx_buf, chunk);
        NyanWatchdogFeed();
    }
    return FPGA_SUCCESS;
}

FPGAReturn FPGAGetBitstreamCompressedSize(LatticeIceHX* fpga)
{
    fpga->bitstream_compressed_size = 0;
    if (EepromRead(&nos_eeprom, false, ADDR_FPGA_BITSTREAM_LEN, SIZE_FPGA_BITSTREAM_LEN) != EEPROM_SUCCESS)
        return FPGA_FAILURE;
    // 4 little endian words, the size is the low half of the last one
    fpga->bitstream_compressed_size = (uint16_t)(nos_eeprom.rx_buf[12] | (nos_eeprom.rx_buf[13] << 8));
    // 0 = no bitstream (or an upload that never completed)
    return fpga->bitstream_compressed_size != 0 ? FPGA_SUCCESS : FPGA_FAILURE;
}
