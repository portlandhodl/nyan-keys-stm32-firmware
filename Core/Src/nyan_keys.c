/**
 * NyanKeys FPGA IP Driver (SPI2)
 * @author Reese Russell
 *
 * The FPGA pushes key state frames (see nyan_keys_frame.h) as the SPI master.
 * SPI2 is an RX-only slave; DMA1 Stream3 receives exactly one frame and its
 * transfer complete interrupt re-arms, validates and acks it.
 */

#include <stdlib.h>
#include <string.h>

#include "24xx_eeprom.h"
#include "nyan_eeprom_map.h"
#include "nyan_keys.h"
#include "nyan_via.h"
#include "spi.h"
#include "usb_hid_keys.h"

_Static_assert(NUM_KEYS == NYAN_KEYS_FRAME_KEYS, "FPGA frame carries a different number of keys");

#define KEYS_DMA_STREAM DMA1_Stream3
#define KEYS_DMA_IFCR   (DMA_LIFCR_CTCIF3 | DMA_LIFCR_CHTIF3 | DMA_LIFCR_CTEIF3 | DMA_LIFCR_CDMEIF3 | DMA_LIFCR_CFEIF3)

extern Eeprom24xx nos_eeprom;

static uint8_t   keys_rx_frame[32] __attribute__((aligned(32))); // DMA target - owns a whole D-cache line
static NyanKeys *keys_ctx;
static uint32_t  keys_spi_cr1;
static uint32_t  keys_spi_cr2;
static volatile uint32_t keys_stall_ndtr;
static uint32_t  keys_stall_since;

inline bool NyanGetKeyState(NyanKeys *keys, int key)
{
    int byteIndex = key / 8;
    int bitIndex = key % 8;
    return (keys->key_states[byteIndex] & (1 << bitIndex)) != 0;
}

/**
 * Stop the RX DMA, reset SPI2 and arm for exactly one frame. Resetting the
 * peripheral clears its shift register, bit counter and RX FIFO, so every
 * frame starts bit aligned no matter what happened on the bus before.
 */
static void NyanKeysRearm(void)
{
    KEYS_DMA_STREAM->CR &= ~DMA_SxCR_EN;
    while (KEYS_DMA_STREAM->CR & DMA_SxCR_EN) {}

    __HAL_RCC_SPI2_FORCE_RESET();
    __HAL_RCC_SPI2_RELEASE_RESET();
    SPI2->CR1 = keys_spi_cr1;
    SPI2->CR2 = keys_spi_cr2;

    DMA1->LIFCR = KEYS_DMA_IFCR;
    KEYS_DMA_STREAM->NDTR = NYAN_KEYS_FRAME_BYTES;
    KEYS_DMA_STREAM->CR |= DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE | DMA_SxCR_EN;
    SPI2->CR1 = keys_spi_cr1 | SPI_CR1_SPE;

    keys_stall_ndtr = NYAN_KEYS_FRAME_BYTES;
}

/**
 * Ack pulse - the FPGA captures the rising edge asynchronously, so any width
 * works; ~100ns keeps it clean on the board.
 */
static inline void NyanKeysAck(void)
{
    keys_ack_GPIO_Port->BSRR = keys_ack_Pin;
    for (int i = 0; i < 16; i++)
        __NOP();
    keys_ack_GPIO_Port->BSRR = (uint32_t)keys_ack_Pin << 16;
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
    // All keys released until the FPGA reports otherwise
    memset((void*)keys->key_states, 0xFF, sizeof(keys->key_states));
    memset((void*)keys->key_states_prv, 0xFF, sizeof(keys->key_states_prv));
    keys->started = false;
    keys->report_pending = false;
    keys->frames_good = 0;
    keys->frames_bad = 0;
    keys->stall_resets = 0;
    keys->super_key_disabled = NyanKeysReadSuperDisableEEPROM(&nos_eeprom);

    return NYAN_KEYS_SUCCESS;
}

NyanKeysReturn NyanKeysStart(NyanKeys *keys)
{
    keys_ctx = keys;

    // Cycle counter for the stall watchdog
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR = 0xC5ACCE55;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    // SPI2 register image from MX_SPI2_Init() (RX-only slave, mode 0, 8 bit).
    // FRXTH: a DMA request for every byte.
    keys_spi_cr1 = SPI2->CR1 & ~SPI_CR1_SPE;
    keys_spi_cr2 = SPI2->CR2 | SPI_CR2_FRXTH | SPI_CR2_RXDMAEN;

    // Stream direction, sizes and priority come from HAL_SPI_MspInit()
    KEYS_DMA_STREAM->PAR  = (uint32_t)&SPI2->DR;
    KEYS_DMA_STREAM->M0AR = (uint32_t)keys_rx_frame;

    NyanKeysRearm();
    keys->started = true;

    // Release the FPGA keys IP - it reports the current state immediately
    HAL_GPIO_WritePin(keys_fpga_resetn_GPIO_Port, keys_fpga_resetn_Pin, GPIO_PIN_SET);

    return NYAN_KEYS_SUCCESS;
}

void NyanKeysDmaIrqHandler(void)
{
    uint8_t  frame[NYAN_KEYS_FRAME_BYTES];
    uint32_t status = DMA1->LISR;
    NyanKeys *keys = keys_ctx;

    DMA1->LIFCR = KEYS_DMA_IFCR;
    if (keys == NULL)
        return;

    if (!(status & DMA_LISR_TCIF3)) {
        // DMA error - drop whatever was received; the FPGA will retry
        keys->frames_bad++;
        NyanKeysRearm();
        return;
    }

    SCB_InvalidateDCache_by_Addr((uint32_t*)keys_rx_frame, sizeof(keys_rx_frame));
    memcpy(frame, keys_rx_frame, sizeof(frame));

    // Ready for the next frame before it can be sent (it is sent after the ack)
    NyanKeysRearm();

    if (!NyanKeysFrameValid(frame)) {
        // Corrupt or misaligned - no ack, the FPGA resends after its timeout
        keys->frames_bad++;
        return;
    }

    NyanKeysAck();
    keys->frames_good++;
    memcpy((void*)keys->key_states, &frame[1], NYAN_KEYS_DATA_BYTES);
    NyanKeysFrameCallback(keys);
}

void NyanKeysService(void)
{
    NyanKeys *keys = keys_ctx;
    uint32_t  ndtr, now;

    if (keys == NULL || !keys->started)
        return;

    ndtr = KEYS_DMA_STREAM->NDTR;
    now  = DWT->CYCCNT;

    // Idle (nothing received) or still receiving - nothing to do
    if (ndtr == NYAN_KEYS_FRAME_BYTES || ndtr == 0 || ndtr != keys_stall_ndtr) {
        keys_stall_ndtr  = ndtr;
        keys_stall_since = now;
        return;
    }

    if ((now - keys_stall_since) < NYAN_KEYS_STALL_US * (SystemCoreClock / 1000000U))
        return;

    // A partial frame stopped making progress: an SCLK edge was lost. Discard
    // it so the FPGA's retry is received bit aligned.
    __disable_irq();
    if (KEYS_DMA_STREAM->NDTR == ndtr) {
        NyanKeysRearm();
        keys->stall_resets++;
    }
    __enable_irq();
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
    memset((void*)desc, 0, sizeof(NyanKeyBoardDescriptor));

    // Set descriptor report counters to 0
    keys->boot_byte_cnt = 0;
    keys->ext_byte_cnt = 0;

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
