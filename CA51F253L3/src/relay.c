/********************************************************************************/
#include "include/stdint.h"
#include "include/config.h"
#include "include/ca51f2sfr.h"
#include "include/ca51f2xsfr.h"
#include "include/gpiodef_f2.h"
#include "include/relay.h"
/********************************************************************************/
static uint8_t xdata relay_on;        /* current relay state (1 = ON)           */
static uint16_t xdata relay_off_secs; /* seconds spent OFF, saturating at the   */
                                      /* window; armed (0) by every 1->0 edge   */
/********************************************************************************/
void Relay_Init(void) {
	RELAY_PIN = RELAY_OFF_LEVEL;         /* drive low BEFORE making it an output */
	P43F = OUTPUT;
	RELAY_PIN = RELAY_OFF_LEVEL;
	relay_on = 0;
	relay_off_secs = RELAY_OFF_WINDOW_SECS; /* boot = long off: no start delay  */
}
/********************************************************************************/
void Relay_Set(uint8_t on) {
	uint8_t next;
	next = on ? 1 : 0;
	RELAY_PIN = on ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL;
	if(relay_on && !next) {
		relay_off_secs = 0;               /* every OFF arms the window          */
	}
	relay_on = next;
}
/********************************************************************************/
uint8_t Relay_IsOn(void) {
	return relay_on;
}
/********************************************************************************/
static uint8_t relay_cold(int16_t t_c100, int16_t setpoint_c100,
                          uint16_t deadband_c100) {
	return t_c100 <= (int16_t)(setpoint_c100 - (int16_t)deadband_c100);
}
/********************************************************************************/
static uint8_t relay_hot(int16_t t_c100, int16_t setpoint_c100,
                         uint16_t deadband_c100) {
	return t_c100 >= (int16_t)(setpoint_c100 + (int16_t)deadband_c100);
}
/********************************************************************************/
void Relay_Update(int16_t in_c100, uint8_t in_ok,
                  int16_t ext_c100, uint8_t ext_ok,
                  temp_src_t src, int16_t setpoint_c100,
                  uint16_t deadband_c100, int16_t max_limit_c100,
                  uint8_t enable, int16_t ws_c100, uint8_t ws_active) {
	uint8_t use_in;
	uint8_t use_ext;
	int16_t t_c100;
	int16_t trip;

	/* safety cut-off: EITHER valid probe reaching the emergency ceiling       */
	/* forces the relay OFF right now - before anything else, so it also runs  */
	/* while the thermostat is off or OTA is pending, and it is issued even    */
	/* when the relay already reads as off (Relay_Set rewrites the pin on      */
	/* every call). temp_src is deliberately ignored: a probe that is measured */
	/* at all may trip. Standing before the window tick, it also freezes that  */
	/* counter, giving a >= 30 s pause after an over-temperature event.        */
	/*                                                                         */
	/* Ceiling = maxHeatSetpointLimit + ABS_MAX_DEADBAND + 1 degC. The highest */
	/* legal OFF point of the law is SP + deadBand <= maxLimit + ABS_MAX_      */
	/* DEADBAND, so this always sits at least RELAY_TRIP_MARGIN_C100 above it  */
	/* and never interferes with normal operation - the earlier fixed 45.00    */
	/* degC version overlapped it (with SP = 45 and deadBand 2.5 the law would */
	/* stop at 47.5, i.e. ABOVE the cut-off). The maximum deadBand is used on  */
	/* purpose so the ceiling does not depend on the user's deadBand setting.  */
	/* max_limit_c100 arrives as a parameter so this module stays clear of     */
	/* settings.h. maxLimit itself is clamped to 45.00 by settings_ranges_     */
	/* valid, so the ceiling tops out at 48.50 degC - far below the NTC        */
	/* saturation at 60 degC.                                                  */
	trip = max_limit_c100 + (int16_t)(ABS_MAX_DEADBAND * 10) +
	       RELAY_TRIP_MARGIN_C100;
	if((in_ok && in_c100 >= trip) || (ext_ok && ext_c100 >= trip)) {
		Relay_Set(RELAY_OFF_LEVEL);
		return;
	}
	/* minimum-OFF window: advances only while the relay is off, saturating   */
	/* at the window; armed to 0 by every 1->0 edge in Relay_Set.             */
	if(!relay_on && relay_off_secs < RELAY_OFF_WINDOW_SECS) {
		relay_off_secs++;
	}
	if(!enable) {
		Relay_Set(RELAY_OFF_LEVEL);      /* fail-safe: no power / OTA active    */
		return;
	}
	/* WS override: while the wireless climate sensor is fresh it drives the law */
	/* INSTEAD of the configured probes. The inputs are rewritten to look like a */
	/* healthy "IN" probe, so the existing single-probe hysteresis (30 s window, */
	/* fail-safe, immediate OFF) applies unchanged - no second copy of the law.  */
	/* The trip above has ALREADY run on the wired probes only: the emergency    */
	/* cut-off deliberately ignores WS.                                          */
	if(ws_active) {
		in_c100 = ws_c100;
		in_ok   = 1;
		src     = SENSOR_SRC_IN;
	}
	/* Which probes take part in the law: IN = internal only, OU = external      */
	/* only (a dead probe means no heat), AL = both - a dead probe of the pair   */
	/* drops out and the survivor keeps the loop running.                        */
	if(src == SENSOR_SRC_IN)       { use_in = in_ok;   use_ext = 0; }
	else if(src == SENSOR_SRC_OUT) { use_in = 0;       use_ext = ext_ok; }
	else                           { use_in = in_ok;   use_ext = ext_ok; }
	/* SENSOR_SRC_ALL                                                          */
	if(!use_in && !use_ext) {        /* no working probe -> OFF at once        */
		Relay_Set(RELAY_OFF_LEVEL);
		return;
	}
	if(use_in && use_ext) {            /* AL with both probes alive            */
		if(!relay_on) {
			/* start once the window has elapsed AND BOTH probes are at/below    */
			/* SP - deadBand                                                     */
			if(relay_off_secs >= RELAY_OFF_WINDOW_SECS &&
			   relay_cold(in_c100, setpoint_c100, deadband_c100) &&
			   relay_cold(ext_c100, setpoint_c100, deadband_c100)) {
				Relay_Set(RELAY_ON_LEVEL);
			}
		}
		else {
			/* stop at the FIRST probe reaching SP + deadBand, immediately       */
			if(relay_hot(in_c100, setpoint_c100, deadband_c100) ||
			   relay_hot(ext_c100, setpoint_c100, deadband_c100)) {
				Relay_Set(RELAY_OFF_LEVEL);
			}
		}
	}
	else {                             /* one working probe: plain hysteresis   */
		t_c100 = use_in ? in_c100 : ext_c100;
		if(!relay_on) {
			/* heat immediately once the window elapsed and the probe is at or   */
			/* below SP - deadBand                                               */
			if(relay_off_secs >= RELAY_OFF_WINDOW_SECS &&
			   relay_cold(t_c100, setpoint_c100, deadband_c100)) {
				Relay_Set(RELAY_ON_LEVEL);
			}
		}
		else {
			/* stop only at or above SP + deadBand, no minimum ON time           */
			if(relay_hot(t_c100, setpoint_c100, deadband_c100)) {
				Relay_Set(RELAY_OFF_LEVEL);
			}
		}
	}
}
