#include "include/crc.h"

uint8_t crc8(uint8_t crc, const uint8_t xdata *p, uint16_t len) {
	uint16_t i;

	if(len == 0) return crc;
	for (i = 0; i < len; i++) {
		uint8_t b;

        crc ^= *p++;
        for (b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
	}

	return crc;
}
