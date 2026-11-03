#include "include/stdint.h"
#include "include/config.h"
#include "include/intrins.h"

/* Calibration, not a formula: one unit (DELAY_50US_PER_MS of them per ms) must
   burn 1000/DELAY_50US_PER_MS us = 47.62 us worth of clocks. The count below
   is what marker captures on a logic analyzer measured for THIS loop as
   emitted by the default clang build: 56 runs x1.3921 of nominal, 39 measures
   x1.003 (two-point model: ticks per unit = 19.2 + 4 * INNER). The s51
   simulator model and the datasheet instruction table both undercount the
   silicon by ~39%, so only a marker run can re-verify: after changing the
   compiler, the flags or this constant, re-measure with markers. */
#define DELAY_50US_INNER 39
#define DELAY_50US_PER_MS 21

void Delay_50us(register uint16_t n) {
	register uint8_t i;

	if(!n) return;
#if (SYSCLK_SRC == PLL)
	{
		uint8_t repeats = PLL_Multiple;
		uint16_t count = n;
		uint16_t part;
		while(repeats--) {
			part = count;
			do {
				_nop_();
				i = DELAY_50US_INNER;
				do {
					_nop_();
				} while(--i);
			} while(--part);
		}
	}
#else
	do {
		_nop_();
		i = DELAY_50US_INNER;
		do {
			_nop_();
		} while(--i);
	} while(--n);
#endif
}
void Delay_ms(register uint16_t n) {
	if(!n) return;
	while(n--) {
		Delay_50us(DELAY_50US_PER_MS);
	}
}
