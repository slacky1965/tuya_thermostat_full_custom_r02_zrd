/*******************************************************************************/
#include "config.h"
#include "stdint.h"
#include "stddef.h"
#include "settings.h"
#include "link_proto.h"
#include "flash.h"
#include "led.h"
#include "crc.h"
#include "debug.h"
/*******************************************************************************/
settings_t xdata settings;

static settings_t xdata settings_shadow;
static uint8_t xdata settings_shadow_valid;
static uint8_t xdata settings_buffer[SETTINGS_IMAGE_SIZE];
static uint8_t xdata settings_active_bank = 0xFF;
static uint16_t xdata settings_active_generation;

typedef char settings_image_size_check[
	(sizeof(settings_t) == SETTINGS_IMAGE_SIZE) ? 1 : -1];
typedef char settings_version_offset_check[
	(offsetof(settings_t, format_version) == 187) ? 1 : -1];
typedef char settings_size_offset_check[
	(offsetof(settings_t, image_size) == 188) ? 1 : -1];
typedef char settings_generation_offset_check[
	(offsetof(settings_t, generation) == 189) ? 1 : -1];
typedef char settings_crc_offset_check[
	(offsetof(settings_t, crc) == 191) ? 1 : -1];
typedef char settings_commit_offset_check[
	(offsetof(settings_t, commit) == 192) ? 1 : -1];
/*******************************************************************************/

schedule_t xdata *Sched_DayRow(uint8_t day) {
	return &((schedule_t xdata *)&settings.schedule)
	       [(uint16_t)day * (sizeof(settings.schedule.schedule_mon) / sizeof(schedule_t))];
}

#if UART_DEBUG && FLASH_EN
static void settings_print(void) {
	DEBUG(FLASH_EN, { debug_puts("cm "); debug_puts(settings.clockMode ? "AM/PM" : "24"); debug_puts("\r\n"); });
	DEBUG(FLASH_EN, { debug_kv("ld ", settings.currentLevel_day); });
	DEBUG(FLASH_EN, { debug_kv("ln ", settings.currentLevel_night); });
	DEBUG(FLASH_EN, { debug_kv("db ", settings.deadBand); });
	DEBUG(FLASH_EN, { debug_kv("kl ", settings.keypadLockout); });
	DEBUG(FLASH_EN, { debug_kv("lc ", settings.localTemperatureCalibration); });
	DEBUG(FLASH_EN, { debug_kv("mx ", settings.maxHeatSetpointLimit); });
	DEBUG(FLASH_EN, { debug_kv("mn ", settings.minHeatSetpointLimit); });
	DEBUG(FLASH_EN, { debug_kv("sp ", settings.occupiedHeatingSetpoint); });
	DEBUG(FLASH_EN, { debug_kv("oc ", settings.outTemperatureCalibration); });
	DEBUG(FLASH_EN, { debug_kv("pm ", settings.progMode); });
	DEBUG(FLASH_EN, { debug_kv("ss ", settings.sensosUsed); });
	DEBUG(FLASH_EN, { debug_kv("sm ", settings.systemMode); });
	DEBUG(FLASH_EN, { debug_kv("mk ", settings.modeKeypadLockout); });
}
#endif

#define SCHED_DEF_PERIODS   6
#define SCHED_DEF_TEMP      1500
static code const uint16_t sched_def_min[SCHED_DEF_PERIODS] = {
	6*60, 8*60, 12*60, 16*60, 20*60, 22*60
};

static void settings_copy(uint8_t xdata *dst, const uint8_t xdata *src, uint8_t len) {
	uint8_t i;

	for(i = 0; i < len; i++) dst[i] = src[i];
}

static void settings_sync_shadow(void) {
	settings_copy((uint8_t xdata *)&settings_shadow,
	              (const uint8_t xdata *)&settings, SETTINGS_IMAGE_SIZE);
	settings_shadow_valid = 1;
}

static uint8_t settings_image_same(void) {
	const uint8_t xdata *live = (const uint8_t xdata *)&settings;
	const uint8_t xdata *saved = (const uint8_t xdata *)&settings_shadow;
	uint8_t i;

	for(i = 0; i < SETTINGS_IMAGE_SIZE; i++) {
		if(live[i] != saved[i]) return 0;
	}
	return 1;
}

#define SETTINGS_IN_RANGE(v, lo, hi) \
	((uint16_t)((v) - (uint16_t)(lo)) <= (uint16_t)((hi) - (uint16_t)(lo)))

/* Semantic gate of the live image. Exported: h_setting uses it as the ingress */
/* check for network writes (validate where data ENTERS settings,              */
/* settings_save() itself only persists).                                      */
uint8_t settings_ranges_valid(const settings_t xdata *value) {
	const schedule_t xdata *entry =
		(const schedule_t xdata *)&value->schedule;
	int16_t min_limit = value->minHeatSetpointLimit;
	int16_t max_limit = value->maxHeatSetpointLimit;
	uint8_t slot;

	if(value->id != SETTINGS_MAGIC) return 0;
	if(value->sensosUsed > SENSOR_SRC_ALL) return 0;
	if(!SETTINGS_IN_RANGE(value->localTemperatureCalibration, ABS_MIN_TEMP_CALIB, ABS_MAX_TEMP_CALIB)) return 0;
	if(!SETTINGS_IN_RANGE(value->outTemperatureCalibration, ABS_MIN_TEMP_CALIB, ABS_MAX_TEMP_CALIB)) return 0;
	if(!SETTINGS_IN_RANGE(value->deadBand, ABS_MIN_DEADBAND, ABS_MAX_DEADBAND)) return 0;
	/* progMode is a BITMAP: only ZCL bit0 (schedule) and bit2 (eco) exist here, */
	/* so 0/1/4/5 pass and everything else (bit1 auto/recovery) is rejected.     */
	if(value->progMode & (uint8_t)~PROG_MODE_MASK) return 0;
	if(value->systemMode > SYS_MODE_SLEEP || value->systemMode == 2) return 0;
	/* The two limits are cross-bounded: the MIN limit may only rise to          */
	/* MIN_HEATSETPOINT_LIMIT_MAX, the MAX limit may only fall to                */
	/* MAX_HEATSETPOINT_LIMIT_MIN, so min <= max can never be broken by a write. */
	if(!SETTINGS_IN_RANGE(min_limit, MIN_HEATSETPOINT_LIMIT_MIN, MIN_HEATSETPOINT_LIMIT_MAX)) return 0;
	if(!SETTINGS_IN_RANGE(max_limit, MAX_HEATSETPOINT_LIMIT_MIN, MAX_HEATSETPOINT_LIMIT_MAX)) return 0;
	if(min_limit > max_limit) return 0;
	/* The setpoint and the schedule slots are checked against the ABSOLUTE      */
	/* bounds only: the menu's 5..15 / 20..45 limit items must adjust            */
	/* independently of the current setpoint.                                    */
	if(!SETTINGS_IN_RANGE(value->occupiedHeatingSetpoint, ABS_MIN_HEATSETPOINT_LIMIT_DEF, ABS_MAX_HEATSETPOINT_LIMIT_DEF)) return 0;
	if(value->keypadLockout > KEYPAD_LOCKOUT_FULL) return 0;
	if(!SETTINGS_IN_RANGE(value->modeKeypadLockout, KEYPAD_LOCKOUT_EX_OFF, KEYPAD_LOCKOUT_FULL)) return 0;
	if(value->clockMode > CLOCK_12H) return 0;
	if(value->currentLevel_day > LED_BRIGHTNESS_MAX ||
	   value->currentLevel_night > LED_BRIGHTNESS_MAX) return 0;

	for(slot = 0; slot < (7 * SCHED_DEF_PERIODS); slot++, entry++) {
		if(entry->minute == LNK_SCHED_TIME_EMPTY) continue;
		if(!SETTINGS_IN_RANGE(entry->minute, 0, LNK_SCHED_DAY_MAX_MIN)) return 0;
		if(!SETTINGS_IN_RANGE(entry->temperature, ABS_MIN_HEATSETPOINT_LIMIT_DEF, ABS_MAX_HEATSETPOINT_LIMIT_DEF)) return 0;
	}
	return 1;
}

static uint8_t settings_crc(const uint8_t xdata *image, uint8_t len) {
	return crc8(0, image, len);
}

static uint8_t settings_image_valid(const settings_t xdata *image) {
	return (uint8_t)(image->format_version == SETTINGS_FORMAT_VERSION &&
	                 image->image_size == SETTINGS_IMAGE_SIZE &&
	                 image->generation != 0 &&
	                 image->commit == SETTINGS_COMMIT &&
	                 image->crc == settings_crc((const uint8_t xdata *)image, 191) &&
	                 settings_ranges_valid(image));
}

static uint8_t settings_read_bank(uint16_t address, uint16_t xdata *generation) {
	const settings_t xdata *image;

	Data_Area_Mass_Read(address, settings_buffer, SETTINGS_IMAGE_SIZE);
	image = (const settings_t xdata *)settings_buffer;
	if(!settings_image_valid(image)) return 0;
	*generation = image->generation;
	return 1;
}

static void settings_sched_defaults(void) {
	uint8_t day;
	uint8_t slot;

	for(day = 0; day < 7; day++) {
		schedule_t xdata *row = Sched_DayRow(day);
		for(slot = 0; slot < SCHED_DEF_PERIODS; slot++) {
			row[slot].minute = sched_def_min[slot];
			row[slot].temperature = SCHED_DEF_TEMP;
		}
	}
}

static void settings_defaults(void) {
	DEBUG(SETTINGS_EN, { debug_puts("settings_defaults()\r\n"); });
	settings.id                          = SETTINGS_MAGIC;
	settings.sensosUsed                  = SENSOR_USED_DEF;
	settings.localTemperatureCalibration = LOCAL_TEMP_CALIB_DEF;
	settings.outTemperatureCalibration   = OUT_TEMP_CALIB_DEF;
	settings.deadBand                    = MIN_SETPOINT_DEADBAND_DEF;
	settings.progMode                    = PROG_MODE_DEF;
	settings.systemMode                  = SYS_MODE_OFF;
	settings.minHeatSetpointLimit        = MIN_HEATSETPOINT_LIMIT_DEF;
	settings.maxHeatSetpointLimit        = MAX_HEATSETPOINT_LIMIT_DEF;
	settings.occupiedHeatingSetpoint     = OCCUPIED_HEATSETPOINT_DEF;
	settings.keypadLockout               = KEYPAD_LOCKOUT_DEF;
	settings.modeKeypadLockout           = KEYPAD_LOCKOUT_EX_OFF;
	settings.clockMode                   = CLOCK_MODE_DEF;
	settings.currentLevel_day            = CURRENT_LEVEL_DAY_DEF;
	settings.currentLevel_night          = CURRENT_LEVEL_NIGHT_DEF;
	settings.format_version              = SETTINGS_FORMAT_VERSION;
	settings.image_size                  = SETTINGS_IMAGE_SIZE;
	settings.generation                  = 0;
	settings.crc                         = 0;
	settings.commit                      = 0;
	settings_sched_defaults();
}

static void settings_prepare_live(uint16_t generation, uint8_t commit) {
	settings.format_version = SETTINGS_FORMAT_VERSION;
	settings.image_size = SETTINGS_IMAGE_SIZE;
	settings.generation = generation;
	settings.commit = commit;
	settings.crc = settings_crc((const uint8_t xdata *)&settings, 191);
}

static void settings_install_fallback(void) {
	settings_prepare_live(1, SETTINGS_COMMIT);
	settings_active_bank = 0xFF;
	settings_active_generation = 0;
	settings_sync_shadow();
}

static void settings_prepare_fallback(void) {
	settings_defaults();
	settings_install_fallback();
}

static void settings_revert_live(void) {
	if(settings_shadow_valid) {
		settings_copy((uint8_t xdata *)&settings,
		              (const uint8_t xdata *)&settings_shadow, SETTINGS_IMAGE_SIZE);
	}
}

static uint8_t settings_write_bank(uint8_t bank, uint16_t generation) {
	uint16_t address = bank ? SETTINGS_BANK1_ADDR : SETTINGS_BANK0_ADDR;
	uint8_t sector = bank ? 2 : 0;
	uint8_t i;

	settings_prepare_live(generation, 0);

	if(!Data_Area_Sector_Erase(sector) || !Data_Area_Sector_Erase(sector + 1)) {
		settings_revert_live();
		return 0;
	}
	Data_Area_Mass_Write(address, (uint8_t xdata *)&settings, SETTINGS_IMAGE_SIZE - 1);
	Data_Area_Write_Byte(address + SETTINGS_IMAGE_SIZE - 1, SETTINGS_COMMIT);
	settings.commit = SETTINGS_COMMIT;

	Data_Area_Mass_Read(address, settings_buffer, SETTINGS_IMAGE_SIZE);
	for(i = 0; i < SETTINGS_IMAGE_SIZE; i++) {
		if(settings_buffer[i] != ((const uint8_t xdata *)&settings)[i]) {
			settings_revert_live();
			return 0;
		}
	}

	settings_sync_shadow();
	settings_active_bank = bank;
	settings_active_generation = generation;
	return 1;
}

uint8_t settings_factory_reset(void) {
	settings_defaults();
	return settings_save();
}

uint8_t settings_save(void) {
	uint8_t bank;
	uint16_t generation;

	/* No semantic gate here by design: every ingress validates before a value
	   sticks (network writes via the shared ranges gate, restore via the image
	   validator, menu/schedule/arrows clamp at edit time). A failing gate here
	   used to revert the WHOLE live image and clobber unrelated unsaved edits.
	   Flash errors are still caught by write_bank below.                    */
	if(settings_active_bank < 2) {
		if(settings_image_same()) {
			#if UART_DEBUG && FLASH_EN
			DEBUG(SETTINGS_EN, { debug_kv("settings_save() - same: ", 1); });
			#endif
			return 1;
		}
		#if UART_DEBUG && FLASH_EN
		DEBUG(SETTINGS_EN, { debug_kv("settings_save() - same: ", 0); });
		#endif
		bank = settings_active_bank ? 0 : 1;
		generation = (uint16_t)(settings_active_generation + 1);
		if(generation == 0) generation = 1;
	}
	else {
		bank = 0;
		generation = 1;
	}

	if(!settings_write_bank(bank, generation)) {
		DEBUG(SETTINGS_EN, { debug_puts("settings_save() failed\r\n"); });
		return 0;
	}
#if UART_DEBUG && FLASH_EN
	settings_print();
#endif
	return 1;
}

uint8_t xdata settings_persist_pending; /* retry canonical RAM fallback  */
/* Attempts per boot for the pending retry: the main loop calls             */
/* Settings_Persist() once per second while the flag is armed (it is armed  */
/* only when the boot restore failed, main.c), so a data flash that is      */
/* genuinely dead would erase two sectors every second FOREVER with the     */
/* result cast to void. After the limit the retry gives up until the next   */
/* boot (values stay RAM-only; boot re-arms the flag) and leaves a trace    */
/* under SETTINGS_EN.                                                       */
#define SETTINGS_PERSIST_MAX_TRIES 60
static uint8_t xdata settings_persist_tries;

uint8_t Settings_Persist(void) {
	if(!settings_save()) {
		if(settings_persist_pending &&
		   ++settings_persist_tries >= SETTINGS_PERSIST_MAX_TRIES) {
			settings_persist_pending = 0; /* give up until the next boot  */
			settings_persist_tries   = 0;
			DEBUG(SETTINGS_EN, { debug_puts("persist: gave up\r\n"); });
		}
		return 0;
	}
	settings_persist_pending = 0;
	settings_persist_tries   = 0;
	return 1;
}

static uint8_t settings_bank1_newer(uint16_t bank0, uint16_t bank1) {
	uint16_t delta = (uint16_t)(bank1 - bank0);

	return (uint8_t)(bank1 != 0 && delta != 0 && delta < 0x8000);
}

uint8_t settings_restore(void) {
	uint16_t generation0 = 0;
	uint16_t generation1 = 0;
	uint8_t valid0;
	uint8_t valid1;
	uint8_t selected;

	DEBUG(SETTINGS_EN, { debug_puts("settings_restore()\r\n"); });
	settings_active_bank = 0xFF;
	settings_active_generation = 0;
	settings_shadow_valid = 0;
	valid0 = settings_read_bank(SETTINGS_BANK0_ADDR, &generation0);
	valid1 = settings_read_bank(SETTINGS_BANK1_ADDR, &generation1);

	if(valid0 && valid1) {
		selected = settings_bank1_newer(generation0, generation1) ? 1 : 0;
	}
	else if(valid0) {
		selected = 0;
	}
	else if(valid1) {
		selected = 1;
	}
	else {
		settings_prepare_fallback();
		if(!settings_save()) return 0;
		#if UART_DEBUG && FLASH_EN
		settings_print();
		#endif
		return 1;
	}

	if(!settings_read_bank(selected ? SETTINGS_BANK1_ADDR : SETTINGS_BANK0_ADDR,
	                       &generation0)) {
		settings_prepare_fallback();
		return 0;
	}
	settings_copy((uint8_t xdata *)&settings, settings_buffer, SETTINGS_IMAGE_SIZE);
	settings_active_bank = selected;
	settings_active_generation = generation0;
	settings_sync_shadow();
	#if UART_DEBUG && FLASH_EN
	settings_print();
	#endif
	return 1;
}
