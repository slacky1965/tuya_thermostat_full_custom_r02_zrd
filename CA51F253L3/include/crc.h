#ifndef _CRC_H_
#define _CRC_H_
#include "include/stdint.h"

/* CRC-8, poly 0x07, init 0x00, no reflection. Byte-wise bit loop (no 256-byte  */
/* table). Hashes exactly len bytes; len == 0 returns crc untouched.            */
/* Usage: crc = crc8(0, buf, len); continuing: crc = crc8(crc, ...).            */
/* p points into xdata (the settings image); no generic __gptrget.              */
uint8_t crc8(uint8_t crc, const uint8_t xdata *p, uint16_t len);

#endif
