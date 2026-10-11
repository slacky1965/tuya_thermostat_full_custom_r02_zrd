#include "include/config.h"
#include "include/modes.h"
#include "include/buttons.h"
#include "include/led.h"
#include "include/chars.h"                     /* CH_MINUS for the "--" field */
#include "include/rtc.h"
#include "include/link_proto.h"
#include "include/ota.h"

/* Weekly-schedule editor (schedule-set mode): 7 days x 6 slots, day-select  */
/* hub first; entered after the minute step of clock-set. Body moved         */
/* verbatim from main.c.                                                     */

/* Schedule-set mode (entered after the minutes step of clock-set): 7 days x 6  */
/* slots; each slot edits start time (h/m) and heat temperature. Starts on a    */
/* day-select hub (UP/DOWN pick the weekday, CLOCK enters it) so days can be    */
/* skipped; then "CLOCK" walks hours -> minutes -> temperature -> next slot ->  */
/* next day. Leaving with POWER/MENU commits the current slot and saves; slots  */
/* left untouched stay empty (0xFFFF), so fewer than 6 slots per day are        */
/* allowed.                                                                     */
uint8_t xdata sched_set;                    /* SCHED_SET_*                       */
uint8_t xdata sched_hub;                    /* 1 = day-select hub, 0 = editing   */
uint8_t xdata sched_day;                    /* 0..6 (Mon..Sun)                   */
uint8_t xdata sched_slot;                   /* 0..5                              */
uint8_t xdata sched_field;                  /* SCHED_FLD_*                       */
uint8_t xdata sched_h;                      /* edited hours   0..23              */
uint8_t xdata sched_m;                      /* edited minutes 0..59              */
int16_t xdata sched_temp;                   /* edited heat temperature x10       */
uint8_t xdata sched_h_set;                  /* 1 = hours field set (load/edit)   */
uint8_t xdata sched_m_set;                  /* 1 = minutes field set (load/edit) */
uint8_t xdata sched_dirty;                  /* 1 = pending report for this day   */
uint8_t xdata sched_send_mask;              /* days persisted but not yet sent   */
uint8_t xdata sched_changed;                /* 1 = edited at all this session:   */
                                            /* controls settings_save on exit    */

/********************************************************************************/
/* Schedule-set helpers.                                                        */
static schedule_t xdata *Sched_DayPtr(uint8_t day) {
	return Sched_DayRow((uint8_t)(day > 6 ? 6 : day));
}

/* Load the current slot into the working registers. An unused/invalid slot     */
/* leaves both time fields unset, so the editor draws "--:--" instead of a fake */
/* 00:00; the temperature is seeded from the occupied setpoint and only becomes */
/* real data once both time fields have been entered (see Sched_EntryValid).    */
void Sched_Set_Load(void) {
	schedule_t xdata *p = &Sched_DayPtr(sched_day)[sched_slot];

	if(!Sched_EntryValid(p)) {
		sched_h = 0;
		sched_m = 0;
		sched_temp = settings.occupiedHeatingSetpoint;
		sched_h_set = 0;
		sched_m_set = 0;
	}
	else {
		uint16_t minute = p->minute;

		sched_h = 0;
		while(minute >= 60) {
			minute -= 60;
			sched_h++;
		}
		sched_m = (uint8_t)minute;
		sched_temp = p->temperature;
		sched_h_set = 1;
		sched_m_set = 1;
	}
}

/* Store the working registers into the current slot. The slot becomes a real   */
/* transition only when BOTH time fields have been set: an edit that cannot     */
/* change the stored image - temperature only, or just one of the time fields - */
/* keeps the empty marker and never queues a day push.                          */
void Sched_Set_Store(void) {
	schedule_t xdata *p = &Sched_DayPtr(sched_day)[sched_slot];

	if(!(sched_h_set && sched_m_set)) {
		/* Empty slot: time marker + NO temperature. The old code stored the    */
		/* current setpoint here because the peer used to validate every entry  */
		/* of the day image - it no longer does, and a stale value in an unused */
		/* slot is exactly the junk we do not want.                             */
		p->minute = LNK_SCHED_TIME_EMPTY;
		p->temperature = 0;
		sched_dirty = 0;               /* nothing changed: do not push the day  */
	}
	else {
		p->minute = (uint16_t)sched_h * 60 + sched_m;
		p->temperature = sched_temp;
		if(sched_dirty) {
			sched_dirty = 0;
			sched_send_mask |= (uint8_t)(1U << sched_day);
		}
	}
}

void Sched_Set_Draw(void) {
	if(sched_hub) {
		/* day-select hub: only the weekday icon, blinking as the cursor */
		Led_SymInit();
		Led_Weekday(clk_blink ? 0 : (uint8_t)(sched_day + 1));
		Led_Flush();
		return;
	}
	Led_SymInit();
	Led_Weekday((uint8_t)(sched_day + 1));           /* current day (Mon=1)   */
	Led_SymSet((uint8_t)(SYM_SP1 + sched_slot), 1);  /* current slot SP1..SP6 */
	Led_SymSet(SYM_HOME, 0);
	Led_SymSet(SYM_SET, 0);
	Led_Clock(sched_h, sched_m, 1, 1);          /* ':' steady            */
	Led_Digit(4, (uint8_t)(sched_h / 10));      /* leading zeros visible */
	Led_Digit(6, (uint8_t)(sched_m / 10));
	/* "--" for a time field the user has not set: an unused slot must not    */
	/* pretend to hold 00:00. A set field keeps its digits, so "hours set,    */
	/* minutes not" reads 12:-- and the minutes keep blinking as dashes       */
	/* through the Led_Clock_Blank pass below. Led_Char preserves bit 7, so   */
	/* the ':' of word 6 survives.                                            */
	if(!sched_h_set) {
		Led_Char(4, CH_MINUS);
		Led_Char(5, CH_MINUS);
	}
	if(!sched_m_set) {
		Led_Char(6, CH_MINUS);
		Led_Char(7, CH_MINUS);
	}
	Led_Temp(sched_temp);                       /* temperature in its digits + flush */
	if(clk_blink) {
		if(sched_field == SCHED_FLD_HOUR)      Led_Clock_Blank(0);
		else if(sched_field == SCHED_FLD_MIN)  Led_Clock_Blank(1);
		else                                   Led_Temp_Blank();
	}
	Led_Flush();
}

void Sched_SendPending(void) {
	uint8_t day;

	if(Link_PeerAlive() && !Ota_IsPending()) {
		for(day = 0; day < 7; day++) {
			if((sched_send_mask & (uint8_t)(1U << day)) &&
			   Link_SendSchedule(day))
				sched_send_mask &= (uint8_t)~(1U << day);
		}
	}
}

void Sched_Set_Enter(void) {
	sched_set = SCHED_SET_ON;
	sched_hub = 1;                      /* start on the day-select hub */
	sched_day = 0;                      /* always start on Monday      */
	sched_slot = 0;
	sched_field = SCHED_FLD_HOUR;
	sched_dirty = 0;                    /* nothing pending yet                 */
	/* sched_send_mask is NOT touched: it tracks days persisted but not yet    */
	/* delivered to the peer, and that lifetime ends with DELIVERY (the        */
	/* main-loop drain), never with an editor session.                         */
	sched_changed = 0;                  /* nothing edited yet                  */
	clk_blink = 0;
	clk_blink_tick = BTN_Tick10ms();
	mode_idle_tick = BTN_Tick10ms();    /* arm the 20 s idle exit    */
	Sched_Set_Draw();
}

/* Advance: hours -> minutes -> temperature -> next slot; after slot 5 save the */
/* finished day and move to the next one. After Sunday, leave the mode (saved). */
void Sched_Set_Next(void) {
	if(sched_field < SCHED_FLD_TEMP) {
		sched_field++;
		Sched_Set_Draw();
		return;
	}
	/* temperature done: commit the slot and advance */
	Sched_Set_Store();
	if(sched_slot < (LNK_SCHED_N - 1)) {
		sched_slot++;
		sched_field = SCHED_FLD_HOUR;
		Sched_Set_Load();
		Sched_Set_Draw();
		return;
	}
	/* last slot of the day: save the finished day and return to the day-      */
	/* select hub with the NEXT day preselected (UP/DOWN then jump anywhere).  */
	if(sched_changed) {
		if(Settings_Persist()) Sched_SendPending();
		else sched_send_mask = 0;
	}
	if(sched_day < 6) {
		sched_day++;
		sched_slot = 0;
		sched_field = SCHED_FLD_HOUR;
		sched_hub = 1;
		Sched_Set_Draw();
		return;
	}
	Sched_Set_Exit();        /* whole week done */
}

void Sched_Set_Exit(void) {
	sched_set = SCHED_SET_OFF;
	Led_SymInit();                        /* clear the SP slot / weekday icons */
	Thermo_Apply();
}

/* Return the schedule row (6 entries) for a weekday 1..7 (Mon=1, matches RTC).  */
static const schedule_t xdata *Sched_Day(uint8_t week) {
	return Sched_DayRow((uint8_t)((week >= 1 && week <= 6) ? (week - 1) : 6));
}

/* A schedule entry counts as used only if it is sane: not the explicit empty   */
/* marker, not the all-zero default, transTime <= 24h, temperature within the   */
/* absolute setpoint limits. Anything else is ignored (guards against a bad     */
/* host write / stale flash).                                                   */
uint8_t Sched_EntryValid(const schedule_t xdata *e) {
	uint16_t t = e->minute;
	int16_t  v = e->temperature;

	if(t == LNK_SCHED_TIME_EMPTY)           return 0;
	if(t == 0 && v == 0)                    return 0;
	if(t > LNK_SCHED_DAY_MAX_MIN)           return 0;
	if(v < ABS_MIN_HEATSETPOINT_LIMIT_DEF)  return 0;
	if(v > ABS_MAX_HEATSETPOINT_LIMIT_DEF)  return 0;
	return 1;
}

/* Setpoint that applies NOW:
   - schedule bit (PROG_MODE_SCHEDULE): the transition with the GREATEST
     transTime <= current minutes, regardless of storage order (unused entries
     are skipped, see Sched_EntryValid). Before the day's first transition there
     is no match, so fall back to occupiedHeatingSetpoint.
   - manual (schedule bit clear): occupiedHeatingSetpoint.
   - eco bit (PROG_MODE_ECO): a fixed control-only offset is subtracted before
     the clamp below, so Eco never rewrites the stored values.
   Every path then passes the same use-time clamp into [min..max]: the stored
   values keep their ABS-valid image (data != policy), only CONTROL is bounded.
   This also restores the safety-cut invariant: with SP_used <= max the normal
   OFF point (SP_used + deadBand) always stays below the trip
   (max + ABS_MAX_DEADBAND + 1 degC) by at least the trip margin.          */
int16_t Sched_ActiveSetpoint(void) {
	XDATA_TMP(uint8_t, h);
	XDATA_TMP(uint8_t, m);
	XDATA_TMP(uint8_t, w);
	uint16_t now;
	uint16_t best = 0;
	uint8_t  found = 0;
	const schedule_t xdata *day;
	int16_t  t = settings.occupiedHeatingSetpoint;
	uint8_t  i;

	if(settings.progMode & PROG_MODE_SCHEDULE) {
		RTC_ReadTime(&h, &m, 0, &w);
		now = (uint16_t)h * 60 + m;
		day = Sched_Day(w);
		for(i = 0; i < LNK_SCHED_N; i++) {
			if(!Sched_EntryValid(&day[i])) continue;
			if(day[i].minute > now) continue;
			if(!found || day[i].minute >= best) {
				best  = day[i].minute;
				t     = day[i].temperature;
				found = 1;
			}
		}
	}
	/* Economy (ZCL POM bit 2): fixed offset on CONTROL only. Applied BEFORE the  */
	/* use-time clamp so the result stays inside [min..max] (Eco never heats      */
	/* below the min limit) and after both mode paths, so it works in manual AND  */
	/* in schedule mode. The stored setpoint/slot keeps its original value.       */
	if(settings.progMode & PROG_MODE_ECO) {
		t -= ECO_SETPOINT_OFFSET_C100;
	}
	/* Use-time policy (CA-2): the active setpoint is clamped into [min..max]   */
	/* for CONTROL only - the stored manual value / schedule slot keeps the     */
	/* ABS-valid image it was saved with, so moving the limits changes the      */
	/* behaviour immediately and never rewrites the data. Applies to MANUAL     */
	/* too: a setpoint outside the limits (menu item 6 or two in-order hub      */
	/* writes) otherwise reaches the relay raw and drags the safety cut-off     */
	/* below the normal OFF point.                                              */
	if(t < settings.minHeatSetpointLimit) {
		t = settings.minHeatSetpointLimit;
	}
	else if(t > settings.maxHeatSetpointLimit) {
		t = settings.maxHeatSetpointLimit;
	}
	return t;
}

/* Active setpoint used for control/display: MANUAL -> occupiedHeatingSetpoint;   */
/* SCHEDULE -> the running weekly-schedule transition (see Sched_ActiveSetpoint). */
/* xdata initialiser dropped: xdata_clear() zeroes it, the 1 s tick recomputes.   */
int16_t xdata target_setpoint /* = OCCUPIED_HEATSETPOINT_DEF */;
