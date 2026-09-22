/**
 * Lattice ICE40HX NyanOS - Nyan Keys Driver
 * @author Reese Russell
 */

#include <stdlib.h>

#include "spi.h"
#include "24xx_eeprom.h"
#include "iceuncompr.h"
#include "lattice_ice_hx.h"

FPGAReturn FPGAInit(LatticeIceHX* fpga)
{
    SCB_DisableDCache();
    // This needs to be optimized, the current time to program is close to 5 seconds.
    fpga->configured = false;
    FPGAGetBitstreamCompressedSize(fpga);
    FPGAGetBitstreamData(fpga);
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
    // Wait for the FPGA to raise c_done - bounded so NyanOS still boots when the
    // FPGA never configures (blank EEPROM, unpopulated/damaged FPGA). The main
    // loop retries configuration in the background when this times out.
    uint32_t config_wait_start = HAL_GetTick();
    while(!fpga->configured && (HAL_GetTick() - config_wait_start) < FPGA_CONFIG_TIMEOUT_MS){
    }
    if(!fpga->configured) {
        // Give up for now - free resources and let the caller retry later.
        free(fpga->p_bitstream_compressed);
        fpga->p_bitstream_compressed = NULL;
        SCB_EnableDCache();
        return FPGA_FAILURE;
    }
    // Send over the remaining dummy bytes 49 of them at minim, we will send 80 to be safe.
    for(uint8_t dummy_byte = 0; dummy_byte < 10; ++dummy_byte) {
        HAL_SPI_Transmit(&hspi4, (uint8_t *)&lattice_dummy_bits, 1, 100);
    }
    // We must free up the compressed memory used by the bitstream
    free(fpga->p_bitstream_compressed);
    fpga->p_bitstream_compressed = NULL;
    SCB_EnableDCache();

    return FPGA_SUCCESS;
}

FPGAReturn FPGAGetBitstreamData(LatticeIceHX* fpga)
{
    // First lets work out the chunk logic
    if (fpga->bitstream_compressed_size == 0)
        return FPGA_FAILURE;
    
    // Chunk loading and tracking
    uint16_t chunks = fpga->bitstream_compressed_size / EEPROM_DRIVER_TX_BUF_SZ;
    chunks += fpga->bitstream_compressed_size % EEPROM_DRIVER_TX_BUF_SZ  == 0 ? 0 : 1;
    if(chunks == 0)
        return FPGA_FAILURE;
    
    // Reallocate the memory needed to hold the compressed bitstream
    uint8_t* temp_ptr = realloc(fpga->p_bitstream_compressed, fpga->bitstream_compressed_size);
    if (temp_ptr == NULL) {
        // Free the original memory as its contents are not needed
        free(fpga->p_bitstream_compressed);
        fpga->p_bitstream_compressed = NULL; // Avoid dangling pointer
        return FPGA_FAILURE;
    }

    // Update the pointer as realloc was successful
    fpga->p_bitstream_compressed = temp_ptr;

    // Now lets start reading blocks of EEPROM in to the STM32F723's RAM
    for (uint16_t blk = 0; blk < chunks; ++blk) {
        // Fetch 128 Bytes
        EepromRead(&nos_eeprom, true, ADDR_FPGA_BITSTREAM + blk * EEPROM_DRIVER_TX_BUF_SZ, EEPROM_DRIVER_TX_BUF_SZ);
        while(nos_eeprom.rx_inflight){}
        // Write them to the allocated compressed bitstream buffer
        for(uint8_t byte = 0; byte < EEPROM_DRIVER_TX_BUF_SZ; ++byte) {
            fpga->p_bitstream_compressed[blk * EEPROM_DRIVER_TX_BUF_SZ + byte] = nos_eeprom.rx_buf[byte];
        }
    }

    return FPGA_SUCCESS;
}

FPGAReturn FPGAGetBitstreamCompressedSize(LatticeIceHX* fpga)
{
    // We need to fetch the owners name from the eeprom - !!!FIXME!!! Polled
    EepromRead(&nos_eeprom, false, ADDR_FPGA_BITSTREAM_LEN, SIZE_FPGA_BITSTREAM_LEN);
    while(nos_eeprom.rx_inflight){}
    // Cast the eeprom as a pointer of shorts
    uint16_t *rx_buf = (uint16_t *)nos_eeprom.rx_buf;
    fpga->bitstream_compressed_size = rx_buf[SIZE_FPGA_BITSTREAM_LEN/2 - 2]; //Little Endian Cast? {0xFFxx} !!!FIXME!!! This will work but is not ideal

    return FPGA_SUCCESS;
}