#ifndef _INTRINS_H_
#define _INTRINS_H_
/*****************************************************************************************/
/* Keil C51 <intrins.h> compatibility shim for SDCC.                                     */
/* The current project sources include <intrins.h> but do not call any of its intrinsics */
/* (_nop_/_testbit_/rotates). Keep the file so the #include resolves under SDCC.         */
/*****************************************************************************************/
#define _nop_()       __asm NOP __endasm
#define _testbit_(b)  ((b) ? ((b) = 0, 1) : 0)

#endif
