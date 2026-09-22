/**
 * @file lattice_ice_hx.h
 * @brief Header file for Lattice ICE40HX FPGA configuration and management.
 * 
 * This file contains the declarations for the LatticeIceHX type and functions
 * for initializing and managing the Lattice ICE40HX FPGA.
 */

#ifndef LATTICE_ICE_HX_H
#define LATTICE_ICE_HX_H

#include <stdint.h>
#include <main.h>
#include "24xx_eeprom.h"
#include "iceuncompr.h"

// External references for EEPROM and uncompression module.
extern Eeprom24xx nos_eeprom;
extern Iceuncompr ice_uncompr;

/**
 * @brief Maximum time to wait for the FPGA to raise c_done during configuration.
 *
 * If the FPGA never configures (no bitstream in EEPROM, unpopulated or damaged
 * FPGA), FPGAInit must still return so the rest of NyanOS (USB, VIA, CDC
 * console) keeps running. The main loop retries configuration in the
 * background, so a slow or late FPGA still comes up.
 */
#define FPGA_CONFIG_TIMEOUT_MS 2000

/**
 * @enum FPGAReturn
 * @brief Enumerates the possible return values for FPGA operations.
 */
typedef enum {
    FPGA_FAILURE,
    FPGA_SUCCESS
} FPGAReturn;

/**
 * @struct LatticeIceHX
 * @brief Represents the state and data for an ICE40HX FPGA.
 * 
 * This structure holds information about the FPGA configuration status,
 * the size of the compressed bitstream, and a pointer to the bitstream data.
 */
typedef struct {
    bool configured;
    uint16_t bitstream_compressed_size;
    uint8_t* p_bitstream_compressed;
} LatticeIceHX;

/**
 * @brief Initializes the FPGA.
 * 
 * This function performs the initial configuration of the FPGA. It disables
 * the DCache, fetches and uncompresses the bitstream from EEPROM, and writes
 * it to the FPGA.
 * 
 * @param fpga Pointer to an LatticeIceHX structure.
 * @return FPGAReturn Indicates the success or failure of the operation.
 */
FPGAReturn FPGAInit(LatticeIceHX* fpga);

/**
 * @brief Fetches the compressed bitstream data for the FPGA from EEPROM.
 * 
 * This function reads the bitstream data from the EEPROM in chunks and
 * writes it to a buffer.
 * 
 * @param fpga Pointer to an LatticeIceHX structure.
 * @return FPGAReturn Indicates the success or failure of the operation.
 */
FPGAReturn FPGAGetBitstreamData(LatticeIceHX* fpga);

/**
 * @brief Obtains the size of the compressed bitstream from EEPROM.
 * 
 * This function reads the size of the compressed bitstream stored in the
 * EEPROM and updates the LatticeIceHX structure.
 * 
 * @param fpga Pointer to an LatticeIceHX structure.
 * @return FPGAReturn Indicates the success or failure of the operation.
 */
FPGAReturn FPGAGetBitstreamCompressedSize(LatticeIceHX* fpga);

#endif // LATTICE_ICE_HX_H
