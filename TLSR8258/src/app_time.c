#include "app_main.h"

uint8_t str_time[32] = {0};

void app_time_request(void) {

    if(zb_isDeviceJoinedNwk()) {
        epInfo_t dstEpInfo;
        TL_SETSTRUCTCONTENT(dstEpInfo, 0);

        dstEpInfo.profileId = HA_PROFILE_ID;
#if FIND_AND_BIND_SUPPORT
        dstEpInfo.dstAddrMode = APS_DSTADDR_EP_NOTPRESETNT;
#else
        dstEpInfo.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
        dstEpInfo.dstEp = APP_ENDPOINT1;
        dstEpInfo.dstAddr.shortAddr = 0x0;
#endif
        zclReadCmd_t *pReadCmd = (zclReadCmd_t *)ev_buf_allocate(sizeof(zclReadCmd_t) + (sizeof(uint16_t)*2));
        if(pReadCmd){
            pReadCmd->numAttr = 2;
            pReadCmd->attrID[0] = ZCL_ATTRID_TIME;
            pReadCmd->attrID[1] = ZCL_ATTRID_LOCAL_TIME;

            zcl_read(APP_ENDPOINT1, &dstEpInfo, ZCL_CLUSTER_GEN_TIME, MANUFACTURER_CODE_NONE, 0, 0, 0, pReadCmd);

            ev_buf_free((uint8_t *)pReadCmd);
        }
    }
}

void app_time_apply(uint32_t utc, uint32_t local) {

    APP_DEBUG(DEBUG_TIME_EN, "UTC time  : %d\r\n", (utc+UNIX_TIME_CONST));
    APP_DEBUG(DEBUG_TIME_EN, "Local time: %d\r\n", (local+UNIX_TIME_CONST));

    /* ZCL Time is seconds since 2000-01-01; the link carries Unix seconds.   */
    /* A 0 return is the normal "queue it for later" state - never a reason   */
    /* to restart the handshake; the CA51F2 re-asks for the time itself.      */
    App_Link_SendTime(utc + UNIX_TIME_CONST, local + UNIX_TIME_CONST);

    zcl_setAttrVal(APP_ENDPOINT1, ZCL_CLUSTER_GEN_TIME, ZCL_ATTRID_TIME, (uint8_t*)&utc);
    zcl_setAttrVal(APP_ENDPOINT1, ZCL_CLUSTER_GEN_TIME, ZCL_ATTRID_LOCAL_TIME, (uint8_t*)&local);
}

