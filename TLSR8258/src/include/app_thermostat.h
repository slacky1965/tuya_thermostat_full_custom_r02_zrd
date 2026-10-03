#ifndef TLSR8258_SRC_INCLUDE_APP_THERMOSTAT_H_
#define TLSR8258_SRC_INCLUDE_APP_THERMOSTAT_H_

#include "tl_common.h"
#include "zcl_include.h"

/* ZCL Thermostat cluster command handlers (HVAC cluster 0x0201), split out of
   zcl_appCb.c. The weekly schedule lives in the shared g_zcl_scheduleData and
   every edited day is forwarded to the display MCU (CA51F2) over the link. */
status_t App_Thermostat_SetWeeklySchedule(zcl_thermostat_setWeeklyScheduleCmd_t *cmd);
status_t App_Thermostat_GetWeeklySchedule(zclIncomingAddrInfo_t *pAddrInfo,
                                          zcl_thermostat_getWeeklyScheduleCmd_t *cmd);

/* Normalize one ZCL Write Attribute record and forward it to the CA51F2.
   Only the thermostat/UI-config attributes on APP_ENDPOINT1 are handled.
   Returns true when (endPoint, clusterId, attrId) is part of the mapping. */
bool App_Thermostat_WriteAttr(uint8_t endPoint, uint16_t clusterId, uint16_t attrId,
                              const uint8_t *data);

#endif /* TLSR8258_SRC_INCLUDE_APP_THERMOSTAT_H_ */
