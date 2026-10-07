/**
 * NyanOS (NOS) v0.01
 * Portland.HODL
 * Apache-2.0 License
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "24xx_eeprom.h"
#include "nyan_os.h"
#include "nyan_sha256.h"
#include "nyan_strings.h"

#include "nyan_health.h"
#include "nyan_keys.h"
#include "usbd_cdc_acm_if.h"

extern volatile NyanKeys nyan_keys;

/*
 * Console I/O. Nothing here runs in interrupt context except
 * NyanCdcRxFromIrq(): the USB IRQ only copies received bytes into the RX ring,
 * the main loop (NyanOsProcess) parses and executes commands and feeds the TX
 * ring to the CDC endpoint. No heap is used by the I/O path.
 */
#define NYAN_RX_RING_SZ 2048U /**< Power of two, >= 2 HS bulk packets */
#define NYAN_TX_RING_SZ 4096U /**< Power of two */
#define NYAN_UPLOAD_TIMEOUT_MS 10000U /**< Abort a direct buffer upload after this long without data */

static uint8_t nyan_rx_ring[NYAN_RX_RING_SZ];
static volatile uint32_t nyan_rx_head;   // Written by the USB IRQ
static volatile uint32_t nyan_rx_tail;   // Written by the main loop
static volatile bool nyan_rx_paused;     // OUT endpoint left un-armed until the ring has room (USB flow control)

static uint8_t nyan_tx_ring[NYAN_TX_RING_SZ];
static uint32_t nyan_tx_head;            // Main loop only
static uint32_t nyan_tx_tail;            // Main loop only
static uint8_t nyan_tx_packet[_NYAN_CDC_TX_MAX_LEN] __attribute__((aligned(4))); // USB DMA source - static so it is in DTCM

typedef enum {
    NYAN_UPLOAD_NONE,
    NYAN_UPLOAD_BITSTREAM,
    NYAN_UPLOAD_BITCOIN
} NyanUploadKind;

/** Direct buffer access (upload) state - main loop only */
static struct {
    NyanUploadKind kind;
    uint32_t size;
    uint32_t received;
    uint32_t last_rx_ms;
    uint16_t page_fill;
    uint16_t page_index;
    uint8_t page[EEPROM_DRIVER_TX_BUF_SZ];
    SHA256_CTX sha;
    uint8_t *bitcoin_dst;
    const uint8_t *bitcoin_msg;
} nyan_upload;

static bool nyan_last_char_cr;

static void NyanHandleInputByte(volatile NyanOS *nos, uint8_t c);
static bool NyanUploadByte(volatile NyanOS *nos, uint8_t c);
static void NyanUploadAbort(volatile NyanOS *nos, const uint8_t *reason);
static EepromReturn NyanWriteBitstreamLength(volatile NyanOS *nos, uint32_t size);

NyanReturn NyanOsInit(volatile NyanOS* nos)
{
    // Set the operational state
    nos->state = READY;
    nos->exe = NYAN_EXE_IDLE;

    // Init the driver pointers
    nos->eeprom = (Eeprom24xx*)&nos_eeprom;
    nos->nyan_bitcoin = &nyan_bitcoin;

    // Default init the OS vars
    nos->command_buffer_num_args = 0;
    nos->command_buffer_pos = 0;
    nos->exe_in_progress = false;
    nos->cdc_ch = _NYAN_CDC_CHANNEL;
    nos->connect_pending = false;
    nos->bytes_received = 0;
    nos->bytes_array_size = 0;

    // Default the OS Performance Counters
    nos->perf_keys_count_spi_calls_nxt = 0;

    // Manual Setting of the memory because of the volatile qualifier.
    ClearNyanCommandBuffer(nos);

    // Release the previous command's arguments (all NULL at boot)
    FreeNyanCommandArgs(nos);

    // Drop any unsent output and any upload in progress
    nyan_tx_head = nyan_tx_tail = 0;
    memset(&nyan_upload, 0, sizeof(nyan_upload));
    nyan_last_char_cr = false;

    return NOS_SUCCESS;
}

NyanReturn NyanWelcomeDisplay(volatile NyanOS *nos)
{
    if(nos->send_welcome_screen) {
        // Set to zero if the welcome screen is sent within the guarded period
        nos->send_welcome_screen = 0x00;
        // If the guard has expired send the Welcome Screen -> increment
        if(nos->send_welcome_screen_guard < 1) {
            nos->send_welcome_screen_guard++;
            NyanPrint(nos, (char*)&nyan_keys_welcome_text[0], strlen((char*)nyan_keys_welcome_text));
            NyanPrint(nos, (char*)&nyan_keys_path_text[0], strlen((char*)nyan_keys_path_text));
        }
    }

    return NOS_SUCCESS;
};

bool NyanCdcRxFromIrq(const uint8_t *buf, uint32_t len)
{
    uint32_t head = nyan_rx_head;

    // The endpoint is only armed while a full packet fits, so nothing is dropped here
    for (uint32_t i = 0; i < len && (head - nyan_rx_tail) < NYAN_RX_RING_SZ; ++i)
        nyan_rx_ring[head++ & (NYAN_RX_RING_SZ - 1U)] = buf[i];
    __DMB();
    nyan_rx_head = head;

    if ((NYAN_RX_RING_SZ - (head - nyan_rx_tail)) >= _NYAN_CDC_RX_PACKET)
        return true;
    nyan_rx_paused = true;
    return false;
}

void NyanCdcRxReset(void)
{
    // The CDC class re-arms the OUT endpoint itself on (re)configuration
    nyan_rx_paused = false;
}

/**
 * Re-arm the OUT endpoint once the main loop has made room in the RX ring.
 */
static void NyanCdcRxResume(volatile NyanOS *nos)
{
    if (!nyan_rx_paused || (NYAN_RX_RING_SZ - (nyan_rx_head - nyan_rx_tail)) < _NYAN_CDC_RX_PACKET)
        return;

    HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
    if (nyan_rx_paused && hUsbDevice.dev_state == USBD_STATE_CONFIGURED) {
        nyan_rx_paused = false;
        USBD_CDC_ReceivePacket(nos->cdc_ch, &hUsbDevice);
    }
    HAL_NVIC_EnableIRQ(OTG_HS_IRQn);
}

NyanReturn NyanPrint(volatile NyanOS *nos, char* data, size_t len)
{
    NyanReturn ret = NOS_SUCCESS;

    if (!nos || !data)
        return NOS_FAILURE;

    // Output that does not fit is dropped (e.g. no terminal draining the port)
    size_t space = NYAN_TX_RING_SZ - (nyan_tx_head - nyan_tx_tail);
    if (len > space) {
        len = space;
        ret = NOS_FAILURE;
    }
    for (size_t i = 0; i < len; ++i)
        nyan_tx_ring[nyan_tx_head++ & (NYAN_TX_RING_SZ - 1U)] = (uint8_t)data[i];

    return ret;
}

NyanReturn NyanCdcTX(volatile NyanOS* nos)
{
    uint32_t pending = nyan_tx_head - nyan_tx_tail;

    if (pending == 0 || nos->tx_inflight || hUsbDevice.dev_state != USBD_STATE_CONFIGURED)
        return NOS_FAILURE;

    if (pending > _NYAN_CDC_TX_MAX_LEN)
        pending = _NYAN_CDC_TX_MAX_LEN;
    for (uint32_t i = 0; i < pending; ++i)
        nyan_tx_packet[i] = nyan_tx_ring[(nyan_tx_tail + i) & (NYAN_TX_RING_SZ - 1U)];

    // The USB IRQ must not run the class while the transfer is being queued
    HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
    uint8_t result = CDC_Transmit(nos->cdc_ch, nyan_tx_packet, (uint16_t)pending);
    HAL_NVIC_EnableIRQ(OTG_HS_IRQn);

    if (result != USBD_OK)
        return NOS_FAILURE;
    nyan_tx_tail += pending;
    return NOS_SUCCESS;
}

void NyanOsProcess(volatile NyanOS* nos)
{
    // Terminal (re)connected (DTR raised) - reset the shell and greet
    if (nos->connect_pending) {
        if (nos->state == DIRECT_BUFFER_ACCESS)
            NyanUploadAbort(nos, nyan_keys_upload_aborted);
        NyanOsInit(nos);
        nos->send_welcome_screen = true;
    }

    if (nos->exe == NYAN_EXE_IDLE)
        NyanWelcomeDisplay(nos);

    // Parse input. Yield after a (slow) EEPROM page write so the rest of the
    // main loop keeps getting serviced during an upload.
    while (nyan_rx_tail != nyan_rx_head) {
        uint8_t c = nyan_rx_ring[nyan_rx_tail & (NYAN_RX_RING_SZ - 1U)];
        __DMB();
        nyan_rx_tail = nyan_rx_tail + 1U;
        if (nos->state == DIRECT_BUFFER_ACCESS) {
            if (NyanUploadByte(nos, c))
                break;
        } else {
            NyanHandleInputByte(nos, c);
        }
    }

    if (nos->state == DIRECT_BUFFER_ACCESS && (HAL_GetTick() - nyan_upload.last_rx_ms) >= NYAN_UPLOAD_TIMEOUT_MS)
        NyanUploadAbort(nos, nyan_keys_upload_timeout);

    NyanCdcRxResume(nos);
    NyanCdcTX(nos);
}

/**
 * Line editing for the READY state.
 */
static void NyanHandleInputByte(volatile NyanOS *nos, uint8_t c)
{
    const uint8_t del_char = 0x7F;
    const uint8_t backspace_char = 0x08;
    const uint8_t carriage_return = '\r';
    const uint8_t line_feed = '\n';
    bool after_cr = nyan_last_char_cr;

    nyan_last_char_cr = (c == carriage_return);

    if((c == backspace_char || c == del_char) && nos->command_buffer_pos > 0) {
        // Handle backspace
        uint8_t backspace_seq[3] = {backspace_char, ' ', backspace_char};
        NyanPrint(nos, (char*)&backspace_seq[0], sizeof(backspace_seq));
        --nos->command_buffer_pos;
        nos->command_buffer[nos->command_buffer_pos] = '\0';
    } else if(c == line_feed && after_cr) {
        // Second half of a CR LF line ending - already handled
    } else if(c == line_feed || c == carriage_return) {
        // Handle the action of executing a command by pressing enter
        NyanDecode(nos);
        ClearNyanCommandBuffer(nos);
        NyanPrint(nos, (char*)&nyan_keys_newline[0], strlen((char*)nyan_keys_newline));
        NyanExecute(nos);
    } else if(nos->command_buffer_pos >= _NYAN_CMD_BUF_LEN - 1) {
        // Handle out of command buffer space on next char
    } else if(c >= 0x20 && c <= 0x7E) {
        nos->command_buffer[nos->command_buffer_pos++] = c;
        NyanPrint(nos, (char*)&c, 1);
    }
}

/**
 * Direct buffer access: one received byte of an upload.
 * @return true if an EEPROM page was written (caller yields).
 */
static bool NyanUploadByte(volatile NyanOS *nos, uint8_t c)
{
    nyan_upload.last_rx_ms = HAL_GetTick();
    nos->bytes_received = ++nyan_upload.received;

    if (nyan_upload.kind == NYAN_UPLOAD_BITCOIN) {
        nyan_upload.bitcoin_dst[nyan_upload.received - 1U] = c;
        if (nyan_upload.received == nyan_upload.size) {
            NyanPrint(nos, (char*)nyan_upload.bitcoin_msg, strlen((char*)nyan_upload.bitcoin_msg));
            NyanPrint(nos, (char*)&nyan_keys_path_text[0], strlen((char*)nyan_keys_path_text));
            nyan_upload.kind = NYAN_UPLOAD_NONE;
            nos->state = READY;
        }
        return false;
    }

    nyan_upload.page[nyan_upload.page_fill++] = c;
    if (nyan_upload.page_fill < sizeof(nyan_upload.page) && nyan_upload.received < nyan_upload.size)
        return false;

    // A page is complete (or this is the tail of the bitstream) - write it
    sha256_update(&nyan_upload.sha, nyan_upload.page, nyan_upload.page_fill);
    EepromFlushTxBuff(nos->eeprom);
    memcpy(nos->eeprom->tx_buf, nyan_upload.page, nyan_upload.page_fill);
    if (EepromWrite(nos->eeprom, true, (uint16_t)(ADDR_FPGA_BITSTREAM + nyan_upload.page_index * EEPROM_DRIVER_TX_BUF_SZ), nyan_upload.page_fill) != EEPROM_SUCCESS) {
        NyanUploadAbort(nos, nyan_keys_eeprom_error);
        return true;
    }
    nyan_upload.page_index++;
    nyan_upload.page_fill = 0;

    if (nyan_upload.received < nyan_upload.size)
        return true;

    // Every page is in - only now publish the length, so an interrupted upload
    // never leaves a length pointing at a partial bitstream
    if (NyanWriteBitstreamLength(nos, nyan_upload.size) != EEPROM_SUCCESS) {
        NyanUploadAbort(nos, nyan_keys_eeprom_error);
        return true;
    }

    // Print the sha256 output for the user to verify their bitstream
    BYTE buf[SHA256_BLOCK_SIZE];
    char hexString[SHA256_BLOCK_SIZE * 2 + 1];
    sha256_final(&nyan_upload.sha, buf);
    for (int i = 0; i < SHA256_BLOCK_SIZE; i++) {
        sprintf(&hexString[i * 2], "%02x", buf[i]);
    }
    hexString[SHA256_BLOCK_SIZE * 2] = '\0';

    NyanPrint(nos, (char*)nyan_keys_write_bitstream_info_eeprom_write_completed, strlen((char*)nyan_keys_write_bitstream_info_eeprom_write_completed));
    NyanPrint(nos, (char*)&hexString[0], SHA256_BLOCK_SIZE * 2);
    NyanPrint(nos, (char*)&nyan_keys_newline[0], strlen((char*)nyan_keys_newline));
    NyanPrint(nos, (char*)&nyan_keys_path_text[0], strlen((char*)nyan_keys_path_text));

    nyan_upload.kind = NYAN_UPLOAD_NONE;
    nos->bytes_received = 0;
    nos->bytes_array_size = 0;
    nos->state = READY;

    // main() reloads the FPGA from the new bitstream
    nos_fpga.reconfigure_requested = true;
    return true;
}

static void NyanUploadAbort(volatile NyanOS *nos, const uint8_t *reason)
{
    NyanPrint(nos, (char*)reason, strlen((char*)reason));
    NyanPrint(nos, (char*)&nyan_keys_path_text[0], strlen((char*)nyan_keys_path_text));
    nyan_upload.kind = NYAN_UPLOAD_NONE;
    nos->bytes_received = 0;
    nos->bytes_array_size = 0;
    nos->state = READY;
}

/**
 * The length lives in the last of 4 little endian words at ADDR_FPGA_BITSTREAM_LEN.
 */
static EepromReturn NyanWriteBitstreamLength(volatile NyanOS *nos, uint32_t size)
{
    uint32_t size_array[4] = { 0x00, 0x00, 0x00, size };

    EepromFlushTxBuff(nos->eeprom);
    memcpy(nos->eeprom->tx_buf, size_array, sizeof(size_array));
    return EepromWrite(nos->eeprom, false, ADDR_FPGA_BITSTREAM_LEN, SIZE_FPGA_BITSTREAM_LEN);
}

NyanReturn NyanDecode(volatile NyanOS* nos)
{
    // First set the nos state to idle
    nos->exe = NYAN_EXE_IDLE;
    // Iterate over the available commands for a match to the input buffer
    for (uint8_t cmd_idx = 0; cmd_idx < _NYAN_NUM_COMMANDS; ++cmd_idx) {
        size_t command_len = strlen(nyan_commands[cmd_idx]);
        // Make sure we compare only the relevant part of the buffer1
        if (_NYAN_CMD_BUF_LEN >= command_len && memcmp((const char*)nos->command_buffer, nyan_commands[cmd_idx], command_len) == 0) {
            NyanDecodeArgs(nos);
            // We have found a match, set the current evaluation cursor to the command length, args will get parsed next.
            nos->exe = (NyanExe)cmd_idx;
            break;
        } else {
            nos->exe = NYAN_EXE_COMMAND_NOT_SUPPORTED;
        }
    }

    // If no command is matched, return some indication (e.g., NULL or a specific error string)
    return NOS_SUCCESS;
}

NyanReturn NyanExecute(volatile NyanOS* nos) {
    switch(nos->exe) {
        case NYAN_EXE_GET_INFO :
            NyanExeGetinfo(nos);
            NyanPrint(nos, (char*)&nyan_keys_newline[0], strlen((char*)nyan_keys_newline));
            NyanPrint(nos, (char*)&nyan_keys_path_text[0], strlen((char*)nyan_keys_path_text));
            // Not located inside NyanExeGetInfo because it's not atomic because of the EEPROM read.
            nos->exe = NYAN_EXE_IDLE;
            return NOS_SUCCESS;

        case NYAN_EXE_HELP :
            NyanExeHelp(nos);
            NyanPrint(nos, (char*)&nyan_keys_newline[0], strlen((char*)nyan_keys_newline));
            NyanPrint(nos, (char*)&nyan_keys_path_text[0], strlen((char*)nyan_keys_path_text));
            return NOS_SUCCESS;
        
        case NYAN_EXE_GET_PERF :
            NyanExeGetPerformanceStats(nos);
            NyanPrint(nos, (char*)&nyan_keys_newline[0], strlen((char*)nyan_keys_newline));
            NyanPrint(nos, (char*)&nyan_keys_path_text[0], strlen((char*)nyan_keys_path_text));
            return NOS_SUCCESS;

        case NYAN_EXE_SET_OWNER:
            if(NyanExeSetOwner(nos) == NOS_SUCCESS)
                NyanPrint(nos, (char*)&nyan_keys_set_owner_success[0], strlen((char*)nyan_keys_set_owner_success));
            else
                NyanPrint(nos, (char*)&nyan_keys_set_owner_failed[0], strlen((char*)nyan_keys_set_owner_failed));
            NyanPrint(nos, (char*)&nyan_keys_path_text[0], strlen((char*)nyan_keys_path_text));
            nos->exe = NYAN_EXE_IDLE;
            return NOS_SUCCESS;

        case NYAN_EXE_WRITE_BITSTREAM :
            // On success the shell is now in DIRECT_BUFFER_ACCESS - the prompt
            // comes back once the upload completes (or is aborted)
            if(NyanExeWriteFpgaBitstream(nos) != NOS_SUCCESS)
                NyanPrint(nos, (char*)&nyan_keys_path_text[0], strlen((char*)nyan_keys_path_text));
            nos->exe = NYAN_EXE_IDLE;
            return NOS_SUCCESS;

        case NYAN_EXE_BITCOIN_MINER_SET:
            if(NyanExeWriteBitcoinMiner(nos) != NOS_SUCCESS)
                NyanPrint(nos, (char*)&nyan_keys_path_text[0], strlen((char*)nyan_keys_path_text));
            nos->exe = NYAN_EXE_IDLE;
            return NOS_SUCCESS;

        case NYAN_EXE_DFU_MODE:
            NyanPrint(nos, (char*)&nyan_keys_enter_dfu_mode_reboot_warning[0], strlen((char*)nyan_keys_enter_dfu_mode_reboot_warning));
            NyanEnterDFUMode(nos);
            nos->exe = NYAN_EXE_IDLE;
            return NOS_SUCCESS;

        case NYAN_EXE_IDLE :
            return NOS_SUCCESS;

        case NYAN_EXE_COMMAND_NOT_SUPPORTED :
            NyanPrint(nos, (char*)&nyan_keys_unknown_command[0], strlen((char*)nyan_keys_unknown_command));
            NyanPrint(nos, (char*)&nyan_keys_newline[0], strlen((char*)nyan_keys_newline));
            NyanPrint(nos, (char*)&nyan_keys_path_text[0], strlen((char*)nyan_keys_path_text));
            nos->exe = NYAN_EXE_IDLE;
            return NOS_SUCCESS;

        default:
            // The execution state is out of bounds correct this.
            nos->exe = NYAN_EXE_IDLE;
            return NOS_FAILURE;
    }
}

NyanReturn NyanEnterDFUMode(volatile NyanOS* nos)
{
    nos->dfu_counter = 0;
    nos->dfu_mode = true;
    return NOS_SUCCESS;
}

NyanReturn NyanDecodeArgs(volatile NyanOS* nos)
{
    if (!nos) {
        return NOS_FAILURE;
    }

    // Destroy any previous allocated arguments
    FreeNyanCommandArgs(nos);

    nos->command_buffer[_NYAN_CMD_BUF_LEN] = '\0';
    const char *delimiter = " ";
    char *token = strtok((char *)nos->command_buffer, delimiter);

    int arg_count = 0;
    while (token != NULL) {
        if (arg_count < _NYAN_CMD_MAX_ARGS) {
            size_t tokenLength = strlen(token);
            nos->command_arg_buffer[arg_count] = malloc(tokenLength + 1); // Allocate memory for the argument
            if (nos->command_arg_buffer[arg_count] == NULL) {
                // Free any previously allocated memory
                for (int i = 0; i < arg_count; ++i) {
                    free(nos->command_arg_buffer[i]);
                }
                return NOS_FAILURE;
            }
            strcpy((char *)nos->command_arg_buffer[arg_count], token);
            arg_count++;
        }
        token = strtok(NULL, delimiter);
    }

    nos->command_buffer_num_args = arg_count;

    // Nullify the command buffer
    memset((void*)nos->command_buffer, 0, sizeof(nos->command_buffer));

    return NOS_SUCCESS;
}

NyanReturn NyanExeGetinfo(volatile NyanOS* nos)
{
    char owner[SIZE_BOARD_OWNER];
    char health[256];

    // We need to fetch the owners name from the eeprom
    if (EepromRead(nos->eeprom, false, ADDR_BOARD_OWNER, SIZE_BOARD_OWNER) == EEPROM_SUCCESS) {
        // Ensure data from EEPROM is null-terminated
        nos->eeprom->rx_buf[SIZE_BOARD_OWNER - 1] = '\0';
        strncpy(owner, (const char *)nos->eeprom->rx_buf, SIZE_BOARD_OWNER);
    } else {
        strcpy(owner, "<eeprom read failed>");
    }

    NyanPrint(nos, (char*)&nyan_keys_getinfo[0], strlen((char*)nyan_keys_getinfo));
    NyanPrint(nos, (char*)&nyan_keys_getinfo_owner[0], strlen((char*)nyan_keys_getinfo_owner));
    NyanPrint(nos, owner, strlen(owner));
    NyanPrint(nos, (char*)&nyan_keys_newline[0], strlen((char*)nyan_keys_newline));
    NyanPrint(nos, health, NyanHealthDescribe(health, sizeof(health)));

    return NOS_SUCCESS;
}

NyanReturn NyanExeSetOwner(volatile NyanOS* nos)
{
    if (!nos) {
        return NOS_FAILURE; // Handle null pointer
    }

    if (nos->command_buffer_num_args < 2) {
        return NOS_FAILURE; // Not enough args
    }

    size_t total_chars = 0;

    // Calculate total length needed, including spaces between arguments
    for (int i = 1; i < nos->command_buffer_num_args && nos->command_arg_buffer[i] != NULL; i++) {
        total_chars += strlen((char *)nos->command_arg_buffer[i]) + 1; // +1 for space or null terminator
    }

    // Since the size cant exceed 63 chars with null terminator
    if (total_chars > SIZE_BOARD_OWNER - nos->command_buffer_num_args - 1  || total_chars == 0) {
        return NOS_FAILURE; // Would overflow memory boundaries
    }

    // Allocate memory for the new owner name
    char* owners_name = (char*)malloc(SIZE_BOARD_OWNER);
    if (!owners_name) {
        return NOS_FAILURE; // Handle allocation failure
    }

    // Zero out the SIZE_BOARD_OWNER bytes
    for (int i = 0; i < SIZE_BOARD_OWNER; ++i) {
        owners_name[i] = '\0';
    }

    // Concatenate arguments with spaces
    char* current_pos = owners_name;
    for (int i = 1; i < nos->command_buffer_num_args && nos->command_arg_buffer[i] != NULL; i++) {
        strcpy(current_pos, (char *)nos->command_arg_buffer[i]);
        current_pos += strlen((char *)nos->command_arg_buffer[i]);

        // Add a space after each argument, except the last one
        if (i < nos->command_buffer_num_args - 1 && nos->command_arg_buffer[i + 1] != NULL) {
            *current_pos = ' ';
            current_pos++;
        }
    }

    // First lets clear out the TX buff
    if(EepromFlushTxBuff(nos->eeprom) != EEPROM_SUCCESS){
        return NOS_FAILURE;
    }

    // Second lets copy our new buffer over to the EEPROM driver
    for (int i = 0; i < SIZE_BOARD_OWNER; ++i){
        nos->eeprom->tx_buf[i]  = owners_name[i];
    }

    // Free up the allocated memory
    free(owners_name);

    // Write the name to the eeprom (blocking, retried on bus errors)
    if (EepromWrite(nos->eeprom, false, ADDR_BOARD_OWNER, SIZE_BOARD_OWNER) != EEPROM_SUCCESS)
        return NOS_FAILURE;

    return NOS_SUCCESS;
}

NyanReturn NyanExeWriteFpgaBitstream(volatile NyanOS* nos)
{
    // If we get here an are already in direct buffer access mode; FAIL
    if(nos->state == DIRECT_BUFFER_ACCESS)
        return NOS_FAILURE;
    // Set the state to NYAN_EXE_IDLE to show that we have ack'd the command
    nos->exe = NYAN_EXE_IDLE;

    // Now we need to convert the arg 1 into an int - skip arg 0 because that is the command.
    uint32_t size = 0;
    if(nos->command_buffer_num_args >= 2 && nos->command_arg_buffer[1] != NULL)
        size = strtoul((char *)nos->command_arg_buffer[1], NULL, 10);
    // Safety the size of the buffer to ensure that it doesn't exceed the size of a block
    if(size == 0 || size > 0xFFFF) {
        NyanPrint(nos, (char*)&nyan_keys_write_bitstream_error_size[0], strlen((char*)nyan_keys_write_bitstream_error_size));
        return NOS_FAILURE;
    }

    // Invalidate the stored length first: until every page is written the
    // EEPROM holds a mix of old and new data that must never be loaded
    if(NyanWriteBitstreamLength(nos, 0) != EEPROM_SUCCESS) {
        NyanPrint(nos, (char*)&nyan_keys_eeprom_error[0], strlen((char*)nyan_keys_eeprom_error));
        return NOS_FAILURE;
    }

    memset(&nyan_upload, 0, sizeof(nyan_upload));
    nyan_upload.kind = NYAN_UPLOAD_BITSTREAM;
    nyan_upload.size = size;
    nyan_upload.last_rx_ms = HAL_GetTick();
    sha256_init(&nyan_upload.sha);
    nos->bytes_array_size = size;
    nos->bytes_received = 0;

    // Enter direct buffer access mode - NyanOsProcess() streams the bytes to the EEPROM
    nos->state = DIRECT_BUFFER_ACCESS;

    return NOS_SUCCESS;
}

NyanReturn NyanExeWriteBitcoinMiner(volatile NyanOS* nos)
{
    // Set the state to NYAN_EXE_IDLE to show that we have ack'd the command
    nos->exe = NYAN_EXE_IDLE;

    // If we get here an are already in direct buffer access mode; FAIL
    if (nos->state == DIRECT_BUFFER_ACCESS)
        return NOS_FAILURE;

    NyanBitcoinHeader *header = &nos->nyan_bitcoin->block_header;
    const char *field = (nos->command_buffer_num_args >= 2 && nos->command_arg_buffer[1] != NULL)
                      ? (const char *)nos->command_arg_buffer[1] : "";
    uint8_t *dst;
    uint32_t size;
    const uint8_t *msg;

    if (strcmp(field, "version") == 0) {
        dst = header->version; size = sizeof(header->version);
        msg = nyan_keys_write_bitcoin_miner_block_version_success;
    } else if (strcmp(field, "prv-block-header-hash") == 0) {
        dst = header->prv_block_header_hash; size = sizeof(header->prv_block_header_hash);
        msg = nyan_keys_write_bitcoin_miner_prv_block_hash_success;
    } else if (strcmp(field, "merkle-root-hash") == 0) {
        dst = header->merkle_root_hash; size = sizeof(header->merkle_root_hash);
        msg = nyan_keys_write_bitcoin_miner_merkle_root_hash_success;
    } else if (strcmp(field, "timestamp") == 0) {
        dst = header->timestamp; size = sizeof(header->timestamp);
        msg = nyan_keys_write_bitcoin_miner_timestamp;
    } else if (strcmp(field, "nbits") == 0) {
        dst = header->n_bits; size = sizeof(header->n_bits);
        msg = nyan_keys_write_bitcoin_miner_nbits;
    } else if (strcmp(field, "nonce") == 0) {
        dst = header->nonce; size = sizeof(header->nonce);
        msg = nyan_keys_write_bitcoin_miner_nonce;
    } else {
        NyanPrint(nos, (char*)&nyan_keys_write_bitcoin_miner_failed_arg[0], strlen((char*)nyan_keys_write_bitcoin_miner_failed_arg));
        return NOS_FAILURE;
    }

    memset(&nyan_upload, 0, sizeof(nyan_upload));
    nyan_upload.kind = NYAN_UPLOAD_BITCOIN;
    nyan_upload.size = size;
    nyan_upload.last_rx_ms = HAL_GetTick();
    nyan_upload.bitcoin_dst = dst;
    nyan_upload.bitcoin_msg = msg;
    nos->bytes_array_size = size;
    nos->bytes_received = 0;

    // NyanOsProcess() fills the field byte by byte
    nos->state = DIRECT_BUFFER_ACCESS;

    return NOS_SUCCESS;
}

NyanReturn NyanExeHelp(volatile NyanOS* nos)
{
    nos->exe = NYAN_EXE_IDLE;
    NyanPrint(nos, (char*)&nyan_keys_help[0], strlen((char*)nyan_keys_help));

    return NOS_SUCCESS;
}

NyanReturn NyanExeGetPerformanceStats(volatile NyanOS* nos)
{
    // Set the state to NYAN_EXE_IDLE to show that we have ack'd the command
    nos->exe = NYAN_EXE_IDLE;
    NyanPrint(nos, (char*)&nyan_keys_getperf_line1[0], strlen((char*)nyan_keys_getperf_line1));
    NyanPrint(nos, (char*)&nyan_keys_getperf_line2[0], strlen((char*)nyan_keys_getperf_line2));
    // Now we need to print the stats for the keyboard in a way that means something to the user
    char keys_poll_cnt[11]; // 10 digits for 2^32-1 plus the terminator
    itoa(nos->perf_keys_count_spi_calls, keys_poll_cnt, 10);
    NyanPrint(nos, (char*)&nyan_keys_getperf_times_scanned[0], strlen((char*)nyan_keys_getperf_times_scanned));
    NyanPrint(nos, (char*)&keys_poll_cnt[0], strlen((char*)keys_poll_cnt));
    NyanPrint(nos, (char*)&nyan_keys_newline[0], strlen((char*)nyan_keys_newline));

    char counters[160];
    int len = snprintf(counters, sizeof(counters),
                       "Key frames good: %lu bad: %lu stalled: %lu\r\nEEPROM bus recoveries: %lu\r\n",
                       (unsigned long)nyan_keys.frames_good, (unsigned long)nyan_keys.frames_bad,
                       (unsigned long)nyan_keys.stall_resets, (unsigned long)nos->eeprom->bus_recoveries);
    if (len > 0)
        NyanPrint(nos, counters, ((size_t)len < sizeof(counters)) ? (size_t)len : sizeof(counters) - 1);

    return NOS_SUCCESS;
}

void FreeNyanCommandArgs(volatile NyanOS* nos)
{
    if (!nos) {
        return;
    }

    for (int i = 0; i < _NYAN_CMD_MAX_ARGS; i++) {
        if (nos->command_arg_buffer[i] != NULL) {
            free(nos->command_arg_buffer[i]);
            nos->command_arg_buffer[i] = NULL;
        }
    }
}

void ClearNyanCommandBuffer(volatile NyanOS* nos)
{
    nos->command_buffer_pos = 0;
    memset((void*)nos->command_buffer, 0, sizeof(nos->command_buffer));
};
