#ifndef RTC_H
#define RTC_H
#include "include/stdint.h"
/**********************************************************************************/
/* RTC (real-time clock) module. Per user guide section 14 (+ CKSEL.RTCKS, 9.3).  */
/*                                                                                */
/* Time kept in hardware: RTCS (=sec 0..59), RTCM (min), RTCH (week[2:0] +        */
/* hour[4:0]), RTCDL/RTCDH (16-bit day counter). Data written only when           */
/* RTCON.RTCWE set (write-enable window).                                         */
/*                                                                                */
/* Clock source: automatically detected - external 32.768 kHz crystal (XOSCL)     */
/* when present, else internal IRCL/4 (RTCKS=1).                                  */
/*                                                                                */
/* Time exchange with the host link follows the format used by this chip's        */
/* stock protocol (ZT3L, command 0x24): TWO 32-bit big-endian unix seconds        */
/* values, `utc` then `local`. Our own link will mirror that format. The MCU      */
/* keeps local time-of-day (+ weekday + day counter) in the RTC hardware; the     */
/* most recently received utc value is remembered so it can be reported back.     */
/**********************************************************************************/

/* RTCON (0xF1) bit fields */
#define RTCE(N)        ((N) << 7)   /* RTC clock enable                          */
#define MSE(N)         ((N) << 6)   /* ms interrupt enable                       */
#define HSE(N)         ((N) << 5)   /* half-second interrupt enable              */
#define SCE(N)         ((N) << 4)   /* alarm: second compare enable              */
#define MCE(N)         ((N) << 3)   /* alarm: minute compare enable              */
#define HCE(N)         ((N) << 2)   /* alarm: hour compare enable                */
#define RTCWE(N)       ((N) << 1)   /* time write-enable                         */

/* RTCIF (0xEE) flags (write 1 to clear) */
#define RTC_MF         (1 << 2)     /* ms interrupt flag                         */
#define RTC_HF         (1 << 1)     /* half-second interrupt flag                */
#define RTC_AF         (1 << 0)     /* alarm interrupt flag                      */

/* RTC clock source actually in use (result of auto-detect) */
#define RTC_SRC_XOSCL  0            /* external 32.768 kHz crystal               */
#define RTC_SRC_IRCL   1            /* internal IRCL / 4                         */

void RTC_Init(void);                              /* detect clock, enable RTC, set default time */
void RTC_WriteTimeDay(uint8_t hour, uint8_t min, uint8_t sec, uint8_t week, uint16_t day);
void RTC_ReadTime(uint8_t xdata *hour, uint8_t xdata *min, uint8_t xdata *sec, uint8_t xdata *week);
uint16_t RTC_GetDay(void);            /* 16-bit day counter                          */
uint8_t RTC_GetWeek(void);            /* weekday 1..7 (1=Monday)                     */
void RTC_SetUnixTime(uint32_t local); /* time sync: local unix seconds from the host */

#if defined(UART_DEBUG) && UART_DEBUG && defined(RTC_EN) && RTC_EN
uint8_t RTC_ClockSrc(void);           /* RTC_SRC_XOSCL or RTC_SRC_IRCL               */
void RTC_Report(void);                /* 1 s debug dump, called from main()          */
#endif

#endif
