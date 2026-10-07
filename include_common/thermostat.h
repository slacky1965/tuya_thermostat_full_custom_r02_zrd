#ifndef _THERMOSTAT_H_
#define _THERMOSTAT_H_

/* NOTE: like link_proto.h this header has no includes; the fixed-width types   */
/* (uint16_t/int16_t) must be in scope already (both sides include their own    */
/* stdint/types before it).                                                     */

/* Programming operation mode (ZCL thermostat bitmap; bit0 = manual/schedule,   */
/* bit2 = Economy/EnergyStar):                                                  */
#define PROG_MODE_MANUAL                    0                        /* simple/setpoint mode (manual up/down)    */
#define PROG_MODE_SCHEDULE                  1                        /* weekly schedule programming enabled      */
#define PROG_MODE_ECO                       4                        /* Economy/EnergyStar (ZCL bit 2)           */
#define PROG_MODE_MASK          (PROG_MODE_SCHEDULE | PROG_MODE_ECO) /* bits we support                          */

#define TEMPERATURE_STEP                    50                                  /* 0.5° x 100   */
#define ABS_MIN_HEATSETPOINT_LIMIT          500                                 /* 5°C x 100    */
#define ABS_MAX_HEATSETPOINT_LIMIT          4500                                /* 45°C x 100   */
#define MIN_HEATSETPOINT_LIMIT_MIN          ABS_MIN_HEATSETPOINT_LIMIT          /* 5°C x 100    */
#define MIN_HEATSETPOINT_LIMIT_MAX         (ABS_MIN_HEATSETPOINT_LIMIT + 1000)  /* 15°C x 100   */
#define MAX_HEATSETPOINT_LIMIT_MIN         (MIN_HEATSETPOINT_LIMIT_MAX + 500)   /* 20°C x 100   */
#define MAX_HEATSETPOINT_LIMIT_MAX          ABS_MAX_HEATSETPOINT_LIMIT          /* 45°C x 100   */
#define ABS_MIN_HEATSETPOINT_LIMIT_DEF      ABS_MIN_HEATSETPOINT_LIMIT          /* 5°C x 100    */
#define ABS_MAX_HEATSETPOINT_LIMIT_DEF      ABS_MAX_HEATSETPOINT_LIMIT          /* 45°C x 100   */
#define MIN_HEATSETPOINT_LIMIT_DEF          ABS_MIN_HEATSETPOINT_LIMIT_DEF      /* 5°C x 100    */
#define MAX_HEATSETPOINT_LIMIT_DEF          ABS_MAX_HEATSETPOINT_LIMIT_DEF      /* 45°C x 100   */
#define LOCAL_TEMP_CALIB_DEF                0                                   /* ?°C x 100    */
#define OUT_TEMP_CALIB_DEF                  0                                   /* ?°C x 100    */
#define OCCUPIED_HEATSETPOINT_DEF           2000                                /* 20°C x 100   */
#define MIN_SETPOINT_DEADBAND_DEF           5                                   /* 0.5°C x 10   */

/* Absolute clamp bounds shared by both MCUs: network writes are normalized      */
/* (clamped) to these, so a bad host value can never reach the settings image.   */
#define ABS_MIN_TEMP_CALIB                  (-90)   /* x10, -9.0 °C     */
#define ABS_MAX_TEMP_CALIB                  90      /* x10, +9.0 °C     */
/* deadBand range is 0.5..2.5 degC (ZCL-style max 2.5; 0 is deliberately         */
/* excluded - the relay would chatter without a dead band).                      */
#define ABS_MIN_DEADBAND                    5       /* x10, 0.5 °C      */
#define ABS_MAX_DEADBAND                    25      /* x10, 2.5 °C      */
#define CURRENT_LEVEL_DAY_DEF               7       /* brightness 0-8   */
#define CURRENT_LEVEL_NIGHT_DEF             2       /* brightness 0-8   */
#define SENSOR_USED_DEF                     SENSOR_SRC_IN
#define PROG_MODE_DEF                       PROG_MODE_MANUAL

#define SYS_MODE_OFF            0x00
#define SYS_MODE_AUTO           0x01
#define SYS_MODE_COOL           0x03
#define SYS_MODE_HEAT           0x04
#define SYS_MODE_EMERGENCY      0x05
#define SYS_MODE_PRECOOLING     0x06
#define SYS_MODE_FAN            0x07
#define SYS_MODE_DRY            0x08
#define SYS_MODE_SLEEP          0x09

#define RUN_MODE_OFF            0x00
#define RUN_MODE_COOL           0x03
#define RUN_MODE_HEAT           0x04

#define RUN_STATE_HEAT_BIT      0
#define RUN_STATE_COOL_BIT      1
#define RUN_STATE_FAN_BIT       2


/* Working temperature source (which sensor drives the measurement).     */
/* Values MUST match LNK_SENSOR_* in link_proto.h (IN=0, OUT=1, ALL=2)   */
/* and the CA51F2 menu order IN -> OU -> AL.                             */
typedef enum
{
    SENSOR_SRC_IN     = 0,    /* internal on-board NTC                    */
    SENSOR_SRC_OUT    = 1,    /* external NTC only (no heating if absent) */
    SENSOR_SRC_ALL    = 2     /* All: external, fall back to internal     */
} temp_src_t;


/* Weekly schedule shared by both MCUs: 7 days x SCHED_PERIODS transitions.     */
/* Entry = minutes since midnight (0..1439, 0xFFFF = unused) + setpoint x100.   */
#define SCHED_PERIODS   6

typedef struct {
    uint16_t    minute;
    int16_t     temperature;
} schedule_t;

typedef struct {
    schedule_t  schedule_mon[SCHED_PERIODS];
    schedule_t  schedule_tue[SCHED_PERIODS];
    schedule_t  schedule_wed[SCHED_PERIODS];
    schedule_t  schedule_thu[SCHED_PERIODS];
    schedule_t  schedule_fri[SCHED_PERIODS];
    schedule_t  schedule_sat[SCHED_PERIODS];
    schedule_t  schedule_sun[SCHED_PERIODS];
} scheduleData_t;


#endif /* _THERMOSTAT_H_ */
