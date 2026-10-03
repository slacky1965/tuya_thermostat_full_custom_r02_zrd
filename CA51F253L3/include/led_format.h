#ifndef LED_FORMAT_H
#define LED_FORMAT_H
#include "include/stdint.h"

/* Signed c100 decomposition shared by the temperature and menu renderers. */

/* The two big digits cannot show every value without lying: a negative value
 * spends the tens cell on the sign (so -15.00 would read "-5.0") and three
 * integer digits do not fit. Values outside the range are reported instead of
 * being rendered as a different number; Led_DrawValue() answers with Lo/UP. */
#define LED_RANGE_IN   0
#define LED_RANGE_LO   1    /* below -9.99 degC: show "Lo"                       */
#define LED_RANGE_UP   2    /* above 99.99 degC: show "UP"                       */
#define LED_RANGE_MIN_C100  (-999)
#define LED_RANGE_MAX_C100  9999

typedef struct {
	uint8_t negative;
	uint8_t tens;
	uint8_t units;
	uint8_t half;
	uint8_t hundred;
	uint8_t range;    /* LED_RANGE_*                                         */
} led_value_parts_t;

/* The output structure must reside in xdata under --model-large. */
void Led_FormatC100(int16_t value_c100, led_value_parts_t xdata *parts);

#endif
