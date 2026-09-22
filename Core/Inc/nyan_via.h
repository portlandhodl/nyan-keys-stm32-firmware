/**
 * @file nyan_via.h
 * @brief VIA protocol support for Nyan Keys.
 *
 * Implements the VIA raw HID protocol (32 byte reports) used by the VIA
 * configurator application. Provides a dynamic keymap persisted to the
 * onboard 24xx EEPROM (bank 0, ADDR_VIA_KEYMAP).
 *
 * Protocol reference: the-via/app (keyboard-api.ts) and QMK quantum/via.c
 * Reports are always 32 bytes, byte 0 is the command id, commands are
 * echoed back in the response followed by any result data.
 */

#ifndef NYAN_VIA_H
#define NYAN_VIA_H

#include <stdint.h>
#include <stdbool.h>

#include "nyan_keys.h"

#define VIA_PROTOCOL_VERSION    0x000B  /* VIA 11 - v11 keycode dictionary, v3 definitions */
#define VIA_FIRMWARE_VERSION    0x00000002UL /* Reported via GET_KEYBOARD_VALUE/FIRMWARE_VERSION */
#define VIA_NUM_LAYERS          2       /* Base layer + FN layer */

#define VIA_KEYMAP_BYTES        (VIA_NUM_LAYERS * NUM_KEYS * 2)
#define VIA_KEYMAP_VERSION      0x02    /* Bump if the EEPROM keymap layout or keycode dictionary changes */
#define VIA_MAGIC_BYTE_0        0x56    /* 'V' */
#define VIA_MAGIC_BYTE_1        0x49    /* 'I' */

/* VIA/QMK keycode ranges (VIA v11 dictionary, protocol 11) */
#define VIA_KC_NO               0x0000
#define VIA_KC_TRNS             0x0001
#define VIA_KC_MUTE             0x00A8
#define VIA_KC_VOLU             0x00A9
#define VIA_KC_VOLD             0x00AA
#define VIA_QK_MODS             0x0100
#define VIA_QK_MODS_MAX         0x1FFF
#define VIA_QK_MOMENTARY        0x5220
#define VIA_QK_MOMENTARY_MAX    0x523F
#define VIA_KC_MO(layer)        (VIA_QK_MOMENTARY + (layer))

typedef enum {
    VIA_FAILURE,
    VIA_SUCCESS
} NyanViaReturn;

/**
 * @brief Initializes the VIA dynamic keymap.
 *
 * Loads the keymap from the EEPROM when the magic/version matches,
 * otherwise programs the factory default keymap to the EEPROM.
 * Must be called from main loop context (uses blocking EEPROM DMA reads).
 * @return NyanViaReturn success or failure.
 */
NyanViaReturn NyanViaInit(void);

/**
 * @brief Processes one pending VIA raw HID command if any.
 *
 * Call from the main loop. Fetches the pending OUT report, executes the
 * command (including EEPROM commits), sends the 32 byte response and
 * re-arms the OUT endpoint.
 * @return NyanViaReturn success or failure.
 */
NyanViaReturn NyanViaProcess(void);

/**
 * @brief Resolves the keycode for a key on a given layer from the dynamic keymap.
 * @param layer Layer number (0..VIA_NUM_LAYERS-1)
 * @param key Key index (0..NUM_KEYS-1, FPGA scan order)
 * @return 16 bit VIA keycode
 */
uint16_t NyanViaGetKeycode(uint8_t layer, uint8_t key);

#endif // NYAN_VIA_H
