/******************************************************************************/
#include "include/stdint.h"
#include "include/ca51f2sfr.h"
#include "include/mdu.h"
/******************************************************************************/
/* CA51F2 MDU (UG ch.27): MDUCON bits 7..6 MODE, bit 5 DSFT (start, hardware  */
/* clears it), MDUDAT is an INDEX-windowed byte register shared with the LED  */
/* buffer and touch keys. The unit is big-endian: MDUDAT0 = MSB, MDUDAT7 =    */
/* LSB, for operands and results alike (table 27-4, control examples 27.5).   */
/* Working values are packed/unpacked explicitly to match that layout.        */
/*                                                                            */
/* INVARIANT: no ISR touches INDEX/MDUDAT (T0 ISR uses registers only, UART1  */
/* ISR uses SFR/ring only, other vectors are empty), so these functions do    */
/* NOT mask interrupts. UART1 has a single-byte receive buffer (S1BUF) and    */
/* NO overrun flag: an interrupt-mask window here delayed the RX ISR past one */
/* byte time (~87 us at 115200) and the next byte silently overwrote S1BUF,   */
/* dropping whole inbound frames (soak 2026-09-27: 100% of link losses were   */
/* in the TLSR->CA direction). INDEX save/restore stays: it protects the      */
/* main-loop INDEX users (led.c / buttons.c). If an ISR ever needs            */
/* INDEX/MDUDAT, this invariant must be revisited together with a mask.       */
#define MDU_MUL_MODE   0x00u                    /* 16x16 multiply, 1 clock  */
#define MDU_DIV_MODE   0x40u                    /* 32/32 divide, 8 clocks   */
#define MDU_START      0x20u                    /* start divide/shift       */
#define MDU_POLL_LIMIT 100u                     /* safety cap on the wait   */
/****************************************************************************/
uint32_t Mdu_Mul16(uint16_t a, uint16_t b) {
	uint32_t p;
	uint8_t s_idx;

	s_idx = (uint8_t)INDEX;

	MDUCON = MDU_MUL_MODE;
	INDEX = 0; MDUDAT = (uint8_t)(a >> 8);    /* multiplicand hi..lo       */
	INDEX = 1; MDUDAT = (uint8_t)a;
	INDEX = 2; MDUDAT = (uint8_t)(b >> 8);    /* multiplier hi..lo         */
	INDEX = 3; MDUDAT = (uint8_t)b;
	INDEX = 4; p  = (uint32_t)MDUDAT << 24;   /* product MSB..LSB          */
	INDEX = 5; p |= (uint32_t)MDUDAT << 16;
	INDEX = 6; p |= (uint32_t)MDUDAT << 8;
	INDEX = 7; p |= (uint8_t)MDUDAT;

	INDEX = s_idx;
	return p;
}
/****************************************************************************/
uint8_t Mdu_DivMod32(uint32_t n, uint32_t d, uint32_t xdata *q, uint32_t xdata *r) {
	uint8_t i, s_idx;
	uint32_t qv = 0;
	uint32_t rv = 0;

	*q = 0;
	*r = 0;
	if(d == 0) return 0;

	s_idx = (uint8_t)INDEX;

	MDUCON = MDU_DIV_MODE;
	INDEX = 0; MDUDAT = (uint8_t)(n >> 24);   /* numerator  MSB..LSB       */
	INDEX = 1; MDUDAT = (uint8_t)(n >> 16);
	INDEX = 2; MDUDAT = (uint8_t)(n >> 8);
	INDEX = 3; MDUDAT = (uint8_t)n;
	INDEX = 4; MDUDAT = (uint8_t)(d >> 24);   /* divisor    MSB..LSB       */
	INDEX = 5; MDUDAT = (uint8_t)(d >> 16);
	INDEX = 6; MDUDAT = (uint8_t)(d >> 8);
	INDEX = 7; MDUDAT = (uint8_t)d;
	MDUCON |= MDU_START;
	i = MDU_POLL_LIMIT;
	while((uint8_t)(MDUCON & MDU_START)) {
		if(--i == 0) {                        /* timeout: keep regs sane   */
			INDEX = s_idx;
			return 0;
		}
	}
	INDEX = 0; qv  = (uint32_t)MDUDAT << 24;  /* quotient  MSB..LSB       */
	INDEX = 1; qv |= (uint32_t)MDUDAT << 16;
	INDEX = 2; qv |= (uint32_t)MDUDAT << 8;
	INDEX = 3; qv |= (uint8_t)MDUDAT;
	INDEX = 4; rv  = (uint32_t)MDUDAT << 24;  /* remainder MSB..LSB       */
	INDEX = 5; rv |= (uint32_t)MDUDAT << 16;
	INDEX = 6; rv |= (uint32_t)MDUDAT << 8;
	INDEX = 7; rv |= (uint8_t)MDUDAT;
	INDEX = s_idx;

	*q = qv;
	*r = rv;
	return 1;
}