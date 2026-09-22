#ifndef _NYAN_EEPROM_MAP_H
#define _NYAN_EEPROM_MAP_H

// Board Properties
#define ADDR_BOARD_SERIAL_NUMBER        0x0000
#define ADDR_BOARD_OWNER                0x0020
#define ADDR_BOARD_BUILD_BLOCK          0x0060
#define ADDR_BOARD_VERSION              0x0070
#define ADDR_TOTAL_KEYSTROKES           0x0080
#define ADDR_TOTAL_USB_CONNECTIONS      0x0090
#define ADDR_TOTAL_TIMES_POWERED_ON     0x00A0
#define ADDR_FPGA_BITSTREAM_LEN         0x00B0

// Reserved Areas
#define ADDR_RESERVED_0                 0x00C0 /*** Now used for storing Super Key Disablement State ***/
#define ADDR_RESERVED_1                 0x00D0
#define ADDR_RESERVED_2                 0x00E0
#define ADDR_RESERVED_3                 0x00F0 /*** Now used for the VIA keymap magic/version ***/
#define ADDR_RESERVED_4                 0x0100 /*** VIA dynamic keymap starts here (see ADDR_VIA_KEYMAP) ***/
#define ADDR_RESERVED_5                 0x0110
#define ADDR_RESERVED_6                 0x0120
#define ADDR_RESERVED_7                 0x0130
#define ADDR_RESERVED_8                 0x0140
#define ADDR_RESERVED_9                 0x0150
#define ADDR_RESERVED_10                0x0160
#define ADDR_RESERVED_11                0x0170
#define ADDR_RESERVED_12                0x0180
#define ADDR_RESERVED_13                0x0190
#define ADDR_RESERVED_14                0x01A0
#define ADDR_RESERVED_15                0x01B0
#define ADDR_RESERVED_16                0x01C0
#define ADDR_RESERVED_17                0x01D0
#define ADDR_RESERVED_18                0x01E0
#define ADDR_RESERVED_19                0x01F0

// VIA Dynamic Keymap (bank 0)
#define ADDR_VIA_MAGIC                  ADDR_RESERVED_3  /* 'V','I', keymap layout version */
#define ADDR_VIA_KEYMAP                 ADDR_RESERVED_4  /* layer-major big-endian u16 keycodes */
#define SIZE_VIA_MAGIC                  3
#define SIZE_VIA_KEYMAP                 244              /* 2 layers x 61 keys x 2 bytes */

// FPGA Bitstream (bank 1)
#define ADDR_FPGA_BITSTREAM             0x0000

// Section Sizes
#define SIZE_BOARD_SERIAL_NUMBER        32
#define SIZE_BOARD_OWNER                64
#define SIZE_BOARD_BUILD_BLOCK          16
#define SIZE_BOARD_VERSION              16
#define SIZE_TOTAL_KEYSTROKES           16
#define SIZE_TOTAL_USB_CONNECTIONS      16
#define SIZE_TOTAL_TIMES_POWERED_ON     16
#define SIZE_FPGA_BITSTREAM_LEN         16
#define SIZE_RESERVED                   16
#define SIZE_FPGA_BITSTREAM             8192 

#endif // _NYAN_EEPROM_MAP_H