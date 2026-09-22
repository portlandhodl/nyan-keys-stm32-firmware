/**
 * NyanKeys FPGA IP Driver (SPI2)
 * @author Reese Russell
 */

#include <stdlib.h>
#include <string.h>

#include "24xx_eeprom.h"
#include "nyan_eeprom_map.h"
#include "nyan_keys.h"
#include "nyan_via.h"
#include "spi.h"
#include "usb_hid_keys.h"

extern Eeprom24xx nos_eeprom;

static uint8_t keys_registers_addresses[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x00, 0x00}; // We need the last dummy byte to extract the last byte from the keys IP

inline bool NyanGetKeyState(NyanKeys *keys, int key)
{
    int byteIndex = key / 8;
    int bitIndex = key % 8;

    // We offset the byte index by 1 to account for the dummy first byte;
    return (keys->key_states[byteIndex + 1] & (1 << bitIndex)) != 0;
}

NyanKeysReturn NyanStuctAllocator(NyanKeys *keys, volatile NyanKeyBoardDescriptor *desc, uint8_t hid_scan_code)
{
    if(keys->boot_byte_cnt < NUM_BOOT_KEYS)
        desc->BOOTKEYCODE[keys->boot_byte_cnt++] = hid_scan_code;
    else if(keys->ext_byte_cnt < NUM_HYBRID_KEYS)
        desc->EXTKEYCODE[keys->ext_byte_cnt++] = hid_scan_code;
    else
        return NYAN_KEYS_FAILURE;
    return NYAN_KEYS_SUCCESS;
}

NyanKeysReturn NyanKeysInit(NyanKeys *keys)
{
    // We only have one device on the bus so we will just leave SS Low
    HAL_GPIO_WritePin(Keys_Slave_Select_GPIO_Port, Keys_Slave_Select_Pin, GPIO_PIN_RESET);

    keys->warm_up_reads = 0;
    keys->warmed_up = false;
    keys->super_key_disabled = NyanKeysReadSuperDisableEEPROM(&nos_eeprom);

    return NYAN_KEYS_SUCCESS;
}

NyanKeysReturn NyanGetKeys(NyanKeys *keys)
{
    // Send out the DMA and we will get the results back from the FPGA 
    if(HAL_SPI_TransmitReceive_DMA(&hspi2, &keys_registers_addresses[0], (uint8_t*)&keys->key_states[0], sizeof(keys_registers_addresses)) != HAL_OK) {
        return NYAN_KEYS_FAILURE;
    }
    
    return NYAN_KEYS_SUCCESS;
}

NyanKeysReturn NyanKeysWriteSuperDisableEEPROM(Eeprom24xx* eeprom, bool disabled)
{   
    // First lets clear out the TX buff
    if(EepromFlushTxBuff(eeprom) != EEPROM_SUCCESS){
        return NYAN_KEYS_FAILURE;
    }
    // Second lets copy our new buffer over to the EEPROM driver
    eeprom->tx_buf[0] = (uint8_t)disabled;
    // Write the state to the eeprom. Don't overwrite the full 16 bytes of slot one because we will use it for other things.
    EepromWrite(eeprom, false, ADDR_RESERVED_0, 1);

    return NYAN_KEYS_SUCCESS;
}

bool NyanKeysReadSuperDisableEEPROM(Eeprom24xx* eeprom)
{   // Fetch the state of the super key disablement from the eeprom
    EepromRead(eeprom, false, ADDR_RESERVED_0, 1);
    while(eeprom->rx_inflight){}
    return (bool)(eeprom->rx_buf[0] == 0x00 ? false : true);
}


NyanKeysReturn NyanBuildHidReportFromKeyStates(NyanKeys *keys, volatile NyanKeyBoardDescriptor *desc)
{
    // Nullify the descriptor report
    if(keys->warmed_up)
        memset((void*)desc, 0, sizeof(NyanKeyBoardDescriptor));

    // Set descriptor report counters to 0
    keys->boot_byte_cnt = 0;
    keys->ext_byte_cnt = 0;

    if(!keys->warmed_up)
        return NYAN_KEYS_SUCCESS;

    // Resolve the active layer: any pressed key bound to MO(n) momentarily raises layer n
    uint8_t active_layer = 0;
    for (uint8_t key = 0; key < NUM_KEYS; ++key) {
        if(!NyanGetKeyState(keys, key)) {
            uint16_t keycode = NyanViaGetKeycode(0, key);
            if(keycode >= VIA_QK_MOMENTARY && keycode <= VIA_QK_MOMENTARY_MAX) {
                uint8_t layer = (uint8_t)(keycode - VIA_QK_MOMENTARY);
                if(layer < VIA_NUM_LAYERS && layer > active_layer)
                    active_layer = layer;
            }
        }
    }

    // Iterate through the keys and process their states via the VIA dynamic keymap
    for (uint8_t key = 0; key < NUM_KEYS; ++key) {
        if(NyanGetKeyState(keys, key))
            continue;

        uint16_t keycode = NyanViaGetKeycode(active_layer, key);
        if(keycode == VIA_KC_TRNS)
            keycode = NyanViaGetKeycode(0, key);

        // Layer keys and empty slots never emit scancodes
        if(keycode == VIA_KC_NO || keycode == VIA_KC_TRNS ||
          (keycode >= VIA_QK_MOMENTARY && keycode <= VIA_QK_MOMENTARY_MAX))
            continue;

        // QK_MODS wrapper: LCTL(KC_X) style keycodes carry a modifier in the high byte
        uint8_t extra_mods = 0;
        if(keycode >= VIA_QK_MODS && keycode <= VIA_QK_MODS_MAX) {
            uint8_t mods = (uint8_t)((keycode >> 8) & 0x1FU);
            extra_mods = (mods & 0x10U) ? (uint8_t)((mods & 0x0FU) << 4) : mods;
            keycode &= 0x00FFU;
        }

        // VIA media keycodes map back to the legacy bytes the original
        // hardcoded Nyan Keys layout emitted on the wire
        switch (keycode) {
            case VIA_KC_MUTE: keycode = KEY_MUTE; break;
            case VIA_KC_VOLU: keycode = KEY_VOLUMEUP; break;
            case VIA_KC_VOLD: keycode = KEY_VOLUMEDOWN; break;
            default: break;
        }

        // This firmware only emits 8 bit keyboard page usages
        if(keycode > 0x00FFU)
            continue;

        uint8_t usage = (uint8_t)keycode;

        /*** Persistent Windows logo key (super) disablement for gaming ***/
        if(usage == KEY_LEFTMETA || usage == KEY_RIGHTMETA) {
            if(active_layer > 0) {
                keys->super_key_disabled = !keys->super_key_disabled;
                NyanKeysWriteSuperDisableEEPROM(&nos_eeprom, keys->super_key_disabled);
                continue;
            }
            if(keys->super_key_disabled)
                continue;
        }

        if(usage >= KEY_LEFTCTRL && usage <= KEY_RIGHTMETA) {
            desc->MODIFIER |= (uint8_t)(1U << (usage - KEY_LEFTCTRL));
        } else if(usage >= KEY_A) {
            desc->MODIFIER |= extra_mods;
            NyanStuctAllocator(keys, desc, usage);
        }
    }

    return NYAN_KEYS_SUCCESS;
}

void NyanWarmupIncrementor(NyanKeys *keys)
{
    // Determine the warmup state of Nyan Keys FPGA outputs
    if (keys->warm_up_reads < KEYS_WARMUP_READS) {
        keys->warm_up_reads++;
        if (keys->warm_up_reads >= KEYS_WARMUP_READS) {
            keys->warmed_up = true;
        }
    }
}