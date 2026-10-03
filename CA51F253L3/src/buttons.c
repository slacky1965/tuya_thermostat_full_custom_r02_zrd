#include "include/stdint.h"
#include "include/config.h"
#include "include/ca51f2sfr.h"
#include "include/ca51f2xsfr.h"
#include "include/gpiodef_f2.h"
#include "include/system_clock.h"
#include "include/delay.h"
#include "include/mdu.h"
#include "include/settings.h"
#include "include/buttons.h"
#include "include/debug.h"

/********************************************************************************/
/* Touch button driver (from tested v11, monolithic before).
 * Working recipe: TK_CAP pin P5.7 must be in touch function (P57F=4),
 * buttons P3.7/P3.6/P3.3/P3.2/P4.7 = TK3..TK7, group slots 0..4 as
 * NPOL|(TK#+1), slot 5 = internal reference cap (0x59), one TKST collects
 * the whole group (TKIF=0x1F), then read TKMS per slot by INDEX.
 * Reference: VDD charge + VDD comparator (TKCON VRS=2, TKPWC=0x40).
 * Press direction: comp - raw is positive on press; the base used for the
 * comparison is rescaled by ref_now/ref_base (common-mode drift from
 * temperature/humidity/supply cancels out). Per-key thresholds with
 * hysteresis (release at half) and a noise floor that freezes the baseline
 * tracking near a press/EMI. Events: CLICK/HOLD/HOLD_REPEAT/RELEASE.
*********************************************************************************/

#define TK_CONF         3       /* consecutive scans to accept a press          */
#define CAL_SCANS       32      /* scans averaged into the baseline             */
#define WET_KEYS        3       /* simultaneous over-threshold keys = water     */

/* event queue: fixed size, events are only drained by BTN_GetEvent()           */
#define EVQ_SIZE        8

/* anti-drift reference channel: 6th group slot = internal reference cap,       */
/* TKPS=0x19 (user guide Table 25-5-6), NPOL included -> 0x59 (as factory).     */
#define TK_REF_SLOT     5
#define TK_REF_CHSEL    0x40 | 0x19

/* per-key press thresholds (K3..K7 = menu/clock/power/up/down)                 */
/* NOTE: tuned under SOURCE_INNER. VDD reference changes the raw scale, so      */
/* these need re-calibration on hardware after the reference switch.            */
static const uint8_t tk_hyst[BTN_COUNT] = { 110, 70, 55, 40, 45 };
static const uint8_t tk_noise[BTN_COUNT] = { 27, 17, 13, 10, 11 };  /* hyst/4   */
static const uint8_t tkch[BTN_COUNT]    = { 3, 4, 5, 6, 7 };        /* TK3..TK7 */

static uint16_t  xdata tkv[BTN_COUNT];
static uint16_t  xdata base[BTN_COUNT];
static int       xdata cnt[BTN_COUNT];
static uint8_t xdata on[BTN_COUNT];

/* timing/event machinery lives in xdata on purpose: it would otherwise push   */
/* the 128-byte DATA bank over the top (was an L107 overflow before moving).   */
static uint16_t  xdata down_tick10[BTN_COUNT];  /* tick10ms when key went down */
static uint16_t  xdata hold_tick10[BTN_COUNT];  /* tick10ms when HOLD fired    */

/* Real-time 10 ms tick: NOT owned here. main.c has the one global that the  */
/* Timer0 10 ms ISR advances every real 10 ms (the same counter driving the  */
/* 1 s clock). This module only READS it via BTN_Tick10ms(). One touch scan  */
/* takes ~20..30 ms, so HOLD/REPEAT MUST NOT be counted in conversions:      */
/* HOLD/REPEAT fire at real 0.5 s / 60 ms: 50 ticks = 0.5 s, 6 = 60 ms.      */
static uint8_t xdata was_hold[BTN_COUNT];     /* HOLD already reported       */

/* Two-button chord (UP+DOWN -> keypad lock). State survives across scans:     */
/* from the moment BOTH keys are down the gesture owns them (no individual     */
/* events) until BOTH are up again. BTN_EVT_CHORD fires once after the hold.   */
static uint8_t xdata pdown[BTN_COUNT];        /* debounced down state snapshot */
static uint8_t xdata chord_on;                /* both chord keys are/were down */
static uint8_t xdata chord_fired;             /* BTN_EVT_CHORD already queued  */
/* chord_tick doubles as the pair start reference: it is stamped at the press  */
/* edge of the FIRST chord key (whichever it is, both press orders) while the  */
/* partner is still up, and the chord block only reads it when !chord_on.      */
static uint16_t xdata chord_tick;             /* tick10ms when chord began     */

static uint16_t  xdata ebase[BTN_COUNT];        /* ref-rescaled base this scan */

static uint8_t xdata evq_key[EVQ_SIZE];
static uint8_t xdata evq_ev[EVQ_SIZE];
static uint8_t xdata evq_in, evq_out;

static uint16_t  xdata tk_ref;              /* reference channel raw        */
static uint16_t  xdata ref_base;            /* reference raw at calibration */

static uint16_t  xdata tk_time;
static uint8_t xdata tk_busy;               /* 1 = a group conversion is running  */
/****************************************************************************/
static void evq_push(btn_id_t b, btn_evt_t ev) {
	uint8_t next = (uint8_t)((evq_in + 1) & (EVQ_SIZE - 1));   /* EVQ_SIZE is a power of 2 */

	if(next == evq_out) return;      /* full: drop the incoming event (queue is a diagnostic aid) */
	evq_key[evq_in] = (uint8_t)b;
	evq_ev[evq_in]  = (uint8_t)ev;
	evq_in = next;
}
/****************************************************************************/
static void Touch_IO_Init(void) {
	P57F = P57_CAP_SETTING;            /* TK_CAP: external modulation cap    */
	P37F = P37_TK3_SETTING;            /* buttons P3.7/P3.6/P3.3/P3.2/P4.7   */
	P36F = P36_TK4_SETTING;
	P33F = P33_TK5_SETTING;
	P32F = P32_TK6_SETTING;
	P47F = P47_TK7_SETTING;
}

static void Touch_Module_On(void) {
	CKCON |= TFCKE;
	Delay_ms(30);
	Touch_IO_Init();

	TKCFG = 0x0A;                        /* TKDIV=0, TKTMS=10 (as demo)        */
	TKCON = 0x42;                        /* VRS=2: VDD charge + VDD comparator */
	TKPWC = 0x40;                        /* VDD preset: VDS/VIRS/TKPWS/TKCVS=0 */
	TKIF  = 0x3F;
}

static void Touch_Group_Config(void) {
	uint8_t i;

	for(i = 0; i < 6; i++) {
		INDEX = i;
		if(i < BTN_COUNT) {
			TKCHS = 0x40 | (tkch[i] + 1);      /* NPOL | channel */
		}
		else {
			TKCHS = TK_REF_CHSEL;              /* NPOL | internal reference cap */
		}
	}
}

/* start one group conversion; the whole group (slots 0..TK_REF_SLOT) is     */
/* collected by the controller and read out when the conversion finishes.    */
static void Touch_Scan_Start(void) {
	if(tk_busy) return;
	TKIF = 0x3F;
	TKCON |= 0x80;                        /* TKST */
	tk_time = 0;
	tk_busy  = 1;
}

/* non-blocking completion poll: 1 = finished (slot values copied into         */
/* tkv[]/tk_ref, busy state cleared), 0 = still converting (call again later). */
static uint8_t Touch_Scan_Poll(void) {
	uint8_t i;

	if(!tk_busy) return 1;

	if((TKCON & 0x80) && ((TKIF & 0x3F) == 0)) {
		if(++tk_time >= 0x8000)           /* hung controller: drop the cycle  */
		{
			tk_busy = 0;
			TKIF = 0x3F;
		}
		return 0;
	}

	for(i = 0; i <= TK_REF_SLOT; i++) {
		INDEX = i;
		if(i < BTN_COUNT) {
			tkv[i] = ((uint16_t)TKMSH << 8) | TKMSL;
		}
		else {
			tk_ref = ((uint16_t)TKMSH << 8) | TKMSL;
		}
	}
	tk_busy = 0;
	return 1;
}

/* blocking one-shot, boot calibration only: start and spin until finished.  */
static void Touch_Scan_Once(void) {
	Touch_Scan_Start();
	while(!Touch_Scan_Poll()) {
		WDFLG = 0xA5;
	}
}
/*****************************************************************************/
void BTN_Init(void) {
	uint8_t i, j;
	uint32_t sum[BTN_COUNT];
	uint32_t sum_ref = 0;

	Touch_Module_On();
	Touch_Group_Config();

	for(i = 0; i < BTN_COUNT; i++) {
		sum[i] = 0;
		cnt[i] = 0;
		down_tick10[i] = 0;
		hold_tick10[i] = 0;
		was_hold[i]  = 0;
		on[i]        = 0;
	}
	evq_in = evq_out = 0;

	for(i = 0; i < CAL_SCANS; i++) {
		tk_time = 0;
		Touch_Scan_Once();
		WDFLG = 0xA5;
		for(j = 0; j < BTN_COUNT; j++) sum[j] += tkv[j];
		sum_ref += tk_ref;
	}
	for(i = 0; i < BTN_COUNT; i++) {
		base[i] = (uint16_t)(sum[i] / CAL_SCANS);
	}
	ref_base = (uint16_t)(sum_ref / CAL_SCANS);
}

void BTN_Scan(void) {
	uint8_t i;
	uint8_t pair_start;

	if(!tk_busy) {
		Touch_Scan_Start();               /* idle: kick off the next cycle */
		return;
	}
	if(!Touch_Scan_Poll()) return;        /* still converting: try again later */

	uint8_t jumps = 0;

	for(i = 0; i < BTN_COUNT; i++) {
		uint32_t eb;
		uint32_t er;
		int d;

		/* ref-rescaled base: mirrors TS_Lib (base * ref_now / ref_base) so a    */
		/* common-mode shift (temperature/humidity/supply) cancels out. Outside  */
		/* the sane [ref/2, 2*ref] window we fall back to the plain base.        */
		if(tk_ref && ref_base) {
			uint32_t lo = (uint32_t)ref_base >> 1;
			uint32_t hi = (uint32_t)ref_base << 1;

			if((uint32_t)tk_ref >= lo && (uint32_t)tk_ref <= hi) {
				if(!Mdu_DivMod32(Mdu_Mul16(base[i], tk_ref), ref_base,
				                  &eb, &er)) {
					eb = base[i];
				}
			}
			else {
				eb = base[i];
			}
		}
		else {
			eb = base[i];
		}
		ebase[i] = (uint16_t)eb;

		d = (int)eb - (int)tkv[i];
		/* moisture/EMI guard: a wet film shoves several channels at once,      */
		/* while a real press moves one (rarely two).                           */
		if(d < 0) d = -d;
		if(d >= tk_hyst[i]) jumps++;
	}
	/* The wet verdict is taken BEFORE cnt[] is updated and drops the whole    */
	/* press state, so a wet film can neither be promoted to a press nor leave */
	/* a phantom edge behind when it dries.                                    */
	if(jumps >= WET_KEYS) {
		for(i = 0; i < BTN_COUNT; i++) {
			cnt[i] = on[i] = pdown[i] = was_hold[i] = 0;
		}
		chord_on = chord_fired = 0;   /* the chord gesture is abandoned     */
		return;
	}

	for(i = 0; i < BTN_COUNT; i++) {
		int d = (int)ebase[i] - (int)tkv[i];

		if(d >= tk_hyst[i]) {
			if(cnt[i] < TK_CONF) cnt[i]++;
		}
		else if(d < tk_hyst[i] / 2) {
			if(cnt[i] > 0) cnt[i]--;
		}
	}

	/* debounced press-state snapshot (all keys) and the real press reference  */
	/* tick of this scan, so every long-press threshold is total time from the */
	/* press (HOLD/REPEAT/chord) instead of from the previous scan.            */
	/* both chord keys were up before this scan -> a press below starts a pair */
	pair_start = !on[BTN_CHORD_A] && !on[BTN_CHORD_B];

	for(i = 0; i < BTN_COUNT; i++) {
		pdown[i] = (cnt[i] >= TK_CONF) ? 1 : 0;
		if(pdown[i] && !on[i]) {
			down_tick10[i] = BTN_Tick10ms();
			/* the FIRST chord key of a pair starts the chord clock, so the 2 s
			   are the total hold from that press in either press order; the
			   partner going down later must not move the start                */
			if(pair_start && (i == BTN_CHORD_A || i == BTN_CHORD_B)) {
				chord_tick = down_tick10[i];
			}
		}
	}

	/* chord state machine: UP+DOWN both down -> hold timer -> EVENT_CHORD     */
	{
		uint8_t ca = pdown[BTN_CHORD_A];
		uint8_t cb = pdown[BTN_CHORD_B];

		if(ca && cb) {
			if(!chord_on) {
				chord_on = 1;
				chord_fired = 0;
				/* 2 s of TOTAL hold from the first key of the pair (both press
				   orders). A partner that arrives after the grace window - or a
				   stale reference - re-anchors the start, so a key that was held
				   for seconds (UP in the open menu) cannot toggle the lock on
				   the next key down, and a wrapped/cleared reference can never
				   make the chord fire at once.                                     */
				if((uint16_t)(BTN_Tick10ms() - chord_tick) > BTN_CHORD_GRACE_10MS_TICKS) {
					chord_tick = BTN_Tick10ms();
				}
			}
			else if(!chord_fired &&
			        (uint16_t)(BTN_Tick10ms() - chord_tick) >= BTN_CHORD_10MS_TICKS) {
				chord_fired = 1;
				evq_push((btn_id_t)BTN_CHORD, BTN_EVT_CHORD);
			}
		}
		else if(chord_on && !ca && !cb) {
			chord_on = 0;            /* both up: gesture complete, next re-arms */
			chord_fired = 0;
			/* both released in the SAME scan: swallow the release edges so the */
			/* chord never leaks a lone CLICK on UP/DOWN (which would change    */
			/* a value). Clear the debounced state here, before the key loop.   */
			on[BTN_CHORD_A] = 0;
			on[BTN_CHORD_B] = 0;
			was_hold[BTN_CHORD_A] = 0;
			was_hold[BTN_CHORD_B] = 0;
		}
	}

	for(i = 0; i < BTN_COUNT; i++) {
		uint8_t pressed = pdown[i];

		/* while the chord is in progress its keys emit no individual events,   */
		/* but IsDown/AnyDown must still see the true hardware state.           */
		if(chord_on && (i == BTN_CHORD_A || i == BTN_CHORD_B)) {
			on[i] = pressed;
			continue;
		}

		if(pressed) {
			if(!on[i])                       /* press edge */
			{
				on[i] = 1;
				/* down_tick10[i] was already stamped by the snapshot loop above */
				was_hold[i]  = 0;
			}
			else {
				/* a lone chord key does not count as an individual hold until  */
				/* the partner window elapsed: a slightly-staggered UP+DOWN     */
				/* press never leaks a lone "up"/"down" HOLD or repeat.         */
				if((i == BTN_CHORD_A || i == BTN_CHORD_B) &&
				   (uint16_t)(BTN_Tick10ms() - down_tick10[i]) < BTN_CHORD_GRACE_10MS_TICKS) {
					continue;
				}
				/* HOLD/REPEAT are measured in REAL 10 ms ticks, not conversions. */
				/* One conversion takes ~20..30 ms, and 500 conversions read      */
				/* 10..15 s on the bench. Main's Timer0 10 ms ISR advances the    */
				/* real tick (BTN_Tick10ms()), so 50 ticks = 0.5 s, 6 = 60 ms.    */
				uint16_t t = BTN_Tick10ms() - down_tick10[i];

				if(!was_hold[i] && t >= BTN_HOLD_10MS_TICKS) {
					was_hold[i] = 1;
					hold_tick10[i] = BTN_Tick10ms();
					evq_push((btn_id_t)i, BTN_EVT_HOLD);
				}
				else if(was_hold[i] && (BTN_Tick10ms() - hold_tick10[i]) >= BTN_REPEAT_10MS_TICKS) {
					hold_tick10[i] = BTN_Tick10ms();
					evq_push((btn_id_t)i, BTN_EVT_HOLD_REPEAT);
				}
			}
		}
		else {
			if(!on[i]) {
				/* key idle: slow baseline tracking against per-key residual     */
				/* drifts. Frozen while the compensated delta sits above the     */
				/* noise floor (EMI / partial touch) or the key is pending press */
				/* (cnt>0) - otherwise the baseline would creep into a press.    */
				if(cnt[i] == 0) {
					int d = (int)ebase[i] - (int)tkv[i];

					if(d < 0) d = -d;
					if(d < tk_noise[i]) {
						int e = (int)tkv[i] - (int)ebase[i];
						int q;

						if(e >= 0)      e += BTN_BASE_SLOW / 2;
						else            e -= BTN_BASE_SLOW / 2;
						/* BTN_BASE_SLOW (128) is a power of 2: shift right with
						   the C division truncation-toward-zero semantics.        */
						if(e >= 0)      q = e >> 7;
						else            q = -((-e) >> 7);
						base[i] += (uint16_t)q;
					}
				}
			}
			else                             /* release edge */
			{
				on[i] = 0;
				down_tick10[i] = 0;
				evq_push((btn_id_t)i, was_hold[i] ? BTN_EVT_RELEASE : BTN_EVT_CLICK);
				was_hold[i] = 0;
			}
		}
	}
}

uint8_t BTN_IsDown(btn_id_t b) {
	if(b >= BTN_COUNT) return 0;
	return on[b];
}

/* Touch-debug helpers below are only read from DEBUG(BUTTONS_EN/RAW_EN, ...)
   print sites in main.c, which compile to nothing without the debug console.
   Gated on their OWN consumer so a build never pays for a helper it cannot
   reach (the linker keeps every compiled function):
     BTN_Delta  -> BUTTONS_EN (the event line prints "d=%d"),
     BTN_Raw/BTN_Ref -> RAW_EN (the raw touch dump),
     BTN_AnyDown/BTN_Baseline -> BTN_BENCH_EN (no caller at all yet).        */
#if UART_DEBUG && BTN_BENCH_EN

uint8_t BTN_AnyDown(void) {
	uint8_t i;

	for(i = 0; i < BTN_COUNT; i++) if(on[i]) return 1;
	return 0;
}

#endif /* UART_DEBUG && BTN_BENCH_EN */

/* Keypad-lock policy for ONE key, evaluated against the CURRENT lock state:   */
/*   FREE   -> every key,                                                      */
/*   EX_OFF -> the power leaf only (the local unlock stays the chord),         */
/*   FULL   -> no ordinary key at all (the chord is handled before this call). */
/* Delayed long-press arms re-run this right before they act, so a remote lock */
/* change cannot be overtaken by an arm that was raised while the keys were    */
/* still free.                                                                 */
uint8_t Btn_ActionAllowed(btn_id_t key) {
	uint8_t lock = settings.keypadLockout;

	if(lock == KEYPAD_LOCKOUT_FREE) return 1;
	if(lock == KEYPAD_LOCKOUT_EX_OFF) return (key == BTN_POWER) ? 1 : 0;
	return 0;   /* KEYPAD_LOCKOUT_FULL */
}

uint8_t BTN_GetEvent(btn_id_t xdata *b) {
	uint8_t ev;

	if(evq_in == evq_out) return BTN_EVT_NONE;
	ev = evq_ev[evq_out];
	if(b) *b = (btn_id_t)evq_key[evq_out];
	evq_out = (uint8_t)((evq_out + 1) & (EVQ_SIZE - 1));   /* EVQ_SIZE is a power of 2 */
	return ev;
}

#if UART_DEBUG && BTN_BENCH_EN

int16_t BTN_Delta(btn_id_t b) {
	return (int16_t)ebase[b] - (int16_t)tkv[b];
}

#endif /* UART_DEBUG && BTN_BENCH_EN */

#if UART_DEBUG && RAW_EN

uint16_t BTN_Raw(btn_id_t b) {
	return tkv[b];
}

uint16_t BTN_Ref(void) {
	return tk_ref;
}

#endif /* UART_DEBUG && RAW_EN */

#if UART_DEBUG && BTN_BENCH_EN

uint16_t BTN_Baseline(btn_id_t b) {
	return base[b];
}

#endif /* UART_DEBUG && BTN_BENCH_EN */
