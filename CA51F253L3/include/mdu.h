#ifndef _MDU_H_
#define _MDU_H_

#include "include/stdint.h"

/* CA51F2 hardware MDU (UG ch.27) helpers, used directly by the callers       */
/* instead of the SDCC 32-bit runtime ops. The MDU shares the INDEX register  */
/* window with the LED buffer and touch, so the helpers save/restore INDEX    */
/* and poll with interrupts masked.                                           */

uint32_t Mdu_Mul16(uint16_t a, uint16_t b);                                             /* 16x16 -> 32 product                */
uint8_t  Mdu_DivMod32(uint32_t n, uint32_t d, uint32_t xdata *q, uint32_t xdata *r);    /* 32/32 -> q and r; 1 = ok, 0 = d==0 */

#endif
