#include <configs/xilinx_zynqmp.h>
#define CONFIG_NVMEM 1
#define CONFIG_I2C_EEPROM 1

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ZynqMP> i2c bus
// ZynqMP> i2c dev 1
// ZynqMP> i2c md 0x58 0x9A 0x6
// 009a: fc c2 3d 5a a7 42    ..=Z.B
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
