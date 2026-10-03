#ifndef _CHARS_H_
#define _CHARS_H_
/***********************************************************************************/
/* 7-seg letter patterns for the big temperature digits.                           */
/* Segment bits: a=bit0 b=bit1 c=bit2 d=bit3 e=bit4 f=bit5 g=bit6; bit7 = icon.    */
/* Shared by the CA51F2 app and the XRAM flasher (pure #defines, no includes).     */
/***********************************************************************************/
#define CH_MINUS 0x40  /* g           */
#define CH_I     0x06  /* b c         */
#define CH_N     0x37  /* a b c e f   */
#define CH_O     0x3F  /* a b c d e f */
#define CH_U     0x3E  /* b c d e f   */
#define CH_A     0x77  /* a b c e f g */
#define CH_L     0x38  /* d e f       */
#define CH_P     0x73  /* a b e f g   */
#define CH_E     0x79  /* a d e f g   */
#define CH_R     0x50  /* e g         */

#endif
