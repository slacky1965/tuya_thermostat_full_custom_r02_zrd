#include "include/config.h"
#include "include/modes.h"
#include "include/buttons.h"
#include "include/led.h"
#include "include/link_proto.h"
#include "include/ota.h"
#include "include/chars.h"
#include <stddef.h>

/* Settings menu: one descriptor table + one get/set pair. Item order and    */
/* behaviour are unchanged from the pre-split main.c.                        */

/* Settings menu (entered by holding MENU >= 3 s while ON). Item number is shown */
/* in the hours digits, the value in the big temperature digits. MENU click      */
/* advances (saving the previous item if it changed); UP/DOWN edit; POWER saves  */
/* and exits (the thermostat is NOT turned off); 20 s of no keys exits too.      */
uint8_t xdata menu_set;              /* MENU_OFF / MENU_ON                */
uint8_t xdata menu_idx;              /* 0..MENU_ITEMS-1                   */
int16_t xdata menu_val;              /* working value of the current item */
uint16_t xdata mode_idle_tick;       /* tick10ms of the last key event    */

/******************************************************************************/
/* Settings menu: one descriptor table + one get/set pair. Item order and     */
/* behaviour are unchanged from the previous per-item switch version.         */
#define MENU_KIND_TEMP10   0   /* signed value shown as x10 (calibration, i8) */
#define MENU_KIND_DIGIT    1   /* unsigned byte, plain digit                  */
#define MENU_KIND_LIMIT    2   /* signed i16 (setpoint limits)                */
#define MENU_KIND_SENSOR   3   /* 0..2, drawn as IN/OU/AL letters             */
#define MENU_KIND_ACTION   4   /* factory reset (not stored here)             */
#define MENU_KIND_DEADBAND 5   /* u8 x10, shown like TEMP10 but sent as u8    */

typedef struct {
	uint8_t  kind;
	int16_t  min;
	int16_t  max;
	int16_t  step;
	uint8_t  cmd;      /* LNK_CMD_* reported to the peer, 0 = none           */
	uint8_t  off;      /* settings byte offset of the field (ACTION: unused) */
} menu_item_t;

/* IN / OU / AL in the big digits: shared renderer in led.c (the letter table   */
/* was duplicated here until 2026-10-02).                                       */

/* Ranges share the ABS_/MIN_/MAX_/KEYPAD_/SENSOR_/LED_ constants with the      */
/* ingress gate (settings_ranges_valid), so menu limits cannot drift from       */
/* validation: items 5/6 are exactly the cross-bounded pair MIN_LIMIT           */
/* [MIN_.._MIN..MIN_.._MAX] and MAX_LIMIT [MAX_.._MIN..MAX_.._MAX].             */
/* step is the raw arrow granularity only - nothing else reads it, hence free   */
/* literals. ACTION stores nothing, its min/max/step are never used.            */
static const menu_item_t code menu_items[MENU_ITEMS] = {
	{ MENU_KIND_TEMP10,  ABS_MIN_TEMP_CALIB,             ABS_MAX_TEMP_CALIB,             5,  LNK_CMD_CAL_ACTIVE,   offsetof(settings_t, localTemperatureCalibration) },
	{ MENU_KIND_TEMP10,  ABS_MIN_TEMP_CALIB,             ABS_MAX_TEMP_CALIB,             5,  LNK_CMD_CAL_EXTERNAL, offsetof(settings_t, outTemperatureCalibration) },
	{ MENU_KIND_DEADBAND,ABS_MIN_DEADBAND,               ABS_MAX_DEADBAND,               5,  LNK_CMD_HYSTERESIS,   offsetof(settings_t, deadBand) },
	{ MENU_KIND_DIGIT,   KEYPAD_LOCKOUT_EX_OFF,          KEYPAD_LOCKOUT_FULL,            1,  0,                    offsetof(settings_t, modeKeypadLockout) },
	{ MENU_KIND_SENSOR,  SENSOR_SRC_IN,                  SENSOR_SRC_ALL,                 1,  LNK_CMD_SENSOR_SRC,   offsetof(settings_t, sensosUsed) },
	{ MENU_KIND_LIMIT,   MIN_HEATSETPOINT_LIMIT_MIN,     MIN_HEATSETPOINT_LIMIT_MAX,     50, LNK_CMD_LIMIT_MIN,    offsetof(settings_t, minHeatSetpointLimit) },
	{ MENU_KIND_LIMIT,   MAX_HEATSETPOINT_LIMIT_MIN,     MAX_HEATSETPOINT_LIMIT_MAX,     50, LNK_CMD_LIMIT_MAX,    offsetof(settings_t, maxHeatSetpointLimit) },
	{ MENU_KIND_DIGIT,   0,                              LED_BRIGHTNESS_MAX,             1,  LNK_CMD_BRIGHT_DAY,   offsetof(settings_t, currentLevel_day) },
	{ MENU_KIND_DIGIT,   0,                              LED_BRIGHTNESS_MAX,             1,  LNK_CMD_BRIGHT_NIGHT, offsetof(settings_t, currentLevel_night) },
	{ MENU_KIND_ACTION,  0,                              1,                              1,  0,                    0 },
};

/* The only place that knows how each KIND is stored (one small switch instead  */
/* of a switch per menu index; the field address comes from item->off).         */
static int16_t Menu_FieldGet(uint8_t idx) {
	const menu_item_t code *item = &menu_items[idx];
	const uint8_t xdata *p = (const uint8_t xdata *)&settings + item->off;

	switch(item->kind) {
	case MENU_KIND_TEMP10: return (int8_t)*p;
	case MENU_KIND_LIMIT:  return (int16_t)(p[0] | ((uint16_t)p[1] << 8));
	case MENU_KIND_ACTION: return 0;
	default:               return (uint8_t)*p;
	}
}

static void Menu_FieldSet(uint8_t idx, int16_t v) {
	const menu_item_t code *item = &menu_items[idx];
	uint8_t xdata *p = (uint8_t xdata *)&settings + item->off;

	switch(item->kind) {
	case MENU_KIND_TEMP10: *p = (uint8_t)(int8_t)v; break;
	case MENU_KIND_LIMIT:  p[0] = (uint8_t)v;
	                       p[1] = (uint8_t)(v >> 8); break;
	case MENU_KIND_ACTION: return;
	default:               *p = (uint8_t)v; break;
	}
	/* item 4 (sensor) also keeps the live source in sync */
	if(idx == 4) temp_src = (temp_src_t)v;
}

static void Menu_Load(void) {
	menu_val = (menu_idx == (MENU_ITEMS - 1)) ? 0 : Menu_FieldGet(menu_idx);
}

void Menu_Step(uint8_t up) {
	const menu_item_t code *item = &menu_items[menu_idx];

	/* Arrow step clamps only to the item's own range (5..15 / 20..45 for the     */
	/* limit items). Deliberately NO semantic gate here: the limits must adjust   */
	/* independently of the current setpoint / schedule; the shared validator     */
	/* checks them against the absolute bounds instead.                           */
	menu_val += up ? item->step : -item->step;
	if(menu_val < item->min) menu_val = item->min;
	if(menu_val > item->max) menu_val = item->max;
}

/* Write the current item's value back into settings; 1 if it changed.          */
static uint8_t Menu_Store(void) {
	if(menu_idx == (MENU_ITEMS - 1)) return 0;
	if(Menu_FieldGet(menu_idx) == menu_val) return 0;
	Menu_FieldSet(menu_idx, menu_val);
	return 1;
}

/* Tell the ZT3L the value of the just-changed menu item (reliable send).       */
static void Menu_Send(void) {
	const menu_item_t code *item = &menu_items[menu_idx];
	uint8_t type = LNK_T_U8;

	if(item->cmd == 0) return;
	if(item->kind == MENU_KIND_LIMIT) {
		Link_SendI16(item->cmd, Menu_FieldGet(menu_idx));
	}
	else if(item->kind == MENU_KIND_TEMP10) {
		Link_SendU8(item->cmd, LNK_T_I8,
		            (uint8_t)(int8_t)Menu_FieldGet(menu_idx));
	}
	else {
		if(item->kind == MENU_KIND_DEADBAND) type = LNK_T_I8;
		else if(item->kind == MENU_KIND_SENSOR) type = LNK_T_ENUM;
		Link_SendU8(item->cmd, type, (uint8_t)Menu_FieldGet(menu_idx));
	}
}

/* Commit the current item. With a live peer, persist before reporting it;        */
/* offline edits accumulate in RAM and Menu_Exit persists them once. Item 10      */
/* (reset) is the exception: factory reset persists immediately and notifies      */
/* the peer so it re-handshakes and pulls the full state dump (STATE_ALL).        */
static void Menu_Commit(void) {
	if(menu_idx == (MENU_ITEMS - 1)) {
		if(menu_val == 1) {
			if(settings_factory_reset()) {
				settings_persist_pending = 0;
				temp_src = (temp_src_t)settings.sensosUsed;   /* defaults */
				menu_val = 0;
				if(!Ota_IsPending()) {
					Link_BootNotify();
				}
			}
		}
		return;
	}
	if(Menu_Store() && Link_PeerAlive() && !Ota_IsPending() && Settings_Persist()) {
		Menu_Send();
	}
}

void Menu_Draw(void) {
	uint8_t n = (uint8_t)(menu_idx + 1);

	Led_SymInit();
	Led_Digit(6, (uint8_t)(n / 10));    /* item number in the minutes digits */
	Led_Digit(7, (uint8_t)(n % 10));
    switch (menu_items[menu_idx].kind) {
        case MENU_KIND_TEMP10:
        case MENU_KIND_DEADBAND:
            Led_MenuValue((int16_t) (((uint16_t) menu_val << 3) + ((uint16_t) menu_val << 1)));
            break;
        case MENU_KIND_LIMIT:
            Led_MenuValue(menu_val);
            break;
        case MENU_KIND_SENSOR: { /* IN / OU / AL letters       */
                uint8_t v = (menu_val > 2) ? 2 : (uint8_t) menu_val;
                Led_SensorLetters(v);
            }
            break;
        default: /* DIGIT / ACTION */
            Led_Digit(2, (uint8_t) menu_val);
            break;
    }
	Led_Flush();
}

void Menu_Enter(void) {
	menu_set = MENU_ON;
	menu_idx = 0;
	mode_idle_tick = BTN_Tick10ms();
	Menu_Load();
	Menu_Draw();
}

void Menu_Exit(void) {
	Menu_Commit();                  /* last item if changed (RAM + network)  */
	Settings_Persist();             /* one flash write for the whole session */
	menu_set = MENU_OFF;
	Led_SymInit();
	Thermo_Apply();
}

void Menu_Advance(void) {
	Menu_Commit();                               /* save the previous item if changed */
	if(++menu_idx >= MENU_ITEMS) menu_idx = 0;   /* wrap (no signed modulo)           */
	mode_idle_tick = BTN_Tick10ms();
	Menu_Load();
	Menu_Draw();
}
