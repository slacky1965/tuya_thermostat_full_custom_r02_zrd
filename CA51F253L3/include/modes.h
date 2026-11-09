#ifndef MODES_H_
#define MODES_H_

/* Shared surface of the mode modules extracted from main.c: clock-set       */
/* (src/clock_mode.c), schedule-set (src/sched_mode.c), settings menu        */
/* (src/menu_mode.c). main() drives the modes through the entry points below */
/* and shares the state declared here. A section is added here in the same   */
/* task that moves the code, never earlier: a non-static prototype in this   */
/* header conflicts with a still-static definition in main.c.                */
#include "temp_pipeline.h"
#include "settings.h"

/* ---- clock-set mode (src/clock_mode.c) ---- */
#define CLK_SET_OFF     0
#define CLK_SET_HOUR    1
#define CLK_SET_MIN     2
#define CLK_SET_WDAY    3                       /* weekday step, after minutes */

void Clock_Set_Enter(void);
void Clock_Set_Draw(void);
void Clock_Set_Commit(void);
void Clock_Set_Exit(uint8_t keep);
void Clock_Set_Tick(void);

/* ---- schedule-set mode (src/sched_mode.c) ---- */
#define SCHED_SET_OFF   0
#define SCHED_SET_ON    1
#define SCHED_FLD_HOUR  0
#define SCHED_FLD_MIN   1
#define SCHED_FLD_TEMP  2

void Sched_Set_Enter(void);
void Sched_Set_Load(void);
void Sched_Set_Store(void);
void Sched_Set_Next(void);
void Sched_SendPending(void);
void Sched_Set_Exit(void);
/* Network wrote a whole day while its editor may be open: close the editor   */
/* without Store (the slot registers/cursor are stale) so the network value   */
/* is not overwritten. Called by h_sched after the day is applied and saved.  */
void Sched_NetPreempt(uint8_t day);
uint8_t Sched_EntryValid(const schedule_t xdata *e);
/* The schedule transition in force RIGHT NOW (weekly-curve rule, see           */
/* sched_mode.c): today's last transition at or before the clock; before the    */
/* day's first transition (or on an empty day) the last transition of the most  */
/* recent previous day that has one, walking back up to 7 days; a completely    */
/* empty week returns 0 - the caller falls back to the manual setpoint.         */
/* Fills the day index (0..6, Mon=0) and the slot index (0..LNK_SCHED_N-1).     */
/* Out-params are xdata pointers (like RTC_ReadTime): the targets must be       */
/* XDATA_TMP statics - plain stack automatics got garbage stores on this        */
/* toolchain.                                                                   */
uint8_t Sched_FindActive(uint8_t xdata *day_out, uint8_t xdata *idx_out);
/* Sort one weekly day in place: ascending by transition time, the unused     */
/* slots (0xFFFF, larger than any valid minute) pushed to the end. Stable, so */
/* equal times keep their storage order. Returns 1 when the order actually    */
/* changed - the caller must then persist the day and report it to ZT3L, so   */
/* the peer copy and the z2m view match the stored image.                     */
uint8_t Sched_DaySort(schedule_t xdata *day);
int16_t Sched_ActiveSetpoint(void);   /* setpoint in force right now                               */
void Sched_SlotApply(void);           /* SYM_SP1..6: period that owns "now" (screen.c)             */
extern int16_t xdata target_setpoint; /* control/display setpoint mirror                           */
extern uint8_t xdata sched_slot_now;  /* 1-based active slot, 0=none (set by Sched_ActiveSetpoint) */

/* ---- settings menu (src/menu_mode.c) ---- */
#define MENU_OFF        0
#define MENU_ON         1
#define MENU_ITEMS      10
#define MODE_IDLE_10MS_TICKS  2000              /* 20 s without keys -> exit+save */

void Menu_Enter(void);
void Menu_Exit(void);
void Menu_Advance(void);
void Menu_Step(uint8_t up);
void Menu_Draw(void);

/* ---- screen: brightness / composition / indication (src/screen.c) ---- */
#define BRIGHTNESS_HOLD_SECS  20         /* max-brightness window after a key, s */

uint8_t Bright_LevelBase(void);          /* day/night level from settings        */
void Bright_Apply(uint8_t level);        /* apply level 0..8 to display+keys     */
void Bright_MaxHold(void);               /* re-arm the window and apply MAX      */
extern uint8_t xdata bright_hold;        /* seconds left of the window           */

#define STANDBY_KEYS_SECS  7            /* OFF: touch keys stay lit this long, s */

void Backlights_Off(uint8_t level);     /* shared OFF backlight pattern         */
void Standby_KeysApply(void);           /* OFF phase 1: keys+leaf at MAX        */
void Standby_GreenApply(void);          /* steady OFF: leaf at day/night level  */
const temp_sample_t xdata *Temp_DisplaySample(void);
void Temp_Draw(const temp_sample_t xdata *sample);
void Temp_ErPoll(void);
void Clock_Draw(void);                  /* HH:MM + immediate flush              */
extern uint8_t xdata off_hold;          /* OFF touch-key stay phase, seconds    */
void Lock_Sym(void);                    /* SYM_LOCK follows keypadLockout       */
void Lock_On_Screen(void);              /* ON: brightness spike + padlock       */
void Lock_Flash_Start(void);            /* start the padlock blink, take screen */
void Lock_Flash(void);                  /* drive one 10 ms blink phase          */
void NetBlink_Screen(void);             /* OFF provisioning screen              */
void Net_Flash_Start(void);             /* start the 1.5 min NET blink          */
void Net_Flash_Stop(void);              /* stop it, keep the steady symbol      */
void Net_StatusApply(void);             /* Link_NetStatus() -> steady/off       */
void Net_Flash(void);                   /* drive one 10 ms NET half-phase       */

/* ---- helpers shared between main(), the modes and the scheduler ---- */
void Thermo_Apply(void);
uint8_t Settings_Persist(void);
void Sched_Set_Draw(void);          /* src/sched_mode.c                                    */
void ProgMode_Icons(uint8_t pm);    /* SYM_HAND/SYM_CLOCK/SYM_ECO from progMode (screen.c) */
uint8_t Screen_ModeFree(void);      /* 1 = no clock/schedule/menu owns the screen          */

/* ---- state shared between main() and the modes ---- */
extern bit clock_colon;                          /* ':' blink state             */
extern uint16_t xdata adj_remain;                /* adjust-display seconds      */
extern uint8_t xdata sched_set;                  /* SCHED_SET_* (sched_mode.c)  */
extern uint8_t xdata clk_set;                    /* CLK_SET_*  (clock_mode.c)   */
extern uint8_t xdata clk_set_h;
extern uint8_t xdata clk_set_m;
extern uint8_t xdata clk_set_w;
extern uint8_t xdata clk_blink;                  /* blink phase (clock_mode.c)   */
extern uint16_t xdata clk_blink_tick;            /* last blink flip tick10ms     */
extern uint8_t xdata sched_hub;
extern uint8_t xdata sched_day;
extern uint8_t xdata sched_slot;
extern uint8_t xdata sched_field;
extern uint8_t xdata sched_h;
extern uint8_t xdata sched_m;
extern int16_t  xdata sched_temp;
extern uint8_t xdata sched_h_set;                /* 1 = hours field is set     */
extern uint8_t xdata sched_m_set;                /* 1 = minutes field is set   */
extern uint8_t xdata sched_dirty;
extern uint8_t xdata sched_send_mask;
extern uint8_t xdata sched_changed;
extern uint8_t xdata menu_set;                   /* MENU_OFF / MENU_ON                   */
extern uint8_t xdata menu_idx;                   /* 0..MENU_ITEMS-1                      */
extern int16_t  xdata menu_val;                  /* working value of the item            */
extern uint16_t xdata mode_idle_tick;            /* tick10ms of last key event           */
extern temp_src_t temp_src;                      /* live sensor source                   */
extern uint8_t xdata settings_persist_pending;   /* retry canonical fallback             */
extern bit power_on;                             /* thermostat power (screen.c)          */
extern bit ext_ok;                               /* external probe present               */
extern uint8_t xdata lock_flash;                 /* padlock blink phases left            */
extern uint8_t xdata trace_tail;                 /* LOCK_TRACE_EN: s of probe life after */
                                                 /* the blink (see screen.c)             */
extern uint8_t xdata net_flash;                  /* 1 = NET blink active                 */
extern uint8_t xdata net_flash_secs;             /* seconds before auto-stop             */
extern uint8_t xdata net_hold;                   /* OFF steady-NET hold, seconds         */

#endif
