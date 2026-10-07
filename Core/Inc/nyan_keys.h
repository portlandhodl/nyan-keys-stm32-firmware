/**
 * @file nyan_keys.h
 * @brief Header file for Nyan Keys FPGA IP driver.
 */

#ifndef NYAN_KEYS_H
#define NYAN_KEYS_H

#include <stdint.h>
#include <main.h>
#include "nyan_keys_frame.h"

#define NUM_KEYS 61 /**< Number of key state bits to be read from FPGA over SPI */
#define NUM_HID_KEYS 60 /**< Number of keys that could have any impact on the HID descriptor - We remove the FN Keys */
#define NUM_BOOT_KEYS 6 /**< Number of keys that can occupy the boot bytes compatible section of nyan keys*/
#define NUM_HYBRID_KEYS (NUM_HID_KEYS - NUM_BOOT_KEYS) /**< Number of keys that can occupy the extended scancodes bytes section of nyan keys for NRKO*/
#define NYAN_KEYS_STALL_US 50 /**< A partial frame with no SPI progress for this long is discarded (FPGA retries after 1ms) */

/**
 * @enum NyanKeysReturn
 * @brief Return types for Nyan Keys functions.
 */
typedef enum {
    NYAN_KEYS_FAILURE, /**< Indicates a failure in the operation */
    NYAN_KEYS_SUCCESS  /**< Indicates success in the operation */
} NyanKeysReturn;

/**
 * @struct NyanKeyBoardDescriptor
 * @brief Structure for USB Report.
 */
typedef struct __attribute__((packed)) {
    uint8_t MODIFIER;                     /**< Modifier keys state */
    uint8_t RESERVED;                     /**< Reserved byte */
    uint8_t BOOTKEYCODE[NUM_BOOT_KEYS];   /**< Boot key codes */
    uint8_t EXTKEYCODE[NUM_HYBRID_KEYS];  /**< Extended key codes */
} NyanKeyBoardDescriptor;

typedef enum {
    ESC, TAB, CAPS, L_SHIFT, LEFT_CTRL, NUM_1, L_WIN, L_ALT, Q, A, Z,
    NUM_2, W, S, X, C, D, K, I, NUM_8, L_ANGLE_BRACKET, L, O, NUM_9, R_ANGLE_BRACKET,
    COLON, P, NUM_0, QUESTION_MARK, L_SQUARE_BRACKET, R_WIN, FN, MINUS, QUOTE, MENU, R_SQUARE_BRACKET, PLUS, R_SHIFT,
    ENTER, SLASH, BACKSPACE, R_CTRL, E, NUM_3, V, F, R, NUM_4, SPACE, B, G, T, NUM_5,
    H, Y, NUM_6, N, J, U, NUM_7, M
} Keyboard60PercentKeys;


/**
 * @struct NyanKeys
 * @brief Structure to hold the state of keys.
 */
typedef struct {
    volatile bool started;                                     /**< Frame reception is armed and the FPGA is out of reset */
    volatile bool report_pending;                              /**< key_states changed and the HID report has not been queued yet */
    volatile uint8_t key_states[NYAN_KEYS_DATA_BYTES];         /**< Last validated key state from the FPGA (1 = released) */
    volatile uint8_t key_states_prv[NYAN_KEYS_DATA_BYTES];     /**< Key state the last HID report was built from */
    volatile uint32_t frames_good;                             /**< Frames received with a valid sync byte and CRC */
    volatile uint32_t frames_bad;                              /**< Frames rejected (sync/CRC mismatch or DMA error) */
    volatile uint32_t stall_resets;                            /**< Partial frames discarded by the stall watchdog */
    volatile bool super_key_disabled;                          /**< Disable Super Key (Win) key */
    uint8_t boot_byte_cnt;                                     /**< Track the number of boot compatible bytes used */
    uint8_t ext_byte_cnt;                                      /**M Track the number of extended report bytes used */
} NyanKeys;

/**
 * @brief Initializes and performs self-test on the Nyan Keys FPGA IP.
 * @param keys Pointer to NyanKeys structure.
 * @return NyanKeysReturn success or failure.
 */
NyanKeysReturn NyanKeysInit(NyanKeys* keys);

/**
 * @brief Arms frame reception (SPI2 slave + DMA) and releases the FPGA from
 *        reset. The FPGA sends the current key state right away.
 * @param keys Pointer to NyanKeys structure.
 * @return NyanKeysReturn success or failure.
 */
NyanKeysReturn NyanKeysStart(NyanKeys *keys);

/**
 * @brief Frame complete handler - call from DMA1_Stream3_IRQHandler.
 *
 * Re-arms reception (with an SPI2 reset so every frame starts bit aligned),
 * validates the frame and acks it. Calls NyanKeysFrameCallback() for each
 * good frame.
 */
void NyanKeysDmaIrqHandler(void);

/**
 * @brief Stall watchdog - call from the main loop. Discards a partially
 *        received frame (lost SCLK edge) so the FPGA's retry is received
 *        bit aligned.
 */
void NyanKeysService(void);

/**
 * @brief Called from interrupt context for every valid frame, after it has
 *        been acked. keys->key_states holds the new state.
 * @param keys Pointer to NyanKeys structure.
 */
void NyanKeysFrameCallback(NyanKeys *keys);

/**
 * @brief Main loop hook (implemented by the application) that queues a HID
 *        report left pending because the USB endpoint was busy.
 */
void NyanKeysSendPendingReport(void);

/**
 * @brief Retrieves the state of a specific key.
 * @param keys Pointer to NyanKeys structure.
 * @param key The key index to check.
 * @return True if the key is pressed, false otherwise.
 */
bool NyanGetKeyState(NyanKeys *keys, int key);

/**
 * @brief Builds and publishes the key states to the USB Extended Descriptor.
 * @param keys Pointer to NyanKeys structure.
 * @param desc Pointer to NyanKeyBoardDescriptor structure.
 * @return NyanKeysReturn success or failure.
 */
NyanKeysReturn NyanBuildHidReportFromKeyStates(NyanKeys *keys, volatile NyanKeyBoardDescriptor *desc);

/**
 * @brief Saves the state of the Super Key disablement to the onboard eeprom
 * @param eeprom pointer to the EEPROM driver (extern)
 * @param disabled boolean representing if the super key is disabled or not. 
 * @return NyanKeysReturn success or failure. 
 */
NyanKeysReturn NyanKeysWriteSuperDisableEEPROM(Eeprom24xx* eeprom, bool disabled);

/**
 * @brief Reads the state of the Super Key disablement to the onboard eeprom
 * @param eeprom pointer to the EEPROM driver (extern)
 * @return Super key enabled or disabled
 */
bool NyanKeysReadSuperDisableEEPROM(Eeprom24xx* eeprom);

#endif // NYAN_KEYS_H