// nyan_strings.c

#include <nyan_strings.h>

#define NOS_VERSION "0.01"

const uint8_t nyan_keys_welcome_text[] =
"Nyan Keys Operating System (NOS) V" NOS_VERSION "\r\n"
"Made by Portland.HODL\r\n"
"\r\n";

const uint8_t nyan_keys_newline[] = "\r\n";
const uint8_t nyan_keys_path_text[] = "NyanOS \xF0\x9F\x90\xB1 > ";

// COMMAND: help
const uint8_t nyan_keys_help[] =
"Nyan Keys Help Menu \xF0\x9F\x90\xB1 \r\n"
"\tgetinfo\r\n"
"\tgetperf\r\n"
"\tset-owner <name with spaces>\r\n"
"\twrite-bitstream <size in bytes>\r\n"
"\tbitcoin-miner-set <args | run with no args for help>\r\n";

// COMMAND: getinfo
const uint8_t nyan_keys_getinfo[] =
"Version: " NOS_VERSION "\r\n"
"Author: Portland.HODL\r\n"
"Built: "__DATE__ "\r\n";

const uint8_t nyan_keys_getinfo_owner[] = "Owner: "; // Make sure to newline after this is filled out from the EEPROM

// COMMAND: getperf
const uint8_t nyan_keys_getperf_line1[] = "Nyan Keys Performance Stats\r\n";
const uint8_t nyan_keys_getperf_line2[] = " ------------------------- \r\n";
const uint8_t nyan_keys_getperf_times_scanned[] = "Total Keyboard Scans 1s: ";

//COMMAND: set-owner
const uint8_t nyan_keys_set_owner_success[] = "Nyan Keys owner has been successfully set\r\n";
const uint8_t nyan_keys_set_owner_failed[] = "Failed to set the owner (name too long or EEPROM error)\r\n";

const uint8_t nyan_keys_eeprom_error[] = "EEPROM write failed, upload aborted.\r\n";
const uint8_t nyan_keys_upload_timeout[] = "\r\nNo data received for 10 seconds, upload aborted.\r\n";
const uint8_t nyan_keys_upload_aborted[] = "\r\nUpload aborted.\r\n";

const uint8_t nyan_keys_unknown_command[] = "Command not supported by NyanOS";

//COMMAND: write-bitstream
const uint8_t nyan_keys_write_bitstream_info_start[] = "ready\r\n";
const uint8_t nyan_keys_write_bitstream_info_eeprom_write_completed[] = "Write to Nyan EEPROM completed.\r\n";
const uint8_t nyan_keys_write_bitstream_info_success[] = "Nyan Keys FPGA bitstream has been written\r\n";
const uint8_t nyan_keys_write_bitstream_error_size[] = "Failed to parse bitstream length, size must be less than 65535 bytes.\r\n";
const uint8_t nyan_keys_write_bitstream_error_size_tx_busy[] = "Failed to write bitstream length, TX buffer is busy.\r\n";

// COMMAND: bitcoin-miner-set
const uint8_t nyan_keys_write_bitcoin_miner_failed_arg[] = 
"Failed to parse arg1 please use\r\n"
"\t - version\r\n"
"\t - prv-block-header-hash\r\n"
"\t - merkle-root-hash\r\n"
"\t - timestamp\r\n"
"\t - nbits\r\n"
"\t - nonce\r\n";

//COMMAND: bitcoin-miner-set version
const uint8_t nyan_keys_write_bitcoin_miner_block_version_success[] = "Nyan BitCoin Miner block template version set successfully.\r\n";

//COMMAND: bitcoin-miner-set prv-block-hash
const uint8_t nyan_keys_write_bitcoin_miner_prv_block_hash_success[] = "Nyan BitCoin Miner previous block hash set successfully.\r\n";

//COMMAND: bitcoin-miner-set merkle-root-hash
const uint8_t nyan_keys_write_bitcoin_miner_merkle_root_hash_success[] = "Nyan BitCoin Miner merkle root hash set successfully.\r\n";

//COMMAND: bitcoin-miner-set timestamp
const uint8_t nyan_keys_write_bitcoin_miner_timestamp[] = "Nyan BitCoin Miner timestamp set successfully.\r\n";

//COMMAND: bitcoin-miner-set nbits
const uint8_t nyan_keys_write_bitcoin_miner_nbits[] = "Nyan BitCoin Miner nbits set successfully.\r\n" ;

//COMMAND: bitcoin-miner-set nonce
const uint8_t nyan_keys_write_bitcoin_miner_nonce[] = "Nyan BitCoin Miner nonce set successfully.\r\n";

//COMMAND: dfu-mode
const uint8_t nyan_keys_enter_dfu_mode_reboot_warning[] = "Nyan Keys entering DFU mode and rebooting\r\n";
