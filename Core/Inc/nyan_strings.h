// nyan_strings.h
#ifndef _NYAN_STRINGS
#define _NYAN_STRINGS

#include <stdint.h>

extern const uint8_t nyan_keys_welcome_text[];
extern const uint8_t nyan_keys_newline[];
extern const uint8_t nyan_keys_path_text[];

// COMMAND: help
extern const uint8_t nyan_keys_help[];

// COMMAND: getinfo
extern const uint8_t nyan_keys_getinfo[];
extern const uint8_t nyan_keys_getinfo_owner[]; // Make sure to newline after this is filled out from the EEPROM

// COMMAND: getperf
extern const uint8_t nyan_keys_getperf_line1[];
extern const uint8_t nyan_keys_getperf_line2[];
extern const uint8_t nyan_keys_getperf_times_scanned[];

// COMMAND: set-owner
extern const uint8_t nyan_keys_set_owner_success[];
extern const uint8_t nyan_keys_set_owner_failed[];

extern const uint8_t nyan_keys_eeprom_error[];
extern const uint8_t nyan_keys_upload_timeout[];
extern const uint8_t nyan_keys_upload_aborted[];

extern const uint8_t nyan_keys_unknown_command[];

// COMMAND: write-bitstream
extern const uint8_t nyan_keys_write_bitstream_info_start[];
extern const uint8_t nyan_keys_write_bitstream_info_eeprom_write_completed[];
extern const uint8_t nyan_keys_write_bitstream_info_success[];
extern const uint8_t nyan_keys_write_bitstream_error_size[];
extern const uint8_t nyan_keys_write_bitstream_error_size_tx_busy[];

// COMMAND: bitcoin-miner-set
extern const uint8_t nyan_keys_write_bitcoin_miner_failed_arg[];

// COMMAND: bitcoin-miner-set version
extern const uint8_t nyan_keys_write_bitcoin_miner_block_version_success[];

//COMMAND: bitcoin-miner-set prv-block-hash
extern const uint8_t nyan_keys_write_bitcoin_miner_prv_block_hash_success[];

//COMMAND: bitcoin-miner-set merkle-root-hash
extern const uint8_t nyan_keys_write_bitcoin_miner_merkle_root_hash_success[];

//COMMAND: bitcoin-miner-set timestamp
extern const uint8_t nyan_keys_write_bitcoin_miner_timestamp[];

//COMMAND: bitcoin-miner-set nbits
extern const uint8_t nyan_keys_write_bitcoin_miner_nbits[];

//COMMAND: bitcoin-miner-set nonce
extern const uint8_t nyan_keys_write_bitcoin_miner_nonce[];

//COMMAND: dfu-mode
extern const uint8_t nyan_keys_enter_dfu_mode_reboot_warning[];

#endif // _NYAN_STRINGS