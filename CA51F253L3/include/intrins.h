#ifndef _INTRINS_H_
#define _INTRINS_H_
/*****************************************************************************************/
/* Keil C51 <intrins.h> compatibility shim for SDCC and clang (mcs51-llvm).              */
/* _nop_ is used by src/delay.c; the other intrinsics are kept for source compatibility. */
/*****************************************************************************************/
#if defined(__clang__)
#define _nop_()       __asm__ volatile ("nop")
#else
#define _nop_()       __asm NOP __endasm
#endif
#define _testbit_(b)  ((b) ? ((b) = 0, 1) : 0)

#endif
