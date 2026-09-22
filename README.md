<div align="center">
 <img src="assets/images/icon_square.png" width="120" height="120">
</div>

<br>

<div align="center">
    <h1>NyanOS 🐱 (NOS) - A Fast Keyboard Firmware</h1>
</div>

## Nyan Keys Keyboard Firmware
STM32F723 Firmware for the Nyan Keys keyboard. This architecture places the highest priority on performance, latency, and reliability.

### Supported Hardware

#### Keyboard PCBs
| PCB            | Description               |
| -------------- | ------------------------- |
| NyanKeys Proto | Nyan Keys Prototype 0     |
| NyanKeys 60    | Nyan Keys Production      |


_Please make a PR if you decide to use NyanOS for your keyboard PCB_

#### MCUs
| MCU         | Description               |
| ----------- | ------------------------- |
| STM32F723xx | STM32 F7 w/ USB2.0 HS PHY |

### Responsibilities 
 - __USB 2.0 HS HID/CDC composite device__
 - __VIA support via a 32 byte Raw HID interface (usage page 0xFF60)__
 - __Dynamic keymap (2 layers x 61 keys) persisted to the onboard EEPROM__
 - __Serial console via USB__
 - __EEPROM master - FPGA Bitstream Storage__
 - __FPGA bitstream programmer - SPI Master__
 - __Status indication - 5 Leds__
 - __Bitcoin Miner - opt-in__
 - __USB HID Interface @ 8000hz Polling__
 - __SPI Master to FPGA switch serializer and debouncer__

### VIA Support
NyanOS speaks the VIA protocol over a dedicated raw HID interface (usage page ```0xFF60```, usage ```0x61```, 32 byte interrupt IN/OUT reports) while keeping the 8000hz NKRO keyboard interface untouched. All 61 keys are remappable on 2 layers (base + FN) and the keymap is persisted to the onboard EEPROM (bank 0, address ```0x0100```, big-endian layer-major - the same layout QMK uses, so the standard VIA buffer commands work unmodified).

The keyboard definition for the VIA app lives at [```via/nyan_keys_60.json```](via/nyan_keys_60.json). To use it today, open [VIA](https://usevia.app) (or a local build of [the-via/app](https://github.com/the-via/app)) and use __Design -> Load Draft Definition__ with that file; to get native detection, submit the JSON to [the-via/keyboards](https://github.com/the-via/keyboards) (```src/nyan_keys/nyan_keys_60.json```).

Supported VIA features: dynamic keymap get/set (per-keycode and buffer commands), 2 layers, ```MO(1)``` layer switching, ```KC_TRNS``` fallthrough, mod-wrapped keycodes (e.g. ```LCTL(KC_X)```), media keycode translation (KC_MUTE/KC_VOLU/KC_VOLD to the legacy Nyan Keys bytes), switch matrix state for the key tester, uptime, EEPROM reset (restores factory defaults) and bootloader jump (enters the existing DFU flow). The factory default keymap reproduces the original hardcoded layout, including __FN + WIN__ persistent super-key lockout. Macros, encoders and lighting are not supported (the VIA app hides them when the device answers ```id_unhandled```/macro count 0).

A host-side unit test of the protocol handler and report builder is available:
```
gcc -std=gnu11 -I<stubs> -ICore/Inc test_via.c Core/Src/nyan_via.c Core/Src/nyan_keys.c -o test_via
```
(with stub headers for HAL/USB; the test simulates the VIA app command stream and a 24xx EEPROM in memory).

### NyanOS Terminal
One of the nicer features of NyanOS is a fully functional USB-CDC (_serial_) interface to interact with NyanOSk. Currently functionality is limited to only the most necessary commands for keyboard operation and configuration. 

### FPGA Bitstream Loading
The NyanOS out of the box should support any Lattice Ice40HX FPGAs that are also supported by [IceStorm](https://github.com/YosysHQ/icestorm). For a complete hardware support list visit. [https://clifford.at/icestorm](https://clifford.at/icestorm) The flow for synthesizing, placing, and routing is outlined below

1. ```yosys <args>```
2. ```nextpnr <args>```
3. ```icepack <bitstream.asc> <bitstream.bin```
4. ```icecompr.py < bitstream.bin > bitstream_compr.bin```

The icecompr tool is utilized to compress the bitstream, typically achieving a final ratio of approximately 25-30%. This compression allows an Ice40HX4K bitstream to fit on a 1Mbit I2C EEPROM. Without compression, the bitstream would occupy 131070 bytes, exceeding the capacity of the I2C EEPROM.

In NyanOS, the FPGA is treated as an SPI slave. The system manages all dummy bits both before (8 bits) and after (47 bits) the bitstream programming. NyanOS sends 48 dummy bits to the slave, as 47 is not divisible by 8, and thus it rounds up.

The FPGA bitstream programming in NyanOS occurs at startup and typically takes 2-3 seconds. This duration is primarily due to loading the bitstream from the I2C bus at 200KHz. Speed improvements might be possible in future updates by using lower value pull-up resistors. Currently, 10K resistors are used in Nyan Keys hardware.

__NOTE:__ The time to load the Bitstream is roughly 2-3 seconds and will occur on device power-on. The FPGA can be reprogrammed without a complete device reset, by setting the nos_fpga->configured to false. The main loop will eventually catch this after the interrupts complete and reload the bitstream from the contents of the EEPROM IC that are in Bank 1, using the value stored in the EEPROM bank 0 EEPROM FPGA Bitstream Len address 0x00B0 aligned as 4 Words, where each word is little endian encoded. This will be fixed later but current functions correct and you can use the ```write-bitstream <size>``` command and this will all be handled. __THE MAXIMUM BITSTREAM SIZE IS 65536 BYTES__ anything more and you will get a size error returned.

User input to keys is not handled until the FPGA bitstream is loaded. Any keys pressed before configuration will not be relayed via the HID peripheral.

### Persistent Windows Logo Key Disable
Nyan Keys now supports Windows logo key disablement. The user just has to press [FN + Windows Logo Key] to toggle the state between enabled and disabled. Each time this is done, the state is saved to the onboard EEPROM, ensuring it persists across reboots

### Status Indication
On the Nyan Keys 0.8x - 0.9x boards there are 5 status leds that are activated upon boot. The labels for these LED(s) are as follows
| ID   | Name        | Description            |
| ---- | ----------- | ---------------------- |
| 0    | LED_0       | MCU Functional POST    |
| 5    | LED_1       | FPGA Configured        |

The system status LED should pulse at a rate of 1.287hz and have a period of 777ms. This is driven by TIM1 and TIM6 using interrupts.

The FPGA configuration LED will always match the pin status of ```c_done``` of the Lattice FPGA. ```c_done``` is an active high signal and will only go high once the FPGA has been programmed __AND__ the 47 dummy bits have been sent over the SPI bus. NyanOS handles all of this without any additional programming using the ```FPGAInit``` function in ```lattice_ice_hx.c```

### EEPROM Address Layout
| Block | Address     | Description            | Length |
| ----  | ----------- | ---------------------- | ------ |
| 0     | 0x0000      | Board Serial Number    | 32     |
| 0     | 0x0020      | Board Owner            | 64     |
| 0     | 0x0060      | Board Build Block      | 16     |
| 0     | 0x0070      | Board Version          | 16     |
| 0     | 0x0080      | Total Keystrokes       | 16     |
| 0     | 0x0090      | Total USB Connections  | 16     |
| 0     | 0x00A0      | Total Times Powered On | 16     |
| 0     | 0x00B0      | FPGA Bitstream Len     | 16     |
| 0     | 0x00C0      | Reserved 0             | 16     |
| 0     | 0x00D0      | Reserved 1             | 16     |
| 0     | 0x00E0      | Reserved 2             | 16     |
| 0     | 0x00F0      | VIA Magic + Version    | 16     |
| 0     | 0x0100      | VIA Dynamic Keymap     | 244    |
| 0     | 0x01F4      | Reserved (5-19)        | 12     |
| 1     | 0x0000      | FPGA Bitstream         | 65535  |

