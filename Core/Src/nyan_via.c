/**
 * @file nyan_via.c
 * @brief VIA protocol handler for Nyan Keys.
 *
 * Speaks the VIA raw HID protocol over the vendor defined HID interface
 * (see usbd_hid_raw.c) and maintains a 2 layer x 61 key dynamic keymap
 * persisted to the onboard 24xx EEPROM (bank 0 @ ADDR_VIA_KEYMAP).
 *
 * Keycodes are 16 bit, stored big-endian, layer-major - identical to QMK
 * dynamic keymap EEPROM layout so the VIA app buffer commands (0x12/0x13)
 * work with the standard offsets.
 *
 * All work is executed in main loop context (never in USB ISR context)
 * because keymap commits perform blocking I2C DMA EEPROM transactions.
 *
 * @author Nyan Keys
 */

#include <string.h>

#include "main.h"
#include "24xx_eeprom.h"
#include "nyan_eeprom_map.h"
#include "nyan_os.h"
#include "nyan_via.h"
#include "usb_hid_keys.h"
#include "usbd_hid_raw.h"

extern Eeprom24xx nos_eeprom;               // 24xx Based EEPROM driver
extern volatile NyanOS nos;                 // NyanOS - for the DFU jump flag
extern volatile NyanKeys nyan_keys;         // Live key states (matrix state query)
extern USBD_HandleTypeDef hUsbDevice;       // USB device handle

/** @brief VIA command ids (the-via/app keyboard-api.ts / QMK via.c) */
typedef enum {
    VIA_CMD_GET_PROTOCOL_VERSION            = 0x01,
    VIA_CMD_GET_KEYBOARD_VALUE              = 0x02,
    VIA_CMD_SET_KEYBOARD_VALUE              = 0x03,
    VIA_CMD_DYNAMIC_KEYMAP_GET_KEYCODE      = 0x04,
    VIA_CMD_DYNAMIC_KEYMAP_SET_KEYCODE      = 0x05,
    VIA_CMD_DYNAMIC_KEYMAP_RESET            = 0x06,
    VIA_CMD_CUSTOM_SET_VALUE                = 0x07,
    VIA_CMD_CUSTOM_GET_VALUE                = 0x08,
    VIA_CMD_CUSTOM_SAVE                     = 0x09,
    VIA_CMD_EEPROM_RESET                    = 0x0A,
    VIA_CMD_BOOTLOADER_JUMP                 = 0x0B,
    VIA_CMD_DYNAMIC_KEYMAP_MACRO_GET_COUNT  = 0x0C,
    VIA_CMD_DYNAMIC_KEYMAP_MACRO_GET_BUFFER_SIZE = 0x0D,
    VIA_CMD_DYNAMIC_KEYMAP_GET_LAYER_COUNT  = 0x11,
    VIA_CMD_DYNAMIC_KEYMAP_GET_BUFFER       = 0x12,
    VIA_CMD_DYNAMIC_KEYMAP_SET_BUFFER       = 0x13,
    VIA_CMD_UNHANDLED                       = 0xFF
} NyanViaCommandId;

/** @brief VIA keyboard value ids (sub commands of 0x02/0x03) */
typedef enum {
    VIA_KV_UPTIME                           = 0x01,
    VIA_KV_LAYOUT_OPTIONS                   = 0x02,
    VIA_KV_SWITCH_MATRIX_STATE              = 0x03,
    VIA_KV_FIRMWARE_VERSION                 = 0x04
} NyanViaKeyboardValueId;

/**
 * @brief Factory default keymap. Reproduces the layout that was previously
 * hardcoded in NyanBuildHidReportFromKeyStates() (base layer + FN layer).
 * Index order follows Keyboard60PercentKeys (FPGA scan order).
 */
static const uint16_t nyan_via_default_keymap[VIA_NUM_LAYERS][NUM_KEYS] = {
    /* Layer 0 - base */
    {
        /* ESC             */ KEY_ESC,
        /* TAB             */ KEY_TAB,
        /* CAPS            */ KEY_CAPSLOCK,
        /* L_SHIFT         */ KEY_LEFTSHIFT,
        /* LEFT_CTRL       */ KEY_LEFTCTRL,
        /* NUM_1           */ KEY_1,
        /* L_WIN           */ KEY_LEFTMETA,
        /* L_ALT           */ KEY_LEFTALT,
        /* Q               */ KEY_Q,
        /* A               */ KEY_A,
        /* Z               */ KEY_Z,
        /* NUM_2           */ KEY_2,
        /* W               */ KEY_W,
        /* S               */ KEY_S,
        /* X               */ KEY_X,
        /* C               */ KEY_C,
        /* D               */ KEY_D,
        /* K               */ KEY_K,
        /* I               */ KEY_I,
        /* NUM_8           */ KEY_8,
        /* L_ANGLE_BRACKET */ KEY_COMMA,
        /* L               */ KEY_L,
        /* O               */ KEY_O,
        /* NUM_9           */ KEY_9,
        /* R_ANGLE_BRACKET */ KEY_DOT,
        /* COLON           */ KEY_SEMICOLON,
        /* P               */ KEY_P,
        /* NUM_0           */ KEY_0,
        /* QUESTION_MARK   */ KEY_SLASH,
        /* L_SQUARE_BRACKET*/ KEY_LEFTBRACE,
        /* R_WIN           */ KEY_RIGHTMETA,
        /* FN              */ VIA_KC_MO(1),
        /* MINUS           */ KEY_MINUS,
        /* QUOTE           */ KEY_APOSTROPHE,
        /* MENU            */ KEY_COMPOSE,
        /* R_SQUARE_BRACKET*/ KEY_RIGHTBRACE,
        /* PLUS            */ KEY_EQUAL,
        /* R_SHIFT         */ KEY_RIGHTSHIFT,
        /* ENTER           */ KEY_ENTER,
        /* SLASH           */ KEY_BACKSLASH,
        /* BACKSPACE       */ KEY_BACKSPACE,
        /* R_CTRL          */ KEY_RIGHTCTRL,
        /* E               */ KEY_E,
        /* NUM_3           */ KEY_3,
        /* V               */ KEY_V,
        /* F               */ KEY_F,
        /* R               */ KEY_R,
        /* NUM_4           */ KEY_4,
        /* SPACE           */ KEY_SPACE,
        /* B               */ KEY_B,
        /* G               */ KEY_G,
        /* T               */ KEY_T,
        /* NUM_5           */ KEY_5,
        /* H               */ KEY_H,
        /* Y               */ KEY_Y,
        /* NUM_6           */ KEY_6,
        /* N               */ KEY_N,
        /* J               */ KEY_J,
        /* U               */ KEY_U,
        /* NUM_7           */ KEY_7,
        /* M               */ KEY_M
    },
    /* Layer 1 - FN layer */
    {
        /* ESC             */ KEY_GRAVE,
        /* TAB             */ KEY_TAB,
        /* CAPS            */ KEY_CAPSLOCK,
        /* L_SHIFT         */ KEY_LEFTSHIFT,
        /* LEFT_CTRL       */ KEY_LEFTCTRL,
        /* NUM_1           */ KEY_F1,
        /* L_WIN           */ KEY_LEFTMETA,
        /* L_ALT           */ KEY_LEFTALT,
        /* Q               */ KEY_Q,
        /* A               */ KEY_LEFT,
        /* Z               */ KEY_Z,
        /* NUM_2           */ KEY_F2,
        /* W               */ KEY_UP,
        /* S               */ KEY_DOWN,
        /* X               */ KEY_X,
        /* C               */ KEY_C,
        /* D               */ KEY_RIGHT,
        /* K               */ KEY_HOME,
        /* I               */ KEY_SYSRQ,
        /* NUM_8           */ KEY_F8,
        /* L_ANGLE_BRACKET */ KEY_END,
        /* L               */ KEY_PAGEUP,
        /* O               */ KEY_SCROLLLOCK,
        /* NUM_9           */ KEY_F9,
        /* R_ANGLE_BRACKET */ KEY_PAGEDOWN,
        /* COLON           */ KEY_LEFT,
        /* P               */ KEY_PAUSE,
        /* NUM_0           */ KEY_F10,
        /* QUESTION_MARK   */ KEY_DOWN,
        /* L_SQUARE_BRACKET*/ KEY_UP,
        /* R_WIN           */ KEY_RIGHTMETA,
        /* FN              */ VIA_KC_TRNS,
        /* MINUS           */ KEY_F11,
        /* QUOTE           */ KEY_RIGHT,
        /* MENU            */ KEY_COMPOSE,
        /* R_SQUARE_BRACKET*/ KEY_RIGHTBRACE,
        /* PLUS            */ KEY_F12,
        /* R_SHIFT         */ KEY_RIGHTSHIFT,
        /* ENTER           */ KEY_ENTER,
        /* SLASH           */ KEY_INSERT,
        /* BACKSPACE       */ KEY_DELETE,
        /* R_CTRL          */ KEY_RIGHTCTRL,
        /* E               */ KEY_E,
        /* NUM_3           */ KEY_F3,
        /* V               */ KEY_V,
        /* F               */ KEY_F,
        /* R               */ KEY_R,
        /* NUM_4           */ KEY_F4,
        /* SPACE           */ KEY_SPACE,
        /* B               */ KEY_B,
        /* G               */ KEY_G,
        /* T               */ KEY_T,
        /* NUM_5           */ KEY_F5,
        /* H               */ KEY_HOME,
        /* Y               */ KEY_Y,
        /* NUM_6           */ KEY_F6,
        /* N               */ VIA_KC_VOLU,
        /* J               */ KEY_LEFT,
        /* U               */ KEY_PAGEUP,
        /* NUM_7           */ KEY_F7,
        /* M               */ VIA_KC_MUTE
    }
};

/** @brief Live keymap (RAM cache, EEPROM backed) */
static uint16_t nyan_via_keymap[VIA_NUM_LAYERS][NUM_KEYS];

/**
 * @brief Read one byte of the big-endian layer-major keymap byte stream.
 */
static uint8_t NyanViaReadKeymapByte(uint16_t offset)
{
    uint16_t keycode = nyan_via_keymap[offset / (NUM_KEYS * 2)][(offset % (NUM_KEYS * 2)) / 2];
    return (offset & 0x01U) ? (uint8_t)(keycode & 0xFFU) : (uint8_t)(keycode >> 8);
}

/**
 * @brief Write one byte of the big-endian layer-major keymap byte stream.
 */
static void NyanViaWriteKeymapByte(uint16_t offset, uint8_t value)
{
    uint16_t *keycode = &nyan_via_keymap[offset / (NUM_KEYS * 2)][(offset % (NUM_KEYS * 2)) / 2];
    if (offset & 0x01U)
        *keycode = (*keycode & 0xFF00U) | value;
    else
        *keycode = (*keycode & 0x00FFU) | ((uint16_t)value << 8);
}

/**
 * @brief Commit a range of the keymap byte stream to the EEPROM.
 *
 * Writes are chunked to the 24xx page size (128 bytes) so no write ever
 * crosses a page boundary (which would wrap within the page).
 * Must be called from main loop context - the EEPROM driver blocks.
 */
static NyanViaReturn NyanViaCommitKeymapRange(uint16_t offset, uint16_t len)
{
    while (len > 0U) {
        uint16_t addr = ADDR_VIA_KEYMAP + offset;
        uint16_t chunk = len;
        uint16_t page_off = addr % (EEPROM_PAGE_SIZE + 1U);

        if ((page_off + chunk) > (EEPROM_PAGE_SIZE + 1U))
            chunk = (EEPROM_PAGE_SIZE + 1U) - page_off;
        if (chunk > EEPROM_DRIVER_TX_BUF_SZ)
            chunk = EEPROM_DRIVER_TX_BUF_SZ;

        EepromFlushTxBuff(&nos_eeprom);
        for (uint16_t i = 0; i < chunk; ++i)
            nos_eeprom.tx_buf[i] = NyanViaReadKeymapByte(offset + i);

        // Blocks until the 24xx internal write cycle completes (retried on bus errors)
        if (EepromWrite(&nos_eeprom, false, addr, chunk) != EEPROM_SUCCESS)
            return VIA_FAILURE;

        offset += chunk;
        len -= chunk;
    }

    return VIA_SUCCESS;
}

/**
 * @brief Restore the factory default keymap (RAM + EEPROM).
 */
static NyanViaReturn NyanViaResetKeymap(void)
{
    memcpy((void *)nyan_via_keymap, (const void *)nyan_via_default_keymap, sizeof(nyan_via_keymap));
    return NyanViaCommitKeymapRange(0U, VIA_KEYMAP_BYTES);
}

NyanViaReturn NyanViaInit(void)
{
    // Probe for an existing keymap via the magic + layout version.
    // A read failure (after the driver's retries) must never look like "no
    // magic" and cause the persisted keymap to be wiped: run on the factory
    // defaults from RAM and leave the EEPROM alone.
    if (EepromRead(&nos_eeprom, false, ADDR_VIA_MAGIC, SIZE_VIA_MAGIC) != EEPROM_SUCCESS) {
        memcpy((void *)nyan_via_keymap, (const void *)nyan_via_default_keymap, sizeof(nyan_via_keymap));
        return VIA_FAILURE;
    }

    if (nos_eeprom.rx_buf[0] == VIA_MAGIC_BYTE_0 &&
        nos_eeprom.rx_buf[1] == VIA_MAGIC_BYTE_1 &&
        nos_eeprom.rx_buf[2] == VIA_KEYMAP_VERSION) {
        // Load the persisted keymap
        if (EepromRead(&nos_eeprom, false, ADDR_VIA_KEYMAP, VIA_KEYMAP_BYTES) != EEPROM_SUCCESS) {
            memcpy((void *)nyan_via_keymap, (const void *)nyan_via_default_keymap, sizeof(nyan_via_keymap));
            return VIA_FAILURE;
        }
        for (uint16_t i = 0; i < VIA_KEYMAP_BYTES; ++i)
            NyanViaWriteKeymapByte(i, nos_eeprom.rx_buf[i]);
        return VIA_SUCCESS;
    }

    // First boot (or layout version change): program factory defaults
    memcpy((void *)nyan_via_keymap, (const void *)nyan_via_default_keymap, sizeof(nyan_via_keymap));

    // Keymap first, magic last: an interrupted first boot is simply redone
    if (NyanViaCommitKeymapRange(0U, VIA_KEYMAP_BYTES) != VIA_SUCCESS)
        return VIA_FAILURE;

    EepromFlushTxBuff(&nos_eeprom);
    nos_eeprom.tx_buf[0] = VIA_MAGIC_BYTE_0;
    nos_eeprom.tx_buf[1] = VIA_MAGIC_BYTE_1;
    nos_eeprom.tx_buf[2] = VIA_KEYMAP_VERSION;
    if (EepromWrite(&nos_eeprom, false, ADDR_VIA_MAGIC, SIZE_VIA_MAGIC) != EEPROM_SUCCESS)
        return VIA_FAILURE;

    return VIA_SUCCESS;
}

uint16_t NyanViaGetKeycode(uint8_t layer, uint8_t key)
{
    if (layer >= VIA_NUM_LAYERS || key >= NUM_KEYS)
        return VIA_KC_NO;

    return nyan_via_keymap[layer][key];
}

/**
 * @brief Execute one VIA command.
 * @param cmd 32 byte raw HID OUT report.
 * @param resp 32 byte raw HID IN report (pre-filled with the command echo).
 */
static void NyanViaHandleCommand(uint8_t *cmd, uint8_t *resp)
{
    switch (cmd[0]) {
        case VIA_CMD_GET_PROTOCOL_VERSION:
            resp[1] = (uint8_t)(VIA_PROTOCOL_VERSION >> 8);
            resp[2] = (uint8_t)(VIA_PROTOCOL_VERSION & 0xFFU);
            break;

        case VIA_CMD_GET_KEYBOARD_VALUE:
            switch (cmd[1]) {
                case VIA_KV_UPTIME: {
                    uint32_t uptime = HAL_GetTick();
                    resp[2] = (uint8_t)(uptime >> 24);
                    resp[3] = (uint8_t)(uptime >> 16);
                    resp[4] = (uint8_t)(uptime >> 8);
                    resp[5] = (uint8_t)(uptime);
                    break;
                }
                case VIA_KV_LAYOUT_OPTIONS:
                    // No layout options on Nyan Keys - report 0xFFFFFFFF
                    resp[2] = 0xFFU;
                    resp[3] = 0xFFU;
                    resp[4] = 0xFFU;
                    resp[5] = 0xFFU;
                    break;
                case VIA_KV_SWITCH_MATRIX_STATE:
                    // 61 keys packed LSB first - pressed keys are active low from the FPGA
                    for (uint8_t byte = 0; byte < (NUM_KEYS + 7U) / 8U; ++byte) {
                        uint8_t packed = 0;
                        for (uint8_t bit = 0; bit < 8U; ++bit) {
                            uint8_t key = (byte * 8U) + bit;
                            if (key < NUM_KEYS && !NyanGetKeyState((NyanKeys *)&nyan_keys, key))
                                packed |= (uint8_t)(1U << bit);
                        }
                        resp[2 + byte] = packed;
                    }
                    break;
                case VIA_KV_FIRMWARE_VERSION:
                    resp[2] = (uint8_t)(VIA_FIRMWARE_VERSION >> 24);
                    resp[3] = (uint8_t)(VIA_FIRMWARE_VERSION >> 16);
                    resp[4] = (uint8_t)(VIA_FIRMWARE_VERSION >> 8);
                    resp[5] = (uint8_t)(VIA_FIRMWARE_VERSION);
                    break;
                default:
                    resp[0] = VIA_CMD_UNHANDLED;
                    break;
            }
            break;

        case VIA_CMD_DYNAMIC_KEYMAP_GET_KEYCODE: {
            // cmd: [0x04, layer, row, col] -> resp: [0x04, layer, row, col, kc_hi, kc_lo]
            uint8_t layer = cmd[1];
            uint8_t col = cmd[3];
            if (layer >= VIA_NUM_LAYERS || cmd[2] != 0U || col >= NUM_KEYS) {
                resp[0] = VIA_CMD_UNHANDLED;
                break;
            }
            uint16_t keycode = nyan_via_keymap[layer][col];
            resp[4] = (uint8_t)(keycode >> 8);
            resp[5] = (uint8_t)(keycode & 0xFFU);
            break;
        }

        case VIA_CMD_DYNAMIC_KEYMAP_SET_KEYCODE: {
            // cmd: [0x05, layer, row, col, kc_hi, kc_lo]
            uint8_t layer = cmd[1];
            uint8_t col = cmd[3];
            if (layer >= VIA_NUM_LAYERS || cmd[2] != 0U || col >= NUM_KEYS) {
                resp[0] = VIA_CMD_UNHANDLED;
                break;
            }
            nyan_via_keymap[layer][col] = ((uint16_t)cmd[4] << 8) | cmd[5];
            if (NyanViaCommitKeymapRange(((uint16_t)layer * NUM_KEYS + col) * 2U, 2U) != VIA_SUCCESS)
                resp[0] = VIA_CMD_UNHANDLED;
            break;
        }

        case VIA_CMD_DYNAMIC_KEYMAP_RESET:
        case VIA_CMD_EEPROM_RESET:
            if (NyanViaResetKeymap() != VIA_SUCCESS)
                resp[0] = VIA_CMD_UNHANDLED;
            break;

        case VIA_CMD_BOOTLOADER_JUMP:
            // Handled by the existing NyanOS DFU flow (charges BOOT0 cap + resets)
            nos.dfu_mode = true;
            break;

        case VIA_CMD_DYNAMIC_KEYMAP_MACRO_GET_COUNT:
            resp[1] = 0U; // Macros are not supported on Nyan Keys
            break;

        case VIA_CMD_DYNAMIC_KEYMAP_MACRO_GET_BUFFER_SIZE:
            resp[1] = 0U; // No macro buffer (16 bit size = 0)
            resp[2] = 0U;
            break;

        case VIA_CMD_DYNAMIC_KEYMAP_GET_LAYER_COUNT:
            resp[1] = VIA_NUM_LAYERS;
            break;

        case VIA_CMD_DYNAMIC_KEYMAP_GET_BUFFER: {
            // cmd: [0x12, off_hi, off_lo, size] -> resp: [0x12, off_hi, off_lo, size, data...]
            uint16_t offset = ((uint16_t)cmd[1] << 8) | cmd[2];
            uint8_t size = cmd[3];
            if (size > 28U || ((uint32_t)offset + size) > VIA_KEYMAP_BYTES) {
                resp[0] = VIA_CMD_UNHANDLED;
                break;
            }
            for (uint8_t i = 0; i < size; ++i)
                resp[4 + i] = NyanViaReadKeymapByte(offset + i);
            break;
        }

        case VIA_CMD_DYNAMIC_KEYMAP_SET_BUFFER: {
            // cmd: [0x13, off_hi, off_lo, size, data...]
            uint16_t offset = ((uint16_t)cmd[1] << 8) | cmd[2];
            uint8_t size = cmd[3];
            if (size > 28U || ((uint32_t)offset + size) > VIA_KEYMAP_BYTES) {
                resp[0] = VIA_CMD_UNHANDLED;
                break;
            }
            for (uint8_t i = 0; i < size; ++i)
                NyanViaWriteKeymapByte(offset + i, cmd[4 + i]);
            if (NyanViaCommitKeymapRange(offset, size) != VIA_SUCCESS)
                resp[0] = VIA_CMD_UNHANDLED;
            break;
        }

        default:
            resp[0] = VIA_CMD_UNHANDLED;
            break;
    }
}

NyanViaReturn NyanViaProcess(void)
{
    // Buffers are DMA sources/targets on the OTG HS core - must be 32-bit aligned
    __ALIGN_BEGIN static uint8_t rx_report[HID_RAW_REPORT_SIZE] __ALIGN_END;
    __ALIGN_BEGIN static uint8_t tx_report[HID_RAW_REPORT_SIZE] __ALIGN_END;

    if (USBD_HID_RAW_RxPending(&hUsbDevice) == 0U)
        return VIA_SUCCESS;

    if (USBD_HID_RAW_GetRxReport(&hUsbDevice, rx_report) != (uint8_t)USBD_OK)
        return VIA_FAILURE;

    // Responses always echo the request bytes - results overlay the tail
    memcpy(tx_report, rx_report, sizeof(tx_report));

    NyanViaHandleCommand(rx_report, tx_report);

    USBD_HID_RAW_SendReport(&hUsbDevice, tx_report, HID_RAW_REPORT_SIZE);

    // Re-arm the OUT endpoint for the next command
    USBD_HID_RAW_ReceiveReport(&hUsbDevice);

    return VIA_SUCCESS;
}
