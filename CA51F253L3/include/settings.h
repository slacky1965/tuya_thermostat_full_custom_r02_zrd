#ifndef _SETTINGS_H_
#define _SETTINGS_H_
#include "include/stdint.h"
#include "include_common/thermostat.h"

#define SETTINGS_MAGIC             0x6565
#define SETTINGS_BANK_SIZE         256
#define SETTINGS_BANK0_ADDR        0x0000
#define SETTINGS_BANK1_ADDR        0x0100
#define SETTINGS_FORMAT_VERSION    2
#define SETTINGS_IMAGE_SIZE        193
#define SETTINGS_COMMIT            0xA5

#define CLOCK_MODE_DEF          CLOCK_24H

#define CLOCK_24H               0
#define CLOCK_12H               1

#define KEYPAD_LOCKOUT_FREE     0
#define KEYPAD_LOCKOUT_EX_OFF   1
#define KEYPAD_LOCKOUT_FULL     2
#define KEYPAD_LOCKOUT_DEF      KEYPAD_LOCKOUT_FREE

/* The single on-flash settings image: NO backward compatibility - a layout
   change bumps SETTINGS_FORMAT_VERSION / SETTINGS_IMAGE_SIZE and older images
   are rejected, the device boots defaults. There is exactly one settings
   type; never add a per-version type. format_version..commit are A/B
   metadata: version/size reject foreign or truncated images, generation
   selects the newer bank, crc covers bytes 0..190 and commit (0xA5) is
   written last as the "fully written" marker.                              */
typedef struct {
	uint16_t id;
	uint8_t sensosUsed;
	int8_t localTemperatureCalibration;
	int8_t outTemperatureCalibration;
	uint8_t deadBand;
	uint8_t progMode;
	uint8_t systemMode;
	int16_t minHeatSetpointLimit;
	int16_t maxHeatSetpointLimit;
	int16_t occupiedHeatingSetpoint;
	uint8_t keypadLockout;
	uint8_t modeKeypadLockout;
	uint8_t clockMode;
	uint8_t currentLevel_day;
	uint8_t currentLevel_night;
	scheduleData_t schedule;
	uint8_t format_version;
	uint8_t image_size;
	uint16_t generation;
	uint8_t crc;
	uint8_t commit;
} settings_t;

extern settings_t xdata settings;

/* Row 0..6 -> pointer to that weekday's schedule (mon..sun). The day arrays  */
/* are contiguous inside scheduleData_t, so no switch is needed.              */
schedule_t xdata *Sched_DayRow(uint8_t day);

/* Return 1 only when the selected or newly installed image is usable. On flash
   failure return 0 but leave a versioned RAM fallback synchronized in shadow. */
uint8_t settings_restore(void);
/* Return 1 for a no-op or a fully verified A/B write; 0 on any flash failure. */
uint8_t settings_save(void);
uint8_t settings_factory_reset(void);
/* Semantic gate for the WHOLE live image (id, ranges, limits cross-check,
   schedule). Ingress writers (h_setting, settings_restore) call it before a
   value is allowed to stick; settings_save() itself does NOT validate.       */
uint8_t settings_ranges_valid(const settings_t xdata *value);

#endif
