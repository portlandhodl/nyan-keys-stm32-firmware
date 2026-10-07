#ifndef NYAN_OS_H
#define NYAN_OS_H

#include <stdint.h>
#include <main.h>
#include "24xx_eeprom.h"
#include "lattice_ice_hx.h"
#include "nyan_bitcoin.h"
#include "nyan_eeprom_map.h"

#include "usb_device.h"

#define _NYAN_WELCOME_GUARD_TIME 2 // Currently a multiple of TIM7 Period (.777 seconds)
#define _NYAN_CDC_CHANNEL 0
#define _NYAN_CDC_RX_BUF_SZ 512
#define _NYAN_CDC_TX_MAX_LEN 128
#define _NYAN_CDC_RX_PACKET 512 // CDC_DATA_HS_OUT_PACKET_SIZE - the OUT endpoint is armed only while this much fits
#define _NYAN_CMD_MAX_ARGS 10
#define _NYAN_CMD_BUF_LEN 128

#define _NYAN_EXE_CHAR '\n'

#define _NYAN_NUM_COMMANDS (sizeof(nyan_commands) / sizeof(nyan_commands[0]))

extern Eeprom24xx nos_eeprom;         // 24xx Based EEPROM
extern LatticeIceHX nos_fpga;         // Lattice ICE40HX4k FPGA driver access
extern NyanBitcoin nyan_bitcoin;      // Nyan Keys Background Bitcoin Miner
extern USBD_HandleTypeDef hUsbDevice; // USB Device for DFU Reset

static const char* const nyan_commands[] = {
    "help",
    "getinfo",
    "getperf",
    "write-bitstream",
    "set-owner",
    "bitcoin-miner-set",
    "dfu-mode"
};

typedef enum {
    NOS_FAILURE,
    NOS_SUCCESS
} NyanReturn;

/**
 * @enum NyanStates
 * @brief Enumeration of the possible states of NyanOS.
 *
 * This enumeration defines the various states that NyanOS can be in during its operation.
 * The states indicate whether the system is ready, not ready, or in a mode that allows
 * direct buffer access.
 */
typedef enum {
    NOT_READY,            /**< The system is not ready for operations. */
    READY,                /**< The system is ready and operational. */
    DIRECT_BUFFER_ACCESS  /**< The system is in a mode that allows direct buffer access. */
} NyanStates;

/**
 * @enum NyanExe
 * @brief Enumeration of executable commands in NyanOS.
 *
 * This enumeration defines the set of possible commands or operations that can be executed by the NyanOS.
 * It includes commands for retrieving information, writing bitstreams, setting the owner, configuring a Bitcoin miner,
 * handling unsupported commands, and an idle state.
 */
typedef enum {
    NYAN_EXE_HELP,                    /**< Execute command to display help messages */
    NYAN_EXE_GET_INFO,                /**< Execute command to get system information. */
    NYAN_EXE_GET_PERF,                /**< Execute command to get nyan keys performance info. */
    NYAN_EXE_WRITE_BITSTREAM,         /**< Execute command to write a bitstream to FPGA. */
    NYAN_EXE_SET_OWNER,               /**< Execute command to set the owner of the system. */
    NYAN_EXE_BITCOIN_MINER_SET,       /**< Execute command to configure the Bitcoin miner. */
    NYAN_EXE_DFU_MODE,                /**< Execute command to make nyan keys enter DFU Mode: Board version > .9e*/
    NYAN_EXE_COMMAND_NOT_SUPPORTED,   /**< Indicator for an unsupported or unrecognized command. */
    NYAN_EXE_IDLE                     /**< System is in an idle state, not currently executing any command. */
} NyanExe;

/**
 * @struct NyanString
 * @brief Structure for handling strings in NyanOS.
 *
 * This structure is used for managing strings within the NyanOS. It provides a way to handle 
 * dynamic strings with associated size information, facilitating easier management of string 
 * operations such as allocation, resizing, and deallocation.
 */
typedef struct {
    uint8_t* p_array; /**< Pointer to the character array representing the string. */
    size_t size;      /**< Size of the string, indicating the number of characters it contains. */
} NyanString;

typedef struct {
    uint16_t raw[2]; /**< Raw values from ADC */
    double temp;     /**< Temperature */
} NyanCPUTemp;

/**
 * @struct NyanOS
 * @brief Struct representing the state and configuration of the NyanOS.
 *
 * NyanOS is designed for handling various operations in the Nyan Keys hardware, 
 * including EEPROM and Bitcoin Miner management, command processing, and USB CDC communication.
 * It contains configuration flags, state variables, buffers for input and output, and pointers 
 * to driver instances.
 */
typedef struct {
    bool        send_welcome_screen;                    /**< Flag to indicate if the welcome screen should be sent. Initialized to false. */
    uint8_t     send_welcome_screen_guard;              /**< Timer value to prevent multiple welcome screens. */
    bool        dfu_mode;                               /**< Boolean representing if Nyan Keys should enter a DFU mode */
    bool        dfu_counter;                            /**< Number of counts to let capacitor charge up, to enable BOOT0 to go high */
    char        exe_char;                               /**< ASCII character that triggers command evaluation. */

    Eeprom24xx  *eeprom;                                /**< Pointer to NyanOS EEPROM driver. */
    NyanBitcoin *nyan_bitcoin;                          /**< Pointer to NyanOS Bitcoin Miner driver. */

    NyanStates  state;                                  /**< Current state of NyanOS. */
    NyanExe     exe;                                    /**< The program to be executed. */
    uint8_t     command_buffer[_NYAN_CMD_BUF_LEN + 1];  /**< Buffer storing user-inputted commands. */
    uint8_t     command_buffer_pos;                     /**< Position of the cursor in the command buffer. */
    uint8_t     command_buffer_num_args;                /**< Number of arguments in the last command. */
    bool        exe_in_progress;                        /**< Flag indicating if a program is being executed. */

    uint8_t     cdc_ch;                                 /**< Active CDC channel used. Should always be 0 for Nyan OS. */
    bool        tx_inflight;                            /**< A CDC IN transfer is queued (cleared by the USB IRQ). */
    bool        connect_pending;                        /**< Terminal raised DTR (set by the USB IRQ, handled by the main loop). */

    uint32_t    bytes_received;                         /**< Number of bytes received in Direct Buffer Mode. */
    uint32_t    bytes_array_size;                       /**< Number of bytes expected in Direct Buffer Mode. */
    uint8_t*    command_arg_buffer[_NYAN_CMD_MAX_ARGS]; /**< Buffers to store command arguments. */

    uint32_t    perf_keys_count_spi_calls;              /**< Current readable value of the number of SPI calls to KEYS IP over 1s */
    uint32_t    perf_keys_count_spi_calls_nxt;          /**< Next readable value of the number of SPI calls to KEYS IP over 1s */
    NyanCPUTemp perf_cpu_temp;                   /*** CPU ADC DMA Temperature storage */
} NyanOS;

/**
 * @brief Initializes the NyanOS system.
 * @param nos Pointer to the NyanOS struct.
 * @param eeprom Pointer to the EEPROM driver struct.
 * @return NyanReturn indicating success or failure.
 */
NyanReturn NyanOsInit(volatile NyanOS* nos);

/**
 * @brief Decodes the currently buffered command in NyanOS.
 * @param nos Pointer to the NyanOS struct.
 * @return NyanReturn indicating success or failure.
 */
NyanReturn NyanOsDecodeCommand(volatile NyanOS* nos);

/**
 * @brief USB IRQ: queue received CDC bytes for the main loop.
 * @param buf Received data.
 * @param len Number of bytes received.
 * @return true if the OUT endpoint may be re-armed now; false if the RX ring
 *         is full - the main loop re-arms it once it has drained (the host is
 *         NAKed meanwhile).
 */
bool NyanCdcRxFromIrq(const uint8_t *buf, uint32_t len);

/**
 * @brief USB IRQ: the CDC interface was (re)initialized and its OUT endpoint
 *        re-armed by the class.
 */
void NyanCdcRxReset(void);

/**
 * @brief Main loop: parses console input, executes commands, runs uploads
 *        and sends queued output.
 * @param nos Pointer to the NyanOS struct.
 */
void NyanOsProcess(volatile NyanOS* nos);

/**
 * @brief Displays a welcome message to the user.
 * @param nos Pointer to the NyanOS struct.
 * @return NyanReturn indicating success or failure.
 */
NyanReturn NyanWelcomeDisplay(volatile NyanOS* nos);

/**
 * @brief Queues output for the console (main loop context only). Output that
 *        does not fit in the TX ring is dropped.
 * @param nos Pointer to the NyanOS struct.
 * @param data Pointer to the data to be printed.
 * @param len Length of the data to be printed.
 * @return NyanReturn indicating success or failure.
 */
NyanReturn NyanPrint(volatile NyanOS* nos, char* data, size_t len);

/**
 * @brief Sends the next chunk of queued console output if the CDC IN
 *        endpoint is idle (main loop context only).
 * @param nos Pointer to the NyanOS struct.
 * @return NyanReturn indicating success or failure.
 */

NyanReturn NyanCdcTX(volatile NyanOS* nos);
/**
 * @brief Decodes, stages, and resets the current input buffer in NyanOS.
 * @param nos Pointer to the NyanOS struct.
 * @return NyanReturn indicating success or failure.
 */
NyanReturn NyanDecode(volatile NyanOS* nos);

/**
 * @brief Executes the current command in the NyanOS and resets the execution state.
 * @param nos Pointer to the NyanOS struct.
 * @return NyanReturn indicating success or failure.
 */
NyanReturn NyanExecute(volatile NyanOS* nos);

/**
 * @brief Takes the first parameter and sets that as the owner of the Nyan Keys board.
 */
NyanReturn NyanExeSetOwner(volatile NyanOS* nos);

/**
 * @brief Store up to _NYAN_CMD_MAX_ARGS in the NyanOS class
 */
NyanReturn NyanDecodeArgs(volatile NyanOS* nos);

/**
 * @brief Prints current information and stats of the NyanOS and associated hardware.
 * @param nos Pointer to the NyanOS struct.
 * @return NyanReturn always returns NOS_SUCCESS.
 */
NyanReturn NyanExeGetinfo(volatile NyanOS* nos);

/**
 * @brief Prints the help message for the user
 * @param nos Pointer to the NyanOS struct.
 * @return NyanReturn always return NOS_SUCCESS; 
 */
NyanReturn NyanExeHelp(volatile NyanOS* nos);

/**
 * @brief Prints NyanKeys Performance Statistics
 * @param nos Pointer to the NyanOS struct.
 * @return NyanReturn always return NOS_SUCCESS; 
 */
NyanReturn NyanExeGetPerformanceStats(volatile NyanOS* nos);

/**
 * @brief Writes the name of argument 1 to the owners name address slot.
 */
NyanReturn NyanExeSetOwner(volatile NyanOS* nos);

/**
 * @brief Starts an FPGA bitstream upload: the next <size> received bytes are
 *        streamed to the EEPROM in 128 byte pages by NyanOsProcess(), then
 *        the FPGA is reconfigured.
 */
NyanReturn NyanExeWriteFpgaBitstream(volatile NyanOS* nos);

/**
 * @brief Executes a command to write various Bitcoin miner related data in the Nyan Keys Operating System (NOS).
 *
 * @param nos A pointer to a volatile NyanOS structure.
 * @return NyanReturn Indicates success (NOS_SUCCESS) or failure (NOS_FAILURE) of the operation.
 *
 * This function is responsible for handling different commands related to Bitcoin mining operations
 * in the Nyan Keys Operating System. It begins by setting the execution state to NYAN_EXE_IDLE, indicating
 * that a command has been acknowledged.
 *
 * The function checks if the system is already in DIRECT_BUFFER_ACCESS mode. If so, it returns NOS_FAILURE,
 * indicating that it cannot proceed with direct buffer access operations already in progress.
 *
 * Depending on the command specified in `nos->command_arg_buffer[1]`, the function allocates a buffer of 
 * appropriate size to store incoming data. The supported commands and their corresponding data sizes are:
 * - "version": 4 bytes
 * - "prv-block-header-hash": 32 bytes
 * - "merkle-root-hash": 32 bytes
 * - "timestamp": 4 bytes
 * - "nbits": 4 bytes
 * - "nonce": 4 bytes
 * If the command is not recognized, the function returns NOS_FAILURE.
 *
 * The shell then enters DIRECT_BUFFER_ACCESS and NyanOsProcess() writes the next received bytes
 * straight into the matching field of `nyan_bitcoin->block_header`, prints a success message and returns
 * to READY. The upload is aborted if no data arrives for NYAN_UPLOAD_TIMEOUT_MS.
 */
NyanReturn NyanExeWriteBitcoinMiner(volatile NyanOS* nos);

/**
 * Clear and nullify the NyanOS command buffer
 */
void ClearNyanCommandBuffer(volatile NyanOS* nos);

/**
 * @brief Free the command args before creating new pointers
 */
void FreeNyanCommandArgs(volatile NyanOS* nos);


/**
 * @brief Pulls pin E0 high to charge capacitor to let Nyan Keys enter th DFU mode
 */
NyanReturn NyanEnterDFUMode(volatile NyanOS* nos);

#endif // NYAN_OS_H