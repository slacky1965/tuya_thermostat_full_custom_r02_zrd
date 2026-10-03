#ifndef _RELAY_H_
#define _RELAY_H_
#include "include/stdint.h"
#include "thermostat.h"
/***********************************************************************************/
/* Heating relay control (pin/levels/timings in config.h section 7).               */
/* Symmetric hystereses: ON when t <= SP - deadBand, OFF when t >= SP + deadBand;  */
/* OFF is always immediate. The only time restriction is the minimum time spent    */
/* OFF (RELAY_OFF_WINDOW_SECS): while the relay has been off for less than that,   */
/* the ON condition is ignored; once the window has elapsed, ON happens on a       */
/* single evaluation. Every 1->0 transition of Relay_Set arms the window; boot     */
/* starts with it already elapsed (first start is not delayed).                    */
/* The law runs over the probes selected by temp_src:                              */
/*   IN  - internal probe only;   OU - external probe only (dead -> no heat);      */
/*   AL  - BOTH probes: start only when both are <= SP - db, stop when EITHER      */
/*         reaches SP + db; with one dead probe it degrades to the single-probe    */
/*         law over the survivor, with none the relay is forced OFF.               */
/*                                                                                 */
/* Safety cut-off: the first thing Relay_Update does is force OFF when EITHER      */
/* valid probe reaches the emergency ceiling                                       */
/*     maxHeatSetpointLimit + ABS_MAX_DEADBAND + RELAY_TRIP_MARGIN_C100            */
/* i.e. 2.5 degC (the MAXIMUM deadBand) plus 1 degC above the configured limit     */
/* - 48.50 degC at the default limit. The highest legal OFF point of the law is    */
/* SP + deadBand <= maxHeatSetpointLimit + ABS_MAX_DEADBAND, so the cut-off        */
/* always clears it by at least the margin and never fires during normal           */
/* operation (a fixed 45.00 degC cut-off used to overlap it at a high setpoint).   */
/* The maximum deadBand is used on purpose so the ceiling does not depend on the   */
/* user's deadBand setting; max_limit_c100 is passed in so this module does not    */
/* need settings.h. The cut-off ignores temp_src, enable and the current relay     */
/* state, so it also applies while the thermostat is off or OTA is pending, and    */
/* it issues the OFF command even when the relay already reads as off. Being       */
/* checked before the OFF-window tick, a trip also pauses the next start by at     */
/* least RELAY_OFF_WINDOW_SECS after cooling down.                                 */
/***********************************************************************************/

/* Configure RELAY_PIN as an output and drive it OFF. Call once at startup.     */
void Relay_Init(void);

/* Force the relay on/off now (bypasses the hysteresis state). Every 1->0        */
/* transition arms the minimum-OFF window, whatever the caller was.              */
void Relay_Set(uint8_t on);

/* 1 if the relay is currently on.                                              */
uint8_t Relay_IsOn(void);

/* One control step (call once per second). enable=0 (power off / OTA) forces     */
/* OFF immediately. in/ext carry the accepted value (0.01 degC) and validity of   */
/* both probes; src selects which of them the law uses. deadband_c100 is the      */
/* hysteresis width (e.g. deadBand x10 -> x100). max_limit_c100 is the configured */
/* upper heat setpoint limit (settings.maxHeatSetpointLimit, c100), which anchors */
/* the emergency cut-off ceiling described above. ws_c100/ws_active: the wireless */
/* climate sensor (0.01 degC + freshness flag Link_WsFresh()); while ws_active    */
/* it drives the law INSTEAD of the configured probes - but the emergency         */
/* cut-off above deliberately ignores it (wired probes only).                     */
void Relay_Update(int16_t in_c100, uint8_t in_ok,
                  int16_t ext_c100, uint8_t ext_ok,
                  temp_src_t src, int16_t setpoint_c100,
                  uint16_t deadband_c100, int16_t max_limit_c100,
                  uint8_t enable, int16_t ws_c100, uint8_t ws_active);

#endif
