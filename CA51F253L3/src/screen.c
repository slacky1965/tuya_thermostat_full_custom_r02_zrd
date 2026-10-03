/* Screen state: brightness policy, composition of the ON/OFF screens, standby
   phases, padlock indication and the NET symbol. Extracted from main.c in
   waves; main() drives these helpers from its event drain and 1 s tick. */
#include "include/config.h"
#include "include/stdint.h"
#include "include/buttons.h"
#include "include/debug.h"
#include "include/link_proto.h"
#include "include/led.h"
#include "include/modes.h"
#include "include/relay.h"
#include "include/rtc.h"
#include "include/settings.h"
#include "include/temp_pipeline.h"

uint8_t xdata bright_hold;                /* seconds left of the max-brightness window */
/* xdata initialisers are useless: xdata_clear() zeroes XISEG at boot and      */
/* Thermo_Apply() re-arms bright_level. Kept commented for reference.          */
static uint8_t xdata bright_level /* = 0xFF */;  /* last level applied to the LEDs */

/* Base brightness for the current time of day: day 06:00-22:00, night the rest. */
uint8_t Bright_LevelBase(void) {
	uint8_t h;

	RTC_ReadTime(&h, 0, 0, 0);
	if((h >= 6) && (h < 22)) {
		return settings.currentLevel_day;
	}
	return settings.currentLevel_night;
}

/* Apply a unified brightness (0=off, 1-8) to the display + all backlights.     */
/* Only meaningful while power_on; the green leaf is only for the OFF standby.  */
void Bright_Apply(uint8_t level) {
	if(level == bright_level) return;   /* already applied (per-event drain refresh) */
	bright_level = level;
	Led_SetBrightness((uint8_t)level);
	Led_Backlight(BL_KEYS,  (uint8_t)level);
	Led_Backlight(BL_RED,   (uint8_t)level);
	Led_Backlight(BL_WHITE, (uint8_t)level);
	Led_Backlight(BL_GREEN, LED_BRIGHTNESS_OFF);
}

/* Re-arm the max-brightness window and apply it. One definition for every      */
/* place that must spike the display: a lock change, the NET blink, a mode      */
/* that owns the display, and any key event.                                    */
void Bright_MaxHold(void) {
	bright_hold = BRIGHTNESS_HOLD_SECS;
	Bright_Apply(LED_BRIGHTNESS_MAX);
}

/* Standby fade after OFF: the 5 touch keys stay at full brightness for a few  */
/* seconds, then only the green ON/OFF leaf glows (settings day/night level).  */
/* (STANDBY_KEYS_SECS lives in include/modes.h: main() arms off_hold with it.) */
uint8_t xdata off_hold;                 /* seconds left of the touch-key stay phase */
/* xdata initialiser dropped: xdata_clear() zeroes it, Thermo_Apply re-arms it. */
static uint8_t xdata green_level /* = 0xFF */; /* last BL_GREEN level while OFF */

/* Backlight pattern shared by every OFF screen: the touch keys and the green  */
/* ON/OFF leaf at the given levels, red strip and white power key off. One     */
/* definition, so the padlock blink, the NET blink and the standby phase can   */
/* never drift apart.                                                          */
void Backlights_Off(uint8_t level) {
	Led_Backlight(BL_KEYS,  level);
	Led_Backlight(BL_RED,   LED_BRIGHTNESS_OFF);
	Led_Backlight(BL_WHITE, LED_BRIGHTNESS_OFF);
	Led_Backlight(BL_GREEN, level);
}

/* OFF phase 1: display off, the 4 touch keys + green ON/OFF leaf glow at     */
/* full brightness for a few seconds; after the hold (Thermo_Apply at OFF)    */
/* the keys go off and the green leaf dims to the settings day/night level.   */
/* While the keypad is locked the padlock stays visible here too (at MAX), so */
/* it never disappears between the 3 s lock hold and the steady standby.      */
void Standby_KeysApply(void) {
	Led_SymInit();
	Lock_Sym();
	Led_SetBrightness(settings.keypadLockout ?
	                  LED_BRIGHTNESS_MAX : LED_BRIGHTNESS_OFF);
	Backlights_Off(LED_BRIGHTNESS_MAX);
	green_level = LED_BRIGHTNESS_MAX;   /* the steady standby re-applies the level */
	Led_Flush();
}

const temp_sample_t xdata *Temp_DisplaySample(void) {
	return (temp_src == SENSOR_SRC_IN) ?
		Temp_Pipeline_Get(TEMP_INTERNAL) : Temp_Pipeline_Get(TEMP_EXTERNAL);
}

/* One place that turns a pipeline snapshot into the big digits: an invalid    */
/* sample shows "Er" (the degree C stays lit) instead of a stale number.       */
/* In AL a dead probe of EITHER kind shows "Er": the pair drives the relay, so */
/* a half-dead pair must not look healthy on screen.                           */
void Temp_Draw(const temp_sample_t xdata *sample) {
	uint8_t ok;

	/* WS override: while the wireless sensor is fresh it drives the relay, so  */
	/* it owns the big digits too. Its value was validated on arrival and the   */
	/* wired probes are irrelevant here - no "Er" verdict. When it goes stale   */
	/* the very next redraw falls back to the configured probes below.          */
	if(Link_WsFresh()) {
		Led_Temp(Link_WsTemp());
		return;
	}
	ok = sample->accepted_valid;
	if(temp_src == SENSOR_SRC_ALL) {
		ok = (uint8_t)(ok &&
		     Temp_Pipeline_Get(TEMP_INTERNAL)->accepted_valid &&
		     Temp_Pipeline_Get(TEMP_EXTERNAL)->accepted_valid);
	}
	if(ok) Led_Temp(sample->accepted_c100);
	else   Led_TempErr();
}

/********************************************************************************/
void Clock_Draw(void) {
	uint8_t h;
	uint8_t m;
	uint8_t h24;

	RTC_ReadTime(&h, &m, 0, 0);
	h24 = (settings.clockMode == CLOCK_24H) ? 1 : 0;
	Led_Clock(h, m, clock_colon, h24);
	Led_Flush();      /* push now: a caller must never rely on the next 1 s tick */
}

/*********************************************************************************/
/* Re-apply the green ON/OFF leaf to the current day/night level while OFF.      */
/* Called once per second from the standby branch so the leaf follows 06:00 and  */
/* 22:00 (and a host time sync) without waiting for a power toggle.              */
/* The standby padlock is driven by the SAME helper: it lives on the display     */
/* brightness, so a day/night or remote brightness change must move it together  */
/* with the leaf. The register write is idempotent; the helper only runs in      */
/* steady standby, where nothing else owns the display.                          */
void Standby_GreenApply(void) {
	uint8_t lvl = Bright_LevelBase();

	if(lvl != green_level) {
		green_level = lvl;
		Led_Backlight(BL_GREEN, lvl);
	}
	/* the padlock rides on the display brightness: re-assert it every second so  */
	/* it tracks the same level as the leaf (it was last written by Thermo_Apply  */
	/* or Standby_KeysApply, not by this helper).                                 */
	Led_SetBrightness(settings.keypadLockout ? lvl : LED_BRIGHTNESS_OFF);
}

/* OFF feedback for the lock (chord or remote 0x17): blink the padlock on the   */
/* dark display so the toggle is visible. One phase = 300 ms, 10 phases = 5     */
/* full blinks (~3 s) at full brightness. Phases only own the blink itself:     */
/* afterwards a locked unit keeps the padlock glowing at the settings day/night */
/* level next to the green leaf.                                                */
#define LOCK_FLASH_PHASES  10
#define LOCK_FLASH_TICKS   30
#define LOCK_FLASH_LEVEL   LED_BRIGHTNESS_MAX
uint8_t xdata lock_flash;                 /* phases left to run                         */
uint8_t xdata trace_tail;                 /* LOCK_TRACE_EN: seconds of probe life after */
                                          /* the blink, so the post-blink state is      */
                                          /* visible (all probes test lock_flash||this) */
static uint16_t xdata lock_flash_tick;    /* tick10ms of the last flip                  */
static uint8_t xdata lock_flash_lit;      /* padlock currently lit                      */

/* Padlock symbol follows settings.keypadLockout; one definition for the three  */
/* places that draw it (power-on screen, lock change, 1 s redraw).              */
void Lock_Sym(void) {
	Led_SymSet(SYM_LOCK, settings.keypadLockout != 0);
}

/* ON: a keypad-lock change (local chord or remote 0x17) gets a fresh-ON        */
/* brightness spike and redraws the padlock at once, so the change is visible   */
/* on an otherwise dimmed display. One definition for both entry points.        */
void Lock_On_Screen(void) {
	Bright_MaxHold();
	Lock_Sym();
	Led_Flush();
}

/* Thermostat state: power always startup OFF.                                  */
bit power_on;    /* 0 = off (green leaf), 1 = on (white leaf + display)  */
bit ext_ok;      /* 1 = external probe present (SYM_HEATER)              */

/* "NET" (SYM_NET) blink: started by a >=2 s DOWN hold while OFF. Blinks OVER  */
/* whatever is on screen (dark in OFF, the normal UI in ON) for 1.5 min, then  */
/* stops by itself. A future ZT3L network event may stop it earlier / turn the */
/* symbol on steady (see Net_Flash_Stop()/Net_Set_Steady() below).             */
/* (NET_HOLD_10MS_TICKS stays in main.c: it arms down_arm in the key drain.)   */
#define NET_FLASH_SECS   90                  /* 1.5 minutes                              */
#define NET_FLASH_TICKS  30                  /* 300 ms per half-phase                    */
uint8_t xdata net_flash;                     /* 1 = blink active                         */
static uint8_t xdata net_flash_lit;          /* current phase (1 = lit)                  */
uint8_t xdata net_flash_secs;                /* seconds left before auto-stop            */
static uint16_t xdata net_flash_tick;        /* tick10ms of the last flip                */
static uint8_t xdata net_steady;             /* 1 = NET on steady (future ZT3L)          */
uint8_t xdata net_hold;                      /* OFF: seconds left of the steady          */
                                             /* NET display before the standby fade      */
/*****************************************************************************************/
/* Apply the whole LED state (display + all backlights) for power_on/off.       */
/* The manual/schedule icon pair: ONE mapping for the remote window, the        */
/* local MENU click and this repaint (was three hand-written copies that had    */
/* already drifted in formatting).                                              */
void ProgMode_Icons(uint8_t pm) {
	Led_SymSet(SYM_HAND,  (pm == PROG_MODE_MANUAL)  ? 1 : 0);
	Led_SymSet(SYM_CLOCK, (pm == PROG_MODE_SCHEDULE) ? 1 : 0);
}

/* 1 when NO settings mode owns the screen. Single source for the six           */
/* clk/sched/menu triple compares that used to live in main.c (two spellings).  */
uint8_t Screen_ModeFree(void) {
	return (uint8_t)(clk_set == CLK_SET_OFF && sched_set == SCHED_SET_OFF &&
	                 menu_set == MENU_OFF);
}

void Thermo_Apply(void) {
	const temp_sample_t xdata *sample = Temp_DisplaySample();

	bright_level = 0xFF;   /* power change: force a full brightness re-apply */
	green_level  = 0xFF;   /* power change: force a green-leaf re-apply      */

	if(power_on) {
		/* power-on: max brightness for the hold window (decays to the day/night level) */
		bright_hold = BRIGHTNESS_HOLD_SECS;
		Bright_Apply(LED_BRIGHTNESS_MAX);
		Led_SymSet(SYM_HOME, 1);
		Led_SymSet(SYM_HEATER, ext_ok);
		Led_SymSet(SYM_HEAT, 0);         /* updated by the 1 s control tick */
		Lock_Sym();
		Led_SymSet(SYM_NET,  net_steady);  /* steady when joined the network */
		ProgMode_Icons(settings.progMode);
		Led_Weekday(RTC_GetWeek());      /* light the current day-of-week */
		clock_colon = 1;                 /* ':' starts lit                */
		Clock_Draw();
		Temp_Draw(sample);
	}
	else {
		/* standby: display off, only the green ON/OFF leaf glows at the         */
		/* settings day/night level - plus the padlock while the keypad is       */
		/* locked, at that same level (after the 3 s hold it keeps glowing       */
		/* together with the leaf; level 0 = everything dark, as before).        */
		Relay_Set(0);                    /* heating off whenever the unit is off */
		Led_SymInit();
		Lock_Sym();
		Led_SetBrightness(settings.keypadLockout ?
		                  Bright_LevelBase() : LED_BRIGHTNESS_OFF);
		Backlights_Off(LED_BRIGHTNESS_OFF);
		Standby_GreenApply();
		Led_Flush();
	}
}
/******************************************************************************/
/* OFF provisioning screen: display ON at max, 4 touch keys + green leaf at   */
/* max, red/white off, clean shadow buffer (only NET toggles over it). Used   */
/* by the NET blink and restored whenever that blink is interrupted.          */
/* Screen ownership while OFF: the padlock blink (Lock_Flash) > NET blink >   */
/* standby key phase > steady standby. Every entry point below therefore asks */
/* whether lock_flash is running and leaves the screen alone if it is; only   */
/* Lock_Flash() itself paints, and its completion restores the right screen.  */
void NetBlink_Screen(void) {
	off_hold = 0;
	Led_SymInit();
	Backlights_Off(LED_BRIGHTNESS_MAX);
	Led_SetBrightness(LED_BRIGHTNESS_MAX);
	Led_Flush();
}

void Lock_Flash_Start(void) {
	lock_flash = LOCK_FLASH_PHASES;
	lock_flash_tick = BTN_Tick10ms();
	lock_flash_lit = 0;                 /* the first Lock_Flash() flips to ON */
	DEBUG(LOCK_TRACE_EN, { debug_putc('!'); });

	/* Own the screen right now and show a DARK panel: the buffer holds no      */
	/* symbols (Led_SymInit clears + flushes), every backlight is off, and      */
	/* the display keeps RUNNING at LOCK_FLASH_LEVEL - from here on only the    */
	/* SYM_LOCK bit is toggled, exactly like Net_Flash() does for SYM_NET       */
	/* (no clock stop, no BLNK - each produced its own visual artifact). Cut    */
	/* the other OFF phases too (same treatment NetBlink_Screen gives           */
	/* off_hold): neither the 7 s key-stay nor the NET steady hold may re-light */
	/* the keys behind or after the blink.                                      */
	off_hold = 0;
	net_hold = 0;
	Led_SymInit();
	Backlights_Off(LED_BRIGHTNESS_OFF);
	Led_SetBrightness(LOCK_FLASH_LEVEL);
}

/* one 10 ms-driven phase of the padlock blink; call from the main loop while */
/* the thermostat is OFF. Aborts if the unit is turned back on mid-flash.     */
void Lock_Flash(void) {
	uint16_t e;

	if(!lock_flash) return;
	if(power_on) {
		lock_flash = 0;                 /* power changed mid-flash: stop it */
		return;
	}
	e = (uint16_t)(BTN_Tick10ms() - lock_flash_tick);
	if(e < LOCK_FLASH_TICKS) return;
	lock_flash_tick = BTN_Tick10ms();
	/* toggle ONLY the symbol bit - the panel, the clock and the brightness     */
	/* stay untouched for the whole blink (Net_Flash style). The buffer holds   */
	/* nothing else, so the "off" phase is an empty (visually dark) panel.      */
	lock_flash_lit = !lock_flash_lit;
	Led_SymSet(SYM_LOCK, lock_flash_lit);
	DEBUG(LOCK_TRACE_EN, { debug_putc(lock_flash_lit ? (uint8_t)'+' : (uint8_t)'-'); });
	Led_Flush();
	if(--lock_flash == 0) {
		DEBUG(LOCK_TRACE_EN, { debug_putc('#'); trace_tail = 2; });
		/* hand the screen back in priority order: a provisioning blink that was
		   interrupted, the NET steady hold, then the standby phases.            */
		if(net_flash)     NetBlink_Screen();
		else if(off_hold) Standby_KeysApply();
		else if(net_hold) {   /* a NET steady hold was interrupted: restore it */
			NetBlink_Screen();
			Led_SymSet(SYM_NET, 1);
			Led_Flush();
		}
		else              Thermo_Apply();
	}
}
/********************************************************************************/
/* NET (SYM_NET) blink: >=2 s DOWN hold while OFF arms a 1.5 min blink.         */
void Net_Flash_Start(void) {
	net_flash = 1;
	net_flash_lit = 0;
	net_flash_secs = NET_FLASH_SECS;
	net_flash_tick = BTN_Tick10ms();
	if(power_on) {
		/* hold full brightness for the whole blink */
		Bright_MaxHold();
	}
	else if(!lock_flash) {
		/* OFF: end the touch-key stay phase; while the blink runs the 5 touch */
		/* keys + green leaf stay at FULL brightness, the display ON at max    */
		/* (only NET toggles), so every flash is identical. A running padlock  */
		/* blink keeps the screen: it restores the NET screen when it ends.    */
		NetBlink_Screen();
	}
}

/* stop the blink now (auto after 1.5 min, or a future ZT3L network event)      */
void Net_Flash_Stop(void) {
	net_flash = 0;
	net_flash_lit = 0;
	Led_SymSet(SYM_NET, net_steady);     /* keep it lit if steady mode is on    */
	if(lock_flash) return;               /* the padlock blink owns the screen;  */
	                                     /* Lock_Flash() restores it at the end */
	if(power_on) {
		Led_Flush();
	}
	else {
		Led_SetBrightness(LED_BRIGHTNESS_OFF);   /* dark screen; standby follows */
		Led_Flush();
	}
}

/* future ZT3L hook: device joined the Zigbee network -> NET on steady forever  */
static void Net_Set_Steady(uint8_t on) {
	net_steady = on;
	if(on) {
		if(net_flash && !power_on && !lock_flash) {
			/* provisioning (OFF DOWN hold) finished: keep the display and the   */
			/* touch keys lit with NET steady for the same time as a normal OFF  */
			/* key-stay phase (STANDBY_KEYS_SECS), then the 1 s standby tick     */
			/* fades to the green-leaf-only state (like OFF).                    */
			net_hold  = STANDBY_KEYS_SECS;
			off_hold  = 0;
			/* the same screen a padlock blink restores when it ends here */
			NetBlink_Screen();
			Led_SymSet(SYM_NET, 1);
			Led_Flush();
		}
		net_flash = 0;
		net_flash_lit = 0;
	}
	Led_SymSet(SYM_NET, on);
	if(power_on) Led_Flush();
}

/* Follow the link net status: when the peer reports CONNECTED the NET symbol   */
/* stops blinking and stays lit (shown while ON); otherwise it is cleared.      */
void Net_StatusApply(void) {
	uint8_t conn = (Link_NetStatus() == LNK_NET_CONNECTED) ? 1 : 0;

	if(conn == net_steady) return;
	Net_Set_Steady(conn);
}

/* one 10 ms-driven half-phase of the NET blink; call from the main loop.       */
/* Always drawn OVER the current screen: in OFF it lights the symbol alone on   */
/* the dark display (like the padlock), in ON it toggles it on the live UI.     */
void Net_Flash(void) {
#if !FACTORY_RESET_TEST_EN
	if(net_steady) return;             /* steady NET owns the symbol */
#endif
	if(net_flash) {
		/* auto-stop after NET_FLASH_SECS, counted on the real 10 ms tick */
		if((uint16_t)(BTN_Tick10ms() - net_flash_tick) >= NET_FLASH_TICKS) {
			net_flash_tick = BTN_Tick10ms();
			net_flash_lit = !net_flash_lit;
			/* overlay on the live UI while ON, symbol alone on the dark OFF      */
			/* display; a running padlock blink keeps the screen to itself.       */
			if(power_on || !lock_flash) {
				Led_SymSet(SYM_NET, net_flash_lit);
				Led_Flush();
			}
		}
	}
}
