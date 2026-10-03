#include "include/stdint.h"
#include "include/led_format.h"

void Led_FormatC100(int16_t value_c100, led_value_parts_t xdata *parts) {
	uint16_t av;
	uint16_t q100;
	uint8_t q10;

	if(value_c100 < 0) {
		parts->negative = 1;
		av = (uint16_t)(0u - (uint16_t)value_c100);
	}
	else {
		parts->negative = 0;
		av = (uint16_t)value_c100;
	}
	q100 = 0;
	while(av >= 100) {
		av -= 100;
		q100++;
	}
	parts->hundred = (uint8_t)(q100 >= 100);
	if(q100 >= 300) q100 -= 300;
	else if(q100 >= 200) q100 -= 200;
	else if(q100 >= 100) q100 -= 100;
	q10 = 0;
	while(q100 >= 10) {
		q100 -= 10;
		q10++;
	}
	parts->tens = q10;
	parts->units = (uint8_t)q100;
	parts->half = (uint8_t)(av >= 50);
	/* The two big digits plus the sign cannot show every value: report the     */
	/* out-of-range ones so the caller prints Lo/UP instead of another number.  */
	if(value_c100 > LED_RANGE_MAX_C100)      parts->range = LED_RANGE_UP;
	else if(value_c100 < LED_RANGE_MIN_C100) parts->range = LED_RANGE_LO;
	else                                     parts->range = LED_RANGE_IN;
}
