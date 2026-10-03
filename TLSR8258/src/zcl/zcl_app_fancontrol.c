#include "zcl_include.h"
#include "app_main.h"


#ifdef ZCL_FANCONTROL

static status_t zcl_fancontrol_cmdHandler(zclIncoming_t *pInMsg);


_CODE_ZCL_ status_t zcl_fancontrol_register(u8 endpoint, u16 manuCode, u8 arrtNum, const zclAttrInfo_t attrTbl[], cluster_forAppCb_t cb)
{
    return zcl_registerCluster(endpoint, ZCL_CLUSTER_HAVC_FAN_CONTROL, manuCode, arrtNum, attrTbl, zcl_fancontrol_cmdHandler, cb);
}

_CODE_ZCL_ static status_t zcl_fancontrol_clientCmdHandler(zclIncoming_t *pInMsg)
{
    u8 status = ZCL_STA_SUCCESS;

    switch(pInMsg->hdr.cmd)
    {
//        case ZCL_CMD_THERMOSTAT_SETPOINT_RAISE_LOWER:
//            status = zcl_fancontrol_setpointRaiseLowerPrc(pInMsg);
//            break;
//        case ZCL_CMD_THERMOSTAT_SET_WEEKLY_SCHEDULE:
//            status = zcl_fancontrol_setWeeklySchedulePrc(pInMsg);
//            break;
//        case ZCL_CMD_THERMOSTAT_GET_WEEKLY_SCHEDULE:
//            status = zcl_fancontrol_getWeeklySchedulePrc(pInMsg);
//            break;
//        case ZCL_CMD_THERMOSTAT_CLEAR_WEEKLY_SCHEDULE:
//            status = zcl_fancontrol_clearWeeklySchedulePrc(pInMsg);
//            break;
//        case ZCL_CMD_THERMOSTAT_GET_RELAY_STATUS_LOG:
//            status = zcl_fancontrol_getRelayStatusLogPrc(pInMsg);
//            break;
        default:
            APP_DEBUG(DEBUG_ZCL_CB_EN, "fancontrol client cmd: 0x%x\r\n", pInMsg->hdr.cmd);
            status = ZCL_STA_UNSUP_CLUSTER_COMMAND;
            break;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_fancontrol_serverCmdHandler(zclIncoming_t *pInMsg)
{
    u8 status = ZCL_STA_SUCCESS;

    switch(pInMsg->hdr.cmd)
    {
//        case ZCL_CMD_THERMOSTAT_GET_WEEKLY_SCHEDULE_RSP:
//            status = zcl_fancontrol_getWeeklyScheduleRspPrc(pInMsg);
//            break;
//        case ZCL_CMD_THERMOSTAT_GET_RELAY_STATUS_LOG_RSP:
//            status = zcl_fancontrol_getRelayStatusLogRspPrc(pInMsg);
//            break;
        default:
            APP_DEBUG(DEBUG_ZCL_CB_EN, "fancontrol server cmd: 0x%x\r\n", pInMsg->hdr.cmd);
            status = ZCL_STA_UNSUP_CLUSTER_COMMAND;
            break;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_fancontrol_cmdHandler(zclIncoming_t *pInMsg)
{
    if(pInMsg->hdr.frmCtrl.bf.dir == ZCL_FRAME_CLIENT_SERVER_DIR){
        return zcl_fancontrol_clientCmdHandler(pInMsg);
    }else{
        return zcl_fancontrol_serverCmdHandler(pInMsg);
    }
}


#endif

