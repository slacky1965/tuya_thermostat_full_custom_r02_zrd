#ifndef _STDINT_H_
#define _STDINT_H_

#ifdef __CDT_PARSER__
    #define xdata
    #define code
#else
    #define xdata   __xdata
    #define code    __code
#endif

/********************************************************************************/
/* Minimal <stdint.h> shim for the legacy Keil C51 compiler (which ships none). */
/* C51 sizes: char=8b, int=16b, long=32b.                                       */
typedef unsigned char   uint8_t;
typedef signed   char   int8_t;
typedef unsigned int    uint16_t;
typedef signed   int    int16_t;
typedef unsigned long   uint32_t;
typedef signed   long   int32_t;

typedef unsigned char   uint_fast8_t;
typedef unsigned int    uint_fast16_t;
typedef unsigned long   uint_fast32_t;
/*****************************************************************************/
/* SDCC portability: map the Keil C51 memory-space keywords to the SDCC      */
/* underscored forms (SDCC rejects the bare `xdata`/`code`/`bit` spellings). */
/* This header is included first by every module, so the aliases are in      */
/* scope before any declaration that uses them.                              */
/*****************************************************************************/
#ifndef xdata
#define xdata   __xdata
#endif
#ifndef code
#define code    __code
#endif
#ifndef data
#define data    __data
#endif
#ifndef idata
#define idata   __idata
#endif
#ifndef pdata
#define pdata   __pdata
#endif
#ifndef bdata
#define bdata   __bdata
#endif
#ifndef bit
#define bit     __bit
#endif
/*****************************************************************************/
#endif
