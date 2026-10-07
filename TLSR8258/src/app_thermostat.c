#include "app_main.h"

/* Weekly schedule helpers: map the ZCL day bitmap to the shared 7-day image    */
/* (Mon..Sun) and forward edited days to the display MCU.                       */
static const uint8_t app_sched_dayMask[7] = {
    DAY_MON, DAY_TUE, DAY_WED, DAY_THU, DAY_FRI, DAY_SAT, DAY_SUN
};

/* Clamps typed after the ZCL attribute they normalize: i16 for INT16 attributes,
   i8 for INT8 (calibrations, dead band), u8_max for the unsigned enums/bitmap.
   The local value must keep the attribute's own type so no implicit widening /
   narrowing hides a range mistake. */
static int16_t app_clamp_i16(int16_t v, int16_t lo, int16_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int8_t app_clamp_i8(int8_t v, int8_t lo, int8_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static uint8_t app_clamp_u8_max(uint8_t v, uint8_t hi) {
    return (v > hi) ? hi : v;
}

static void app_link_resync_if_needed(uint8_t accepted) {
    (void)accepted;
}

static schedule_t *app_sched_row(uint8_t day) {
    schedule_t *row = (schedule_t *)&g_zcl_scheduleData;
    return row + (uint16_t)day * SCHED_PERIODS;
}

static uint16_t app_sched_transTime(const zcl_thermostat_setWeeklyScheduleCmd_t *cmd, uint8_t i) {
    if (cmd->modeForSequence == BOTH_SETPOINT_FIELD_PRESENT) {
        return cmd->sequenceMode.pBothMode[i].transTime;
    }
    return cmd->sequenceMode.pHeatMode[i].transTime;
}

static int16_t app_sched_heatSetpoint(const zcl_thermostat_setWeeklyScheduleCmd_t *cmd, uint8_t i) {
    if (cmd->modeForSequence == BOTH_SETPOINT_FIELD_PRESENT) {
        return (int16_t)cmd->sequenceMode.pBothMode[i].heatSetpoint;
    }
    return (int16_t)cmd->sequenceMode.pHeatMode[i].heatSetpoint;
}

status_t App_Thermostat_SetWeeklySchedule(zcl_thermostat_setWeeklyScheduleCmd_t *cmd) {
    uint8_t n;
    uint8_t i;
    uint8_t day;

    if (cmd->modeForSequence != HEAT_SERPOINT_FIELD_PRESENT &&
        cmd->modeForSequence != BOTH_SETPOINT_FIELD_PRESENT) {
        return ZCL_STA_INVALID_FIELD;
    }

    n = cmd->numOfTransForSequence;
    if (n > SCHED_PERIODS) n = SCHED_PERIODS;
    /* A zero transition count carries no data: applying it would wipe every   */
    /* selected day through the trailing loop below and still answer SUCCESS.  */
    if (n == 0) return ZCL_STA_INVALID_FIELD;

    for (day = 0; day < 7; day++) {
        schedule_t *row;

        if (!(cmd->dayOfWeekForSequence & app_sched_dayMask[day])) continue;

        row = app_sched_row(day);
        for (i = 0; i < n; i++) {
            uint16_t t = app_sched_transTime(cmd, i);
            int16_t  h = app_sched_heatSetpoint(cmd, i);

            /* Sanity at the ingress: the storage is only checked against      */
            /* FIXED bounds - time <= 23:59 (minutes) and the ABS setpoint     */
            /* range. An over-range value is clamped, never dropped (a         */
            /* transition must not vanish silently). The configurable          */
            /* min/maxHeatSetpointLimit is policy and is applied by the CA51F2 */
            /* when the setpoint is used, never baked in here.                 */
            if (t != LNK_SCHED_TIME_EMPTY && t > LNK_SCHED_DAY_MAX_MIN) {
                t = LNK_SCHED_DAY_MAX_MIN;
            }
            if (t == LNK_SCHED_TIME_EMPTY) {
                h = 0;                       /* empty slot: no temperature     */
            } else if (h < ABS_MIN_HEATSETPOINT_LIMIT_DEF) {
                h = ABS_MIN_HEATSETPOINT_LIMIT_DEF;
            } else if (h > ABS_MAX_HEATSETPOINT_LIMIT_DEF) {
                h = ABS_MAX_HEATSETPOINT_LIMIT_DEF;
            }
            row[i].minute      = t;
            row[i].temperature = h;
        }
        /* Unused trailing entries: empty time and NO temperature - the old     */
        /* code kept a clamped stale value here, which reached the hub as junk. */
        for (i = n; i < SCHED_PERIODS; i++) {
            row[i].minute      = LNK_SCHED_TIME_EMPTY;
            row[i].temperature = 0;
        }
        app_link_resync_if_needed(App_Link_SendSchedule(day));
    }

    return ZCL_STA_SUCCESS;
}

/* Get Weekly Schedule: one response frame per requested day (the SDK builder   */
/* serialises a single sequence only); frames are spaced by a short timer so    */
/* the TX queue is not flooded.                                                 */
static uint8_t  app_sched_getMask;
static uint8_t  app_sched_getSeq;
static uint16_t app_sched_getAddr;
static uint8_t  app_sched_getEp;
static ev_timer_event_t *app_sched_getEvt = NULL;

static void app_sched_getSend(void);

static int32_t app_sched_getTimerCb(void *arg) {
    (void)arg;
    app_sched_getEvt = NULL;
    app_sched_getSend();
    return -1;
}

static void app_sched_getSend(void) {
    zcl_thermostat_getWeeklyScheduleRspCmd_t rsp;
    epInfo_t dstEpInfo;
    uint8_t day;
    uint8_t i;
    uint8_t cnt = 0;
    const schedule_t *row;
    /* Compact copy of the REAL transitions of the day being answered. A       */
    /* static buffer outlives the call even if the ZCL builder defers          */
    /* serialisation; it is rewritten only when the next day (20 ms later)     */
    /* is sent.                                                                */
    static heatMode_t app_sched_getBuf[SCHED_PERIODS];

    for (day = 0; day < 7; day++) {
        if (app_sched_getMask & app_sched_dayMask[day]) break;
    }
    if (day >= 7) return;

    app_sched_getMask &= (uint8_t)~app_sched_dayMask[day];

    /* Report only the transitions that actually exist: empty slots (0xFFFF)    */
    /* are an internal marker and must never reach the hub - it renders them as */
    /* 1092:15. numOfTrans is the compacted count.                              */
    row = app_sched_row(day);
    for (i = 0; i < SCHED_PERIODS; i++) {
        if (row[i].minute == LNK_SCHED_TIME_EMPTY) continue;
        app_sched_getBuf[cnt].transTime    = row[i].minute;
        app_sched_getBuf[cnt].heatSetpoint = row[i].temperature;
        cnt++;
    }
    if (!cnt) {                        /* fully empty day: still one entry     */
        app_sched_getBuf[0].transTime    = row[0].minute;
        app_sched_getBuf[0].heatSetpoint = row[0].temperature;
        cnt = 1;
    }

    TL_SETSTRUCTCONTENT(dstEpInfo, 0);
    dstEpInfo.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
    dstEpInfo.dstAddr.shortAddr = app_sched_getAddr;
    dstEpInfo.dstEp = app_sched_getEp;
    dstEpInfo.profileId = HA_PROFILE_ID;

    TL_SETSTRUCTCONTENT(rsp, 0);
    rsp.numOfTransForSequence = cnt;
    rsp.dayOfWeekForSequence  = app_sched_dayMask[day];
    rsp.modeForSequence       = HEAT_SERPOINT_FIELD_PRESENT;
    rsp.sequenceMode.pHeatMode = app_sched_getBuf;

    zcl_thermostat_getWeeklyScheduleRspCmdSend(APP_ENDPOINT1, &dstEpInfo, 0,
                                               app_sched_getSeq, &rsp);

    if (app_sched_getMask) {
        app_sched_getEvt = TL_ZB_TIMER_SCHEDULE(app_sched_getTimerCb, NULL, 20);
    }
}

status_t App_Thermostat_GetWeeklySchedule(zclIncomingAddrInfo_t *pAddrInfo,
                                          zcl_thermostat_getWeeklyScheduleCmd_t *cmd) {
    uint8_t mask = cmd->daysToReturn & 0x7F;

    if (!mask) mask = 0x7F;                 /* no day specified -> answer all     */

    app_sched_getMask = mask;
    app_sched_getSeq  = pAddrInfo->seqNum;
    app_sched_getAddr = pAddrInfo->srcAddr;
    app_sched_getEp   = pAddrInfo->srcEp;

    if (app_sched_getEvt) {
        TL_ZB_TIMER_CANCEL(&app_sched_getEvt);
    }
    app_sched_getSend();

    return ZCL_STA_CMD_HAS_RESP;
}

/* ZCL Write Attribute -> link settings. The ZCL stack has already stored the raw
 * value in the attribute, so the value is normalized (clamped) here, written back
 * into the attribute and forwarded to the CA51F2 exactly as clamped. Only the
 * thermostat/UI-config clusters on APP_ENDPOINT1 exist, so any other endpoint is
 * ignored (the ZCL stack rejects the write, we must not forward it). Returns true
 * when the (endPoint, clusterId, attrId) triple belongs to the mapping. */

bool App_Thermostat_WriteAttr(uint8_t endPoint, uint16_t clusterId, uint16_t attrId,
                              const uint8_t *data) {
    if (endPoint != APP_ENDPOINT1) {
        return false;
    }

    if (clusterId == ZCL_CLUSTER_HAVC_THERMOSTAT) {
        switch (attrId) {
            case ZCL_ATTRID_HVAC_THERMOSTAT_OCCUPIED_HEATING_SETPOINT: {
                int16_t v = (int16_t) BUILD_U16(data[0], data[1]);
                v = app_clamp_i16(v, g_zcl_thermostatAttrs.minHeatSetpointLimit, g_zcl_thermostatAttrs.maxHeatSetpointLimit);
                g_zcl_thermostatAttrs.occupiedHeatingSetpoint = v;
                app_link_resync_if_needed(App_Link_SendSettingI16(LNK_CMD_SETPOINT_HEAT, v));
                break;
            }
            case ZCL_ATTRID_HVAC_THERMOSTAT_MIN_HEAT_SETPOINT_LIMIT: {
                int16_t v = (int16_t) BUILD_U16(data[0], data[1]);
                /* The two limit ranges are disjoint (MIN <= 1500 < 2000 <= MAX), so   */
                /* min <= max holds without a cross-clamp against the other attribute. */
                v = app_clamp_i16(v, MIN_HEATSETPOINT_LIMIT_MIN, MIN_HEATSETPOINT_LIMIT_MAX);
                g_zcl_thermostatAttrs.minHeatSetpointLimit = v;
                app_link_resync_if_needed(App_Link_SendSettingI16(LNK_CMD_LIMIT_MIN, v));
                break;
            }
            case ZCL_ATTRID_HVAC_THERMOSTAT_MAX_HEAT_SETPOINT_LIMIT: {
                int16_t v = (int16_t) BUILD_U16(data[0], data[1]);
                v = app_clamp_i16(v, MAX_HEATSETPOINT_LIMIT_MIN, MAX_HEATSETPOINT_LIMIT_MAX);
                g_zcl_thermostatAttrs.maxHeatSetpointLimit = v;
                app_link_resync_if_needed(App_Link_SendSettingI16(LNK_CMD_LIMIT_MAX, v));
                break;
            }
            case ZCL_ATTRID_HVAC_THERMOSTAT_LOCAL_TEMP_CALIBRATION: {
                int8_t v = (int8_t) data[0]; /* ZCL INT8: -90..+90 = -9.0..+9.0 C x10 */
                v = app_clamp_i8(v, ABS_MIN_TEMP_CALIB, ABS_MAX_TEMP_CALIB);
                g_zcl_thermostatAttrs.localTemperatureCalibration = v;
                app_link_resync_if_needed(App_Link_SendSettingU8(LNK_CMD_CAL_ACTIVE, LNK_T_I8, (uint8_t) v));
                break;
            }
            case ZCL_ATTRID_HVAC_THERMOSTAT_CUSTOM_EXT_TEMP_CALIBRATION: {
                int8_t v = (int8_t) data[0]; /* ZCL INT8, same bounds as the local one */
                v = app_clamp_i8(v, ABS_MIN_TEMP_CALIB, ABS_MAX_TEMP_CALIB);
                g_zcl_thermostatAttrs.extTemperatureCalibration = v;
                app_link_resync_if_needed(App_Link_SendSettingU8(LNK_CMD_CAL_EXTERNAL, LNK_T_I8, (uint8_t) v));
                break;
            }
            case ZCL_ATTRID_HVAC_THERMOSTAT_MIN_SETPOINT_DEAD_BAND: {
                /* Wire: one signed byte (i8, 10..50 = 1.0..5.0 C x10), matching the     */
                /* ZCL INT8 attribute; int8_t arithmetic keeps a negative value clamping */
                /* to the low bound instead of the high one.                             */
                int8_t v = (int8_t) data[0];
                v = app_clamp_i8(v, ABS_MIN_DEADBAND, ABS_MAX_DEADBAND);
                g_zcl_thermostatAttrs.dead_band = v;
                app_link_resync_if_needed(App_Link_SendSettingU8(LNK_CMD_HYSTERESIS, LNK_T_I8, (uint8_t) v));
                break;
            }
            case ZCL_ATTRID_HVAC_THERMOSTAT_PROGRAMMING_OPERATION_MODE: {
                /* Bitmap8 forwarded byte-for-byte: the CA51F2 accepts the modes    */
                /* it supports (0/1/4/5 = manual/schedule bits + Economy bit 2)     */
                /* and rejects anything else, so the hub learns about an            */
                /* unsupported value via the STATE_ALL resync.                      */
                uint8_t v = data[0];
                APP_DEBUG(DEBUG_ZCL_CB_EN, "OperMode: 0x%02x\r\n", v);
                g_zcl_thermostatAttrs.manual_progMode = v;
                app_link_resync_if_needed(App_Link_SendSettingU8(LNK_CMD_PROG_MODE, LNK_T_BITMAP8, v));
                break;
            }
            case ZCL_ATTRID_HVAC_THERMOSTAT_SYS_MODE:
                g_zcl_thermostatAttrs.systemMode = data[0];
                app_link_resync_if_needed(App_Link_SendSettingU8(LNK_CMD_SYSTEM_MODE, LNK_T_ENUM, data[0]));
                break;
            case ZCL_ATTRID_HVAC_THERMOSTAT_CUSTOM_SENSOR_USED: {
                uint8_t v = app_clamp_u8_max(data[0], 2);
                g_zcl_thermostatAttrs.sensor_used = v;
                app_link_resync_if_needed(App_Link_SendSettingU8(LNK_CMD_SENSOR_SRC, LNK_T_ENUM, v));
                break;
            }
            default:
                return false;
        }
        return true;
    }

    if (clusterId == ZCL_CLUSTER_HAVC_USER_INTERFACE_CONFIG &&
        attrId == ZCL_ATTRID_HVAC_KEYPAD_LOCKOUT) {
        uint8_t v = app_clamp_u8_max(data[0], 2);   /* tri-state 0..2 */
        g_zcl_thermostatAttrs.keypadLockout = v;
        app_link_resync_if_needed(App_Link_SendSettingU8(LNK_CMD_KEY_LOCK, LNK_T_ENUM, v));
        return true;
    }

    return false;
}
