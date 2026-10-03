#include "include/stdint.h"
#include "include/config.h"
#include "include/intrins.h"

#define DELAY_50US_INNER 56
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
