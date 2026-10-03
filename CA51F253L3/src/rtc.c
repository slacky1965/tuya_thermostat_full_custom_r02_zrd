/**********************************************************************************/
#include "include/stdint.h"
#include "include/config.h"
#include "include/ca51f2sfr.h"
#include "include/ca51f2xsfr.h"
#include "include/gpiodef_f2.h"
#include "include/system_clock.h"
#include "include/delay.h"
#include "include/mdu.h"
#include "include/debug.h"
#include "include/rtc.h"
/**********************************************************************************/
#define RTC_XOSCL_MAXWAIT   98                 /* 98 x 10 ms startup bound        */
#define RTC_RTCE_SETTLE_US  8                  /* ~300 us before writing time     */
/**********************************************************************************/
static uint8_t rtc_src = RTC_SRC_IRCL;
static uint16_t rtc_day_snapshot;
/**********************************************************************************/
void RTC_WriteTimeDay(uint8_t hour, uint8_t min, uint8_t sec,
                      uint8_t week, uint16_t day) {
	if(hour > 23) hour = 23;
	if(min  > 59) min  = 59;
	if(sec  > 59) sec  = 59;
	if(week < 1)  week = 1;
	if(week > 7)  week = 7;

	RTCON |= RTCWE(1);
	Delay_50us(1);
	RTCS  = sec;
	RTCM  = min;
	RTCH  = (uint8_t)((week << 5) | hour);
	RTCDL = (uint8_t)day;
	RTCDH = (uint8_t)(day >> 8);
	Delay_50us(1);
	RTCON &= ~RTCWE(1);
	rtc_day_snapshot = day;
}
/**********************************************************************************/
void RTC_ReadTime(uint8_t xdata *hour, uint8_t xdata *min, uint8_t xdata *sec, uint8_t xdata *week) {
	uint16_t t1;
	uint16_t t2;
	uint16_t d1;
	uint16_t d2;
	uint8_t s1;
	uint8_t s2;

	do {
		s1 = RTCS;
		t1 = (uint16_t)(((uint16_t)RTCH << 8) | RTCM);
		d1 = (uint16_t)(((uint16_t)RTCDH << 8) | RTCDL);
		s2 = RTCS;
		t2 = (uint16_t)(((uint16_t)RTCH << 8) | RTCM);
		d2 = (uint16_t)(((uint16_t)RTCDH << 8) | RTCDL);
	} while(s1 != s2 || t1 != t2 || d1 != d2);

	rtc_day_snapshot = d1;
	if(hour) *hour = (uint8_t)((t1 >> 8) & 0x1F);
	if(week) *week = (uint8_t)(t1 >> 13);
	if(min)  *min  = (uint8_t)t1;
	if(sec)  *sec  = s2;
}
/**********************************************************************************/
uint16_t RTC_GetDay(void) {
	return rtc_day_snapshot;
}
/**********************************************************************************/
uint8_t RTC_GetWeek(void) {
	uint8_t week;

	RTC_ReadTime(0, 0, 0, &week);
	return week;
}
/**********************************************************************************/
#if UART_DEBUG && RTC_EN
void RTC_Report(void) {
	uint8_t rtc_h;
	uint8_t rtc_m;
	uint8_t rtc_s;
	uint8_t rtc_w;
	uint16_t  rtc_d;

	RTC_ReadTime(&rtc_h, &rtc_m, &rtc_s, &rtc_w);
	rtc_d = RTC_GetDay();
	debug_puts("t: ");
	debug_dec2((uint8_t)rtc_h); debug_putc(':');
	debug_dec2((uint8_t)rtc_m); debug_putc(':');
	debug_dec2((uint8_t)rtc_s);
	debug_puts(" w"); debug_i16((uint16_t)rtc_w);
	debug_puts(" d"); debug_i16((uint16_t)rtc_d);
	debug_puts("\r\n");
}
/**********************************************************************************/
#endif
/********************************************************************************/
/* Set time-of-day, weekday and day counter from `local` unix seconds (and      */
/* remember `utc` for reporting back). Matches the stock two-32-bit-numbers     */
/* time exchange. 1970-01-01 is a Thursday, so weekday (Mon=1..Sun=7) is        */
/* ((days + 3) mod 7) + 1. Values before 2000-01-01 are rejected as garbage.    */
#define RTC_UNIX_MIN_TIME   946684800UL        /* 2000-01-01 00:00:00 UTC */

void RTC_SetUnixTime(uint32_t local) {
	uint32_t day;
	uint32_t sec;
	uint32_t q;
	uint32_t rem;
	uint8_t h;
	uint8_t m;
	uint8_t s;
	uint8_t w;

	if(local < RTC_UNIX_MIN_TIME) {
		return;
	}
	if(!Mdu_DivMod32(local, 86400UL, &day, &sec)) return;
	if(!Mdu_DivMod32(sec, 3600UL, &q, &rem)) return;
	h   = (uint8_t)q;
	m   = 0;
	while(rem >= 60UL) {
		rem -= 60UL;
		m++;
	}
	/* the loop above reduced rem = sec % 3600 modulo 60, i.e. rem == sec % 60:
	   the seconds are already here, no second division needed                 */
	s   = (uint8_t)rem;
	if(!Mdu_DivMod32(day + 3UL, 7UL, &q, &rem)) return;
	w   = (uint8_t)(rem + 1UL);

	RTC_WriteTimeDay(h, m, s, w, (uint16_t)(day & 0xFFFFUL));
}
/********************************************************************************/
#if UART_DEBUG && RTC_EN
uint8_t RTC_ClockSrc(void) {
	return rtc_src;
}
#endif
/********************************************************************************/
void RTC_Init(void) {
	uint8_t i;
	uint8_t xoscl_ok = 0;

	P71F = P71_XOSCL_OUT_SETTING;
	P72F = P72_XOSCL_IN_SETTING;
	CKCON |= XLCKE;
	i = RTC_XOSCL_MAXWAIT;
	do {
		WDFLG = 0xA5;
		if(CKCON & XLSTA) {
			xoscl_ok = 1;
			break;
		}
		Delay_ms(10);
	} while(--i);

	if(xoscl_ok) {
		CKSEL &= ~RTCKS(1);                  /* RTCKS=0: RTC clock = XOSCL */
		rtc_src = RTC_SRC_XOSCL;
	}
	else {
		CKCON &= ~XLCKE;                     /* no crystal: drop XOSCL               */
		CKCON |= ILCKE;                      /* IRCL must be on (RTC clock = IRCL/4) */
		CKSEL |= RTCKS(1);                   /* RTCKS=1: RTC clock = IRCL/4          */
		rtc_src = RTC_SRC_IRCL;
	}

	/* enable the RTC module (no interrupts yet) */
	RTCON = (uint8_t)RTCE(1);
	Delay_50us(RTC_RTCE_SETTLE_US);          /* ~300 us before writing time */

	/* default time-of-day until the first ZT3L sync provides the real value    */
	RTC_WriteTimeDay(0, 0, 0, 1, 0);
}
