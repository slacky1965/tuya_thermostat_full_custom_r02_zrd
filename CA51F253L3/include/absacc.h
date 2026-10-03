#ifndef _ABSACC_H_
#define _ABSACC_H_
/******************************************************************************************/
/* Keil C51 <absacc.h> compatibility shim for SDCC.                                       */
/* Only XBYTE is provided. The current project sources include <absacc.h> (uart.c) but do */
/* not use XBYTE (it appears only in excluded experimental mains).                        */
/******************************************************************************************/
#define XBYTE ((volatile unsigned char __xdata *)0)

#endif
