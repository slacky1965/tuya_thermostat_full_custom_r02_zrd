#include "include/modes.h"
#include "include/buttons.h"
#include "include/rtc.h"
#include "include/led.h"

/* Clock-set mode: entered by holding the CLOCK key while ON; steps are       */
/* weekday -> hour -> minute, then CLOCK applies the time (RTC_WriteTime)     */
/* and the schedule-set hub takes over. Body moved verbatim from main.c.      */

/* Clock-set mode (ON, CLOCK key held >= 2 s): 0 = off, 1 = set hours, 2 = set  */
/* minutes. The edited field blinks; the weekday display is blanked. Exiting    */
/* (power leaf or MENU) writes the value to the RTC.                            */
uint8_t xdata clk_set;               /* CLK_SET_*                        */
uint8_t xdata clk_set_h;             /* edited hours 0..23               */
uint8_t xdata clk_set_m;             /* edited minutes 0..59             */
uint8_t xdata clk_set_w;             /* edited weekday 1..7 (Mon=1)      */
uint8_t xdata clk_blink;             /* blink phase (toggles ~0.5 s)     */
uint16_t xdata clk_blink_tick;       /* tick10ms of the last blink flip  */

/* Draw the clock-set screen. Steps: HOUR/MIN edit the time (leading zero shown */
/* so BOTH digits blink; weekday blank); WDAY lights the selected weekday and   */
/* blinks it. Hours = words 4/5, minutes = 6/7.                                 */
void Clock_Set_Draw(void) {
	uint8_t h24 = (settings.clockMode == CLOCK_24H) ? 1 : 0;

	Led_Clock(clk_set_h, clk_set_m, 1, h24);   /* ':' steady during setup */
	if(clk_set == CLK_SET_WDAY) {
		Led_Weekday(clk_blink ? 0 : clk_set_w);   /* blink the weekday */
		Led_Flush();
		return;
	}
	Led_Weekday(0);                       /* blank Monday..Sunday (keeps NET)  */
	{
		uint8_t fld = (clk_set == CLK_SET_HOUR) ? 0 : 1;
		/* force the leading zero of the edited field visible */
		Led_Digit(fld ? 6 : 4, (uint8_t)((fld ? clk_set_m : clk_set_h) / 10));
		if(clk_blink) {
			Led_Clock_Blank(fld);
		}
	}
	Led_Flush();
}

/* Enter the clock-set mode: load the current time, blank the weekday, start    */
/* blinking the hours field. Only meaningful while ON.                          */
void Clock_Set_Enter(void) {
	XDATA_TMP(uint8_t, h);
	XDATA_TMP(uint8_t, m);
	XDATA_TMP(uint8_t, w);

	RTC_ReadTime(&h, &m, 0, &w);
	clk_set_h = h;
	clk_set_m = m;
	clk_set_w = (w >= 1 && w <= 7) ? w : 1;
	clk_set   = CLK_SET_WDAY;           /* start on the weekday step */
	clk_blink = 0;
	clk_blink_tick = BTN_Tick10ms();
	mode_idle_tick = BTN_Tick10ms();    /* arm the 20 s idle exit    */
	clock_colon = 1;
	Led_SymSet(SYM_HOME, 0);            /* leave the temperature view */
	Led_SymSet(SYM_SET, 0);
	/* a frozen %RH window (its expiry sits in the mode 'else') must not leak   */
	/* onto the clock-set screen for the whole session                          */
	Led_SymSet(SYM_HUMID, 0);
	Clock_Set_Draw();
}

void Clock_Set_Commit(void) {
	XDATA_TMP(uint8_t, current_week);
	uint16_t day;
	int8_t delta;

	RTC_ReadTime(0, 0, 0, &current_week);
	day = RTC_GetDay();
	delta = (int8_t)(clk_set_w - current_week);
	if(delta > 3) delta = (int8_t)(delta - 7);
	else if(delta < -3) delta = (int8_t)(delta + 7);
	if(delta < 0 && (uint16_t)(-delta) > day) delta = (int8_t)(delta + 7);
	RTC_WriteTimeDay(clk_set_h, clk_set_m, 0, clk_set_w,
	                 (uint16_t)(day + delta));
}
/********************************************************************************/
/* Leave the mode; keep = 1 writes the edited time to the RTC, 0 discards it.   */
void Clock_Set_Exit(uint8_t keep) {
	if(keep) {
		Clock_Set_Commit();
	}
	clk_set = CLK_SET_OFF;
	clock_colon = 1;
	adj_remain = 0;
	Thermo_Apply();                     /* restore the normal ON screen */
}

/* Blink the edited field (~0.5 s) in the clock-set or schedule-set mode.       */
void Clock_Set_Tick(void) {
	if(clk_set == CLK_SET_OFF && sched_set == SCHED_SET_OFF) return;
	if((uint16_t)(BTN_Tick10ms() - clk_blink_tick) >= 50) {   /* 0.5 s */
		clk_blink_tick = BTN_Tick10ms();
		clk_blink = !clk_blink;
		if(clk_set != CLK_SET_OFF) Clock_Set_Draw();
		else                       Sched_Set_Draw();
	}
}
