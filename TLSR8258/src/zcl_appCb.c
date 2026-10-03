/********************************************************************************************************
 * @file    zcl_appCb.c
 *
 * @brief   This is the source file for zcl_appCb
 *
 * @author  Zigbee Group
 * @date    2021
 *
 * @par     Copyright (c) 2021, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *			All rights reserved.
 *
 *          Licensed under the Apache License, Version 2.0 (the "License");
 *          you may not use this file except in compliance with the License.
 *          You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 *          Unless required by applicable law or agreed to in writing, software
 *          distributed under the License is distributed on an "AS IS" BASIS,
 *          WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *          See the License for the specific language governing permissions and
 *          limitations under the License.
 *
 *******************************************************************************************************/

/**********************************************************************
 * INCLUDES
 */
#include "tl_common.h"
#include "zcl_include.h"

#include "app_main.h"


/**********************************************************************
 * LOCAL CONSTANTS
 */

/* Clamp for the Level Control brightness (0..maxLevelA/B). */
static uint8_t app_clamp_u8_max(uint8_t v, uint8_t hi) {
    return (v > hi) ? hi : v;
}

/* Validity bounds for the wireless climate sensor (WS) reports, in ZCL units  */
/* (0.01 degC / 0.01 %RH) - same x100 convention as everywhere else in the     */
/* project; kept as named constants for quick editing.                         */
#define WS_TEMP_MIN_C100        (-4000) /* -40.00 degC                        */
#define WS_TEMP_MAX_C100        12500   /* +125.00 degC                       */
#define WS_HUMID_MAX_C100       10000   /* 100.00 %RH (min is 0)              */


/**********************************************************************
 * TYPEDEFS
 */


/**********************************************************************
 * LOCAL FUNCTIONS
 */
#ifdef ZCL_READ
static void app_zclReadRspCmd(uint16_t clusterId, zclReadRspCmd_t *pReadRspCmd);
#endif
#ifdef ZCL_WRITE
static void app_zclWriteRspCmd(uint16_t clusterId, zclWriteRspCmd_t *pWriteRspCmd);
static void app_zclWriteReqCmd(uint8_t endPoint, uint16_t clusterId, zclWriteCmd_t *pWriteReqCmd);
#endif
#ifdef ZCL_REPORT
static void app_zclCfgReportCmd(uint8_t endPoint, uint16_t clusterId, zclCfgReportCmd_t *pCfgReportCmd);
static void app_zclCfgReportRspCmd(uint16_t clusterId, zclCfgReportRspCmd_t *pCfgReportRspCmd);
static void app_zclReportCmd(uint8_t endPoint, uint16_t clusterId, zclReportCmd_t *pReportCmd);
#endif
static void app_zclDfltRspCmd(uint16_t clusterId, zclDefaultRspCmd_t *pDftRspCmd);


/**********************************************************************
 * GLOBAL VARIABLES
 */


/**********************************************************************
 * LOCAL VARIABLES
 */
#ifdef ZCL_IDENTIFY
static ev_timer_event_t *identifyTimerEvt = NULL;
#endif

/**********************************************************************
 * FUNCTIONS
 */

/*********************************************************************
 * @fn      app_zclProcessIncomingMsg
 *
 * @brief   Process ZCL Foundation incoming message.
 *
 * @param   pInMsg - pointer to the received message
 *
 * @return  None
 */
void app_zclProcessIncomingMsg(zclIncoming_t *pInHdlrMsg)
{
//    printf("app_zclProcessIncomingMsg\r\n");

	uint16_t cluster = pInHdlrMsg->msg->indInfo.cluster_id;
	uint8_t endPoint = pInHdlrMsg->msg->indInfo.dst_ep;
	switch(pInHdlrMsg->hdr.cmd)
	{
#ifdef ZCL_READ
		case ZCL_CMD_READ_RSP:
			app_zclReadRspCmd(cluster, pInHdlrMsg->attrCmd);
			break;
#endif
#ifdef ZCL_WRITE
		case ZCL_CMD_WRITE_RSP:
			app_zclWriteRspCmd(cluster, pInHdlrMsg->attrCmd);
			break;
		case ZCL_CMD_WRITE:
		    app_zclWriteReqCmd(endPoint, cluster, pInHdlrMsg->attrCmd);
			break;
#endif
#ifdef ZCL_REPORT
		case ZCL_CMD_CONFIG_REPORT:
			app_zclCfgReportCmd(endPoint, cluster, pInHdlrMsg->attrCmd);
			break;
		case ZCL_CMD_CONFIG_REPORT_RSP:
			app_zclCfgReportRspCmd(cluster, pInHdlrMsg->attrCmd);
			break;
		case ZCL_CMD_REPORT:
			app_zclReportCmd(endPoint, cluster, pInHdlrMsg->attrCmd);
			break;
#endif
		case ZCL_CMD_DEFAULT_RSP:
			app_zclDfltRspCmd(cluster, pInHdlrMsg->attrCmd);
			break;
		default:
			break;
	}
}

#ifdef ZCL_READ
/*********************************************************************
 * @fn      app_zclReadRspCmd
 *
 * @brief   Handler for ZCL Read Response command.
 *
 * @param   pInHdlrMsg - incoming message to process
 *
 * @return  None
 */
static void app_zclReadRspCmd(uint16_t clusterId, zclReadRspCmd_t *pReadRspCmd)
{
    //printf("app_zclReadRspCmd\n");
    uint8_t numAttr = pReadRspCmd->numAttr;
    zclReadRspStatus_t *attrList = pReadRspCmd->attrList;

    uint32_t utc;
    uint32_t time_local;
    bool have_utc = false;
    bool have_local = false;

    for (uint8_t i = 0; i < numAttr; i++) {
        if (attrList[i].attrID == ZCL_ATTRID_TIME && attrList[i].status == ZCL_STA_SUCCESS) {
            utc = attrList[i].data[0] & 0xff;
            utc |= (attrList[i].data[1] << 8)  & 0x0000ffff;
            utc |= (attrList[i].data[2] << 16) & 0x00ffffff;
            utc |= (attrList[i].data[3] << 24) & 0xffffffff;
            have_utc = true;
        } else if (attrList[i].attrID == ZCL_ATTRID_LOCAL_TIME && attrList[i].status == ZCL_STA_SUCCESS) {
            time_local = attrList[i].data[0] & 0xff;
            time_local |= (attrList[i].data[1] << 8)  & 0x0000ffff;
            time_local |= (attrList[i].data[2] << 16) & 0x00ffffff;
            time_local |= (attrList[i].data[3] << 24) & 0xffffffff;
            have_local = true;
        }
    }

    if (have_utc && have_local) {
        app_time_apply(utc, time_local);
    }
}
#endif	/* ZCL_READ */

#ifdef ZCL_WRITE
/*********************************************************************
 * @fn      app_zclWriteRspCmd
 *
 * @brief   Handler for ZCL Write Response command.
 *
 * @param   pInHdlrMsg - incoming message to process
 *
 * @return  None
 */
static void app_zclWriteRspCmd(uint16_t clusterId, zclWriteRspCmd_t *pWriteRspCmd)
{
//    printf("app_zclWriteRspCmd\r\n");

}

/*********************************************************************
 * @fn      app_zclWriteReqCmd
 *
 * @brief   Handler for ZCL Write Request command.
 *
 * @param   pInHdlrMsg - incoming message to process
 *
 * @return  None
 */
static void app_zclWriteReqCmd(uint8_t endPoint, uint16_t clusterId, zclWriteCmd_t *pWriteReqCmd) {
    uint8_t numAttr = pWriteReqCmd->numAttr;
    zclWriteRec_t *attr = pWriteReqCmd->attrList;
    uint8_t i;

    for (i = 0; i < numAttr; i++) {
        App_Thermostat_WriteAttr(endPoint, clusterId, attr[i].attrID, attr[i].attrData);
    }

#ifdef ZCL_POLL_CTRL
	if(clusterId == ZCL_CLUSTER_GEN_POLL_CONTROL){
		for(int32_t i = 0; i < numAttr; i++){
			if(attr[i].attrID == ZCL_ATTRID_CHK_IN_INTERVAL){
				app_zclCheckInStart();
				return;
			}
		}
	}
#endif
}
#endif	/* ZCL_WRITE */


/*********************************************************************
 * @fn      app_zclDfltRspCmd
 *
 * @brief   Handler for ZCL Default Response command.
 *
 * @param   pInHdlrMsg - incoming message to process
 *
 * @return  None
 */
static void app_zclDfltRspCmd(uint16_t clusterId, zclDefaultRspCmd_t *pDftRspCmd)
{
//    printf("app_zclDfltRspCmd\r\n");

}

#ifdef ZCL_REPORT
/*********************************************************************
 * @fn      app_zclCfgReportCmd
 *
 * @brief   Handler for ZCL Configure Report command.
 *
 * @param   pInHdlrMsg - incoming message to process
 * @param   dst_ep     - number of endPoint
 *
 * @return  None
 */
static void app_zclCfgReportCmd(uint8_t endPoint, uint16_t clusterId, zclCfgReportCmd_t *pCfgReportCmd)
{
    printf("app_zclCfgReportCmd\r\n");
//    reportCfgInfo_t *pEntry;
//    for(uint8_t i = 0; i < pCfgReportCmd->numAttr; i++) {
//        for (uint8_t ii = 0; ii < ZCL_REPORTING_TABLE_NUM; ii++) {
//            pEntry = &reportingTab.reportCfgInfo[ii];
//            if (pEntry->used) {
//                printf("attrId: 0x%x, maxInterval: %d, minInterval: %d\r\n", pEntry->attrID, pEntry->maxInterval, pEntry->minInterval);
//
//            }
////            if (app_reporting[ii].pEntry->used) {
////                if (app_reporting[ii].pEntry->endPoint == endPoint && app_reporting[ii].pEntry->attrID == pCfgReportCmd->attrList[i].attrID) {
////                    if (app_reporting[ii].timerReportMinEvt) {
////                        TL_ZB_TIMER_CANCEL(&app_reporting[ii].timerReportMinEvt);
////                    }
////                    if (app_reporting[ii].timerReportMaxEvt) {
////                        TL_ZB_TIMER_CANCEL(&app_reporting[ii].timerReportMaxEvt);
////                    }
////                    return;
////                }
////            }
//        }
//    }
}

/*********************************************************************
 * @fn      app_zclCfgReportRspCmd
 *
 * @brief   Handler for ZCL Configure Report Response command.
 *
 * @param   pInHdlrMsg - incoming message to process
 *
 * @return  None
 */
static void app_zclCfgReportRspCmd(uint16_t clusterId, zclCfgReportRspCmd_t *pCfgReportRspCmd)
{
    printf("app_zclCfgReportRspCmd\r\n");

}

/*********************************************************************
 * @fn      app_zclReportCmd
 *
 * @brief   Handler for ZCL Report command.
 *
 * @param   pInHdlrMsg - incoming message to process
 *
 * @return  None
 */
static void app_zclReportCmd(uint8_t endPoint, uint16_t clusterId, zclReportCmd_t *pReportCmd)
{
    if (endPoint != APP_ENDPOINT1) return;

    for (uint8_t i = 0; i < pReportCmd->numAttr; i++) {
        if (clusterId == ZCL_CLUSTER_MS_TEMPERATURE_MEASUREMENT) {
            if (pReportCmd->attrList[i].attrID == ZCL_TEMPERATURE_MEASUREMENT_ATTRID_MEASUREDVALUE) {
                int16_t t = (int16_t)BUILD_U16(pReportCmd->attrList[i].attrData[0], pReportCmd->attrList[i].attrData[1]);
                if (t < WS_TEMP_MIN_C100 || t > WS_TEMP_MAX_C100) {
                    APP_DEBUG(DEBUG_WS_EN, "ws t bad: %d\r\n", (int)t);
                    continue;
                }
                APP_DEBUG(DEBUG_WS_EN, "ws t: %d\r\n", (int)t);
                if (App_Link_Ca51f2Alive()) {
                    App_Link_SendWsTemp(t);
                }
            }
        } else if (clusterId == ZCL_CLUSTER_MS_RELATIVE_HUMIDITY) {
            if (pReportCmd->attrList[i].attrID == ZCL_RELATIVE_HUMIDITY_MEASUREMENT_ATTRID_MEASUREDVALUE) {
                uint16_t h = (uint16_t)BUILD_U16(pReportCmd->attrList[i].attrData[0], pReportCmd->attrList[i].attrData[1]);
                if (h > WS_HUMID_MAX_C100) {
                    APP_DEBUG(DEBUG_WS_EN, "ws h bad: %d\r\n", (int)h);
                    continue;
                }
                APP_DEBUG(DEBUG_WS_EN, "ws h: %d\r\n", (int)h);
                if (App_Link_Ca51f2Alive()) {
                    /* Rule: round to a whole percent and map 100 -> 99         */
                    /* HERE, so the CA51F2 renders as-is (its menu renderer has */
                    /* no hundreds digit and must do no math at all).           */
                    uint16_t pct = (uint16_t)((h + 50) / 100);
                    if (pct > 99) pct = 99;
                    App_Link_SendWsHumid((uint16_t)(pct * 100));
                }
            }
        }
    }

}
#endif	/* ZCL_REPORT */

#ifdef ZCL_BASIC
/*********************************************************************
 * @fn      app_zclBasicResetCmdHandler
 *
 * @brief   Handler for ZCL Basic Reset command.
 *
 * @param   pAddrInfo
 * @param   cmdId
 * @param   cmdPayload
 *
 * @return  status_t
 */
status_t app_basicCb(zclIncomingAddrInfo_t *pAddrInfo, uint8_t cmdId, void *cmdPayload)
{
	if(cmdId == ZCL_CMD_BASIC_RESET_FAC_DEFAULT){
		//Reset all the attributes of all its clusters to factory defaults
		//zcl_nv_attr_reset();
	}

	return ZCL_STA_SUCCESS;
}
#endif	/* ZCL_BASIC */

#ifdef ZCL_IDENTIFY
int32_t app_zclIdentifyTimerCb(void *arg)
{
	if(g_zcl_identifyAttrs.identifyTime <= 0){
		identifyTimerEvt = NULL;
		return -1;
	}
	g_zcl_identifyAttrs.identifyTime--;
	return 0;
}

void app_zclIdentifyTimerStop(void)
{
	if(identifyTimerEvt){
		TL_ZB_TIMER_CANCEL(&identifyTimerEvt);
	}
}

/*********************************************************************
 * @fn      app_zclIdentifyCmdHandler
 *
 * @brief   Handler for ZCL Identify command. This function will set blink LED.
 *
 * @param   endpoint
 * @param   srcAddr
 * @param   identifyTime - identify time
 *
 * @return  None
 */
void app_zclIdentifyCmdHandler(uint8_t endpoint, uint16_t srcAddr, uint16_t identifyTime)
{
	g_zcl_identifyAttrs.identifyTime = identifyTime;

	if(identifyTime == 0){
		app_zclIdentifyTimerStop();
//		light_blink_stop();
	}else{
		if(!identifyTimerEvt){
//			light_blink_start(identifyTime, 500, 500);
			identifyTimerEvt = TL_ZB_TIMER_SCHEDULE(app_zclIdentifyTimerCb, NULL, 1000);
		}
	}
}

/*********************************************************************
 * @fn      app_zcltriggerCmdHandler
 *
 * @brief   Handler for ZCL trigger command.
 *
 * @param   pTriggerEffect
 *
 * @return  None
 */
static void app_zcltriggerCmdHandler(zcl_triggerEffect_t *pTriggerEffect)
{
	uint8_t effectId = pTriggerEffect->effectId;
	//uint8_t effectVariant = pTriggerEffect->effectVariant;

	switch(effectId){
		case IDENTIFY_EFFECT_BLINK:
//			light_blink_start(1, 500, 500);
			break;
		case IDENTIFY_EFFECT_BREATHE:
//			light_blink_start(15, 300, 700);
			break;
		case IDENTIFY_EFFECT_OKAY:
//			light_blink_start(2, 250, 250);
			break;
		case IDENTIFY_EFFECT_CHANNEL_CHANGE:
//			light_blink_start(1, 500, 7500);
			break;
		case IDENTIFY_EFFECT_FINISH_EFFECT:
//			light_blink_start(1, 300, 700);
			break;
		case IDENTIFY_EFFECT_STOP_EFFECT:
//			light_blink_stop();
			break;
		default:
			break;
	}
}

/*********************************************************************
 * @fn      app_zclIdentifyQueryRspCmdHandler
 *
 * @brief   Handler for ZCL Identify Query response command.
 *
 * @param   endpoint
 * @param   srcAddr
 * @param   identifyRsp
 *
 * @return  None
 */
static void app_zclIdentifyQueryRspCmdHandler(uint8_t endpoint, uint16_t srcAddr, zcl_identifyRspCmd_t *identifyRsp)
{
#if FIND_AND_BIND_SUPPORT
	if(identifyRsp->timeout){
		findBindDst_t dstInfo;
		dstInfo.addr = srcAddr;
		dstInfo.endpoint = endpoint;

		bdb_addIdentifyActiveEpForFB(dstInfo);
	}
#endif
}

/*********************************************************************
 * @fn      app_identifyCb
 *
 * @brief   Handler for ZCL Identify command.
 *
 * @param   pAddrInfo
 * @param   cmdId
 * @param   cmdPayload
 *
 * @return  status_t
 */
status_t app_identifyCb(zclIncomingAddrInfo_t *pAddrInfo, uint8_t cmdId, void *cmdPayload)
{

//    printf("app_identifyCb()\r\n");

	if(pAddrInfo->dstEp == APP_ENDPOINT1){
		if(pAddrInfo->dirCluster == ZCL_FRAME_CLIENT_SERVER_DIR){
			switch(cmdId){
				case ZCL_CMD_IDENTIFY:
					app_zclIdentifyCmdHandler(pAddrInfo->dstEp, pAddrInfo->srcAddr, ((zcl_identifyCmd_t *)cmdPayload)->identifyTime);
					break;
				case ZCL_CMD_TRIGGER_EFFECT:
					app_zcltriggerCmdHandler((zcl_triggerEffect_t *)cmdPayload);
					break;
				default:
					break;
			}
		}else{
			if(cmdId == ZCL_CMD_IDENTIFY_QUERY_RSP){
				app_zclIdentifyQueryRspCmdHandler(pAddrInfo->dstEp, pAddrInfo->srcAddr, (zcl_identifyRspCmd_t *)cmdPayload);
			}
		}
	}

	return ZCL_STA_SUCCESS;
}
#endif	/* ZCL_IDENTIFY */

#ifdef ZCL_IAS_ZONE
/*********************************************************************
 * @fn      leak_zclIasZoneEnrollRspCmdHandler
 *
 * @brief   Handler for ZCL IAS ZONE Enroll response command.
 *
 * @param   pZoneEnrollRsp
 *
 * @return  None
 */
static void leak_zclIasZoneEnrollRspCmdHandler(zoneEnrollRsp_t *pZoneEnrollRsp, uint8_t endpoint) {

    //printf("zclIasZoneEnrollRspCmdHandler endpoint: %d, code: %d zone_id: %d\r\n", endpoint, pZoneEnrollRsp->code, pZoneEnrollRsp->zoneId);
    if (pZoneEnrollRsp->zoneId != ZCL_ZONE_ID_INVALID) {
        u8 zoneState;
        zoneState = ZONE_STATE_ENROLLED;
        zcl_setAttrVal(endpoint, ZCL_CLUSTER_SS_IAS_ZONE, ZCL_ATTRID_ZONE_ID, &(pZoneEnrollRsp->zoneId));
        zcl_setAttrVal(endpoint, ZCL_CLUSTER_SS_IAS_ZONE, ZCL_ATTRID_ZONE_STATE, &zoneState);
    }

}

/*********************************************************************
 * @fn      leak_zclIasZoneInitNormalOperationModeCmdHandler
 *
 * @brief   Handler for ZCL IAS ZONE normal operation mode command.
 *
 * @param
 *
 * @return  status
 */
static status_t leak_zclIasZoneInitNormalOperationModeCmdHandler(void) {

    //printf("zclIasZoneInitNormalOperationModeCmdHandler\r\n");
    u8 status = ZCL_STA_FAILURE;

    return status;
}

/*********************************************************************
 * @fn      leak_zclIasZoneInitTestModeCmdHandler
 *
 * @brief   Handler for ZCL IAS ZONE test mode command.
 *
 * @param   pZoneInitTestMode
 *
 * @return  status
 */
static status_t leak_zclIasZoneInitTestModeCmdHandler(zoneInitTestMode_t *pZoneInitTestMode) {

    //printf("zclIasZoneInitNormalOperationModeCmdHandler\r\n");
    u8 status = ZCL_STA_FAILURE;

    return status;
}

/*********************************************************************
 * @fn      leak_iasZoneCb
 *
 * @brief   Handler for ZCL IAS Zone command.
 *
 * @param   pAddrInfo
 * @param   cmdId
 * @param   cmdPayload
 *
 * @return  status_t
 */
status_t leak_iasZoneCb(zclIncomingAddrInfo_t *pAddrInfo, u8 cmdId, void *cmdPayload)
{
    status_t status = ZCL_STA_SUCCESS;

    if(pAddrInfo->dstEp == APP_ENDPOINT4 || pAddrInfo->dstEp == APP_ENDPOINT5){
        if(pAddrInfo->dirCluster == ZCL_FRAME_CLIENT_SERVER_DIR){
            switch(cmdId){
                case ZCL_CMD_ZONE_ENROLL_RSP:
                    leak_zclIasZoneEnrollRspCmdHandler((zoneEnrollRsp_t *)cmdPayload, pAddrInfo->dstEp);
                    break;
                case ZCL_CMD_INIT_NORMAL_OPERATION_MODE:
                    leak_zclIasZoneInitNormalOperationModeCmdHandler();
                    break;
                case ZCL_CMD_INIT_TEST_MODE:
                    leak_zclIasZoneInitTestModeCmdHandler((zoneInitTestMode_t *)cmdPayload);
                    break;
                default:
                    break;
            }
        }
    }

    return status;
}
#endif  /* ZCL_IAS_ZONE */


/*********************************************************************
 * @fn      app_powerCfgCb
 *
 * @brief   Handler for ZCL Identify command.
 *
 * @param   pAddrInfo
 * @param   cmdId
 * @param   cmdPayload
 *
 * @return  status_t
 */
status_t app_powerCfgCb(zclIncomingAddrInfo_t *pAddrInfo, uint8_t cmdId, void *cmdPayload)
{

    return ZCL_STA_SUCCESS;
}


#ifdef ZCL_GROUP
/*********************************************************************
 * @fn      app_zclAddGroupRspCmdHandler
 *
 * @brief   Handler for ZCL add group response command.
 *
 * @param   pAddGroupRsp
 *
 * @return  None
 */
static void app_zclAddGroupRspCmdHandler(uint8_t ep, zcl_addGroupRsp_t *pAddGroupRsp) {

//    printf("app_zclAddGroupRspCmdHandler. ep: %d, status: %d, gid: %d\r\n", ep, pAddGroupRsp->status, pAddGroupRsp->groupId);
}

/*********************************************************************
 * @fn      app_zclViewGroupRspCmdHandler
 *
 * @brief   Handler for ZCL view group response command.
 *
 * @param   pViewGroupRsp
 *
 * @return  None
 */
static void app_zclViewGroupRspCmdHandler(zcl_viewGroupRsp_t *pViewGroupRsp)
{

}

/*********************************************************************
 * @fn      app_zclRemoveGroupRspCmdHandler
 *
 * @brief   Handler for ZCL remove group response command.
 *
 * @param   pRemoveGroupRsp
 *
 * @return  None
 */
static void app_zclRemoveGroupRspCmdHandler(zcl_removeGroupRsp_t *pRemoveGroupRsp)
{

}

/*********************************************************************
 * @fn      app_zclGetGroupMembershipRspCmdHandler
 *
 * @brief   Handler for ZCL get group membership response command.
 *
 * @param   pGetGroupMembershipRsp
 *
 * @return  None
 */
static void app_zclGetGroupMembershipRspCmdHandler(zcl_getGroupMembershipRsp_t *pGetGroupMembershipRsp)
{

}

/*********************************************************************
 * @fn      app_groupCb
 *
 * @brief   Handler for ZCL Group command.
 *
 * @param   pAddrInfo
 * @param   cmdId
 * @param   cmdPayload
 *
 * @return  status_t
 */
status_t app_groupCb(zclIncomingAddrInfo_t *pAddrInfo, uint8_t cmdId, void *cmdPayload) {

//    printf("app_groupCb. ep: %d\r\n", pAddrInfo->dstEp);

	if(pAddrInfo->dstEp == APP_ENDPOINT1 || pAddrInfo->dstEp == APP_ENDPOINT2) {
		if(pAddrInfo->dirCluster == ZCL_FRAME_SERVER_CLIENT_DIR){
			switch(cmdId){
				case ZCL_CMD_GROUP_ADD_GROUP_RSP:
					app_zclAddGroupRspCmdHandler(pAddrInfo->dstEp, (zcl_addGroupRsp_t *)cmdPayload);
					break;
				case ZCL_CMD_GROUP_VIEW_GROUP_RSP:
					app_zclViewGroupRspCmdHandler((zcl_viewGroupRsp_t *)cmdPayload);
					break;
				case ZCL_CMD_GROUP_REMOVE_GROUP_RSP:
					app_zclRemoveGroupRspCmdHandler((zcl_removeGroupRsp_t *)cmdPayload);
					break;
				case ZCL_CMD_GROUP_GET_MEMBERSHIP_RSP:
					app_zclGetGroupMembershipRspCmdHandler((zcl_getGroupMembershipRsp_t *)cmdPayload);
					break;
				default:
					break;
			}
		}
	}

	return ZCL_STA_SUCCESS;
}
#endif	/* ZCL_GROUP */

#ifdef ZCL_SCENE
/*********************************************************************
 * @fn      app_zclAddSceneRspCmdHandler
 *
 * @brief   Handler for ZCL add scene response command.
 *
 * @param   cmdId
 * @param   pAddSceneRsp
 *
 * @return  None
 */
static void app_zclAddSceneRspCmdHandler(uint8_t cmdId, addSceneRsp_t *pAddSceneRsp)
{

}

/*********************************************************************
 * @fn      app_zclViewSceneRspCmdHandler
 *
 * @brief   Handler for ZCL view scene response command.
 *
 * @param   cmdId
 * @param   pViewSceneRsp
 *
 * @return  None
 */
static void app_zclViewSceneRspCmdHandler(uint8_t cmdId, viewSceneRsp_t *pViewSceneRsp)
{

}

/*********************************************************************
 * @fn      app_zclRemoveSceneRspCmdHandler
 *
 * @brief   Handler for ZCL remove scene response command.
 *
 * @param   pRemoveSceneRsp
 *
 * @return  None
 */
static void app_zclRemoveSceneRspCmdHandler(removeSceneRsp_t *pRemoveSceneRsp)
{

}

/*********************************************************************
 * @fn      app_zclRemoveAllSceneRspCmdHandler
 *
 * @brief   Handler for ZCL remove all scene response command.
 *
 * @param   pRemoveAllSceneRsp
 *
 * @return  None
 */
static void app_zclRemoveAllSceneRspCmdHandler(removeAllSceneRsp_t *pRemoveAllSceneRsp)
{

}

/*********************************************************************
 * @fn      app_zclStoreSceneRspCmdHandler
 *
 * @brief   Handler for ZCL store scene response command.
 *
 * @param   pStoreSceneRsp
 *
 * @return  None
 */
static void app_zclStoreSceneRspCmdHandler(storeSceneRsp_t *pStoreSceneRsp)
{

}

/*********************************************************************
 * @fn      app_zclGetSceneMembershipRspCmdHandler
 *
 * @brief   Handler for ZCL get scene membership response command.
 *
 * @param   pGetSceneMembershipRsp
 *
 * @return  None
 */
static void app_zclGetSceneMembershipRspCmdHandler(getSceneMemRsp_t *pGetSceneMembershipRsp)
{

}

/*********************************************************************
 * @fn      app_sceneCb
 *
 * @brief   Handler for ZCL Scene command.
 *
 * @param   pAddrInfo
 * @param   cmdId
 * @param   cmdPayload
 *
 * @return  status_t
 */
status_t app_sceneCb(zclIncomingAddrInfo_t *pAddrInfo, uint8_t cmdId, void *cmdPayload)
{
	if(pAddrInfo->dstEp == APP_ENDPOINT1){
		if(pAddrInfo->dirCluster == ZCL_FRAME_SERVER_CLIENT_DIR){
			switch(cmdId){
				case ZCL_CMD_SCENE_ADD_SCENE_RSP:
				case ZCL_CMD_SCENE_ENHANCED_ADD_SCENE_RSP:
					app_zclAddSceneRspCmdHandler(cmdId, (addSceneRsp_t *)cmdPayload);
					break;
				case ZCL_CMD_SCENE_VIEW_SCENE_RSP:
				case ZCL_CMD_SCENE_ENHANCED_VIEW_SCENE_RSP:
					app_zclViewSceneRspCmdHandler(cmdId, (viewSceneRsp_t *)cmdPayload);
					break;
				case ZCL_CMD_SCENE_REMOVE_SCENE_RSP:
					app_zclRemoveSceneRspCmdHandler((removeSceneRsp_t *)cmdPayload);
					break;
				case ZCL_CMD_SCENE_REMOVE_ALL_SCENE_RSP:
					app_zclRemoveAllSceneRspCmdHandler((removeAllSceneRsp_t *)cmdPayload);
					break;
				case ZCL_CMD_SCENE_STORE_SCENE_RSP:
					app_zclStoreSceneRspCmdHandler((storeSceneRsp_t *)cmdPayload);
					break;
				case ZCL_CMD_SCENE_GET_SCENE_MEMSHIP_RSP:
					app_zclGetSceneMembershipRspCmdHandler((getSceneMemRsp_t *)cmdPayload);
					break;
				default:
					break;
			}
		}
	}

	return ZCL_STA_SUCCESS;
}
#endif	/* ZCL_SCENE */

#ifdef ZCL_POLL_CTRL
static ev_timer_event_t *zclFastPollTimeoutTimerEvt = NULL;
static ev_timer_event_t *zclCheckInTimerEvt = NULL;
static bool isFastPollMode = FALSE;

void app_zclCheckInCmdSend(void)
{
	epInfo_t dstEpInfo;
	TL_SETSTRUCTCONTENT(dstEpInfo, 0);

	dstEpInfo.dstAddrMode = APS_DSTADDR_EP_NOTPRESETNT;
	dstEpInfo.dstEp = APP_ENDPOINT;
	dstEpInfo.profileId = HA_PROFILE_ID;

	zcl_pollCtrl_checkInCmd(APP_ENDPOINT, &dstEpInfo, TRUE);
}

int32_t app_zclCheckInTimerCb(void *arg)
{
	zcl_pollCtrlAttr_t *pPollCtrlAttr = zcl_pollCtrlAttrGet();

	if(!pPollCtrlAttr->chkInInterval){
		zclCheckInTimerEvt = NULL;
		return -1;
	}

	app_zclCheckInCmdSend();

	return 0;
}

void app_zclCheckInStart(void)
{
	if(zb_bindingTblSearched(ZCL_CLUSTER_GEN_POLL_CONTROL, APP_ENDPOINT)){
		zcl_pollCtrlAttr_t *pPollCtrlAttr = zcl_pollCtrlAttrGet();

		if(!zclCheckInTimerEvt){
			zclCheckInTimerEvt = TL_ZB_TIMER_SCHEDULE(app_zclCheckInTimerCb, NULL, pPollCtrlAttr->chkInInterval * POLL_RATE_QUARTERSECONDS);
			
			if(pPollCtrlAttr->chkInInterval){
				app_zclCheckInCmdSend();
			}
		}
	}
}

void app_zclSetFastPollMode(bool fastPollMode)
{
	zcl_pollCtrlAttr_t *pPollCtrlAttr = zcl_pollCtrlAttrGet();

	isFastPollMode = fastPollMode;
	uint32_t pollRate = fastPollMode ? pPollCtrlAttr->shortPollInterval : pPollCtrlAttr->longPollInterval;

	zb_setPollRate(pollRate  * POLL_RATE_QUARTERSECONDS);
}

int32_t app_zclFastPollTimeoutCb(void *arg)
{
	app_zclSetFastPollMode(FALSE);

	zclFastPollTimeoutTimerEvt = NULL;
	return -1;
}

static status_t app_zclPollCtrlChkInRspCmdHandler(zcl_chkInRsp_t *pCmd)
{
	zcl_pollCtrlAttr_t *pPollCtrlAttr = zcl_pollCtrlAttrGet();

	if(pCmd->startFastPolling){
		uint16_t fastPollTimeoutCnt = 0;

		if(pCmd->fastPollTimeout){
			if(pCmd->fastPollTimeout > pPollCtrlAttr->fastPollTimeoutMax){
				return ZCL_STA_INVALID_FIELD;
			}

			fastPollTimeoutCnt = pCmd->fastPollTimeout;
		}else{
			fastPollTimeoutCnt = pPollCtrlAttr->fastPollTimeout;
		}

		if(fastPollTimeoutCnt){
			app_zclSetFastPollMode(TRUE);
			
			if(zclFastPollTimeoutTimerEvt){
				TL_ZB_TIMER_CANCEL(&zclFastPollTimeoutTimerEvt);
			}
			zclFastPollTimeoutTimerEvt = TL_ZB_TIMER_SCHEDULE(app_zclFastPollTimeoutCb, NULL, fastPollTimeoutCnt * POLL_RATE_QUARTERSECONDS);
		}
	}else{
		//continue in normal operation and not required to go into fast poll mode.
	}

	return ZCL_STA_SUCCESS;
}

static status_t app_zclPollCtrlFastPollStopCmdHandler(void)
{
	if(!isFastPollMode){
		return ZCL_STA_ACTION_DENIED;
	}else{
		if(zclFastPollTimeoutTimerEvt){
			TL_ZB_TIMER_CANCEL(&zclFastPollTimeoutTimerEvt);
		}
		app_zclSetFastPollMode(FALSE);
	}

	return ZCL_STA_SUCCESS;
}

static status_t app_zclPollCtrlSetLongPollIntervalCmdHandler(zcl_setLongPollInterval_t *pCmd)
{
	zcl_pollCtrlAttr_t *pPollCtrlAttr = zcl_pollCtrlAttrGet();

	if((pCmd->newLongPollInterval >= 0x04) && (pCmd->newLongPollInterval <= 0x6E0000)
		&& (pCmd->newLongPollInterval <= pPollCtrlAttr->chkInInterval) && (pCmd->newLongPollInterval >= pPollCtrlAttr->shortPollInterval)){
		pPollCtrlAttr->longPollInterval = pCmd->newLongPollInterval;
		zb_setPollRate(pCmd->newLongPollInterval * POLL_RATE_QUARTERSECONDS);
	}else{
		return ZCL_STA_INVALID_VALUE;
	}

	return ZCL_STA_SUCCESS;
}

static status_t app_zclPollCtrlSetShortPollIntervalCmdHandler(zcl_setShortPollInterval_t *pCmd)
{
	zcl_pollCtrlAttr_t *pPollCtrlAttr = zcl_pollCtrlAttrGet();

	if((pCmd->newShortPollInterval >= 0x01) && (pCmd->newShortPollInterval <= 0xff)
		&& (pCmd->newShortPollInterval <= pPollCtrlAttr->longPollInterval)){
		pPollCtrlAttr->shortPollInterval = pCmd->newShortPollInterval;
		zb_setPollRate(pCmd->newShortPollInterval * POLL_RATE_QUARTERSECONDS);
	}else{
		return ZCL_STA_INVALID_VALUE;
	}

	return ZCL_STA_SUCCESS;
}

/*********************************************************************
 * @fn      app_pollCtrlCb
 *
 * @brief   Handler for ZCL Poll Control command.
 *
 * @param   pAddrInfo
 * @param   cmdId
 * @param   cmdPayload
 *
 * @return  status_t
 */
status_t app_pollCtrlCb(zclIncomingAddrInfo_t *pAddrInfo, uint8_t cmdId, void *cmdPayload)
{
	status_t status = ZCL_STA_SUCCESS;

	if(pAddrInfo->dstEp == APP_ENDPOINT){
		if(pAddrInfo->dirCluster == ZCL_FRAME_CLIENT_SERVER_DIR){
			switch(cmdId){
				case ZCL_CMD_CHK_IN_RSP:
					status = app_zclPollCtrlChkInRspCmdHandler((zcl_chkInRsp_t *)cmdPayload);
					break;
				case ZCL_CMD_FAST_POLL_STOP:
					status = app_zclPollCtrlFastPollStopCmdHandler();
					break;
				case ZCL_CMD_SET_LONG_POLL_INTERVAL:
					status = app_zclPollCtrlSetLongPollIntervalCmdHandler((zcl_setLongPollInterval_t *)cmdPayload);
					break;
				case ZCL_CMD_SET_SHORT_POLL_INTERVAL:
					status = app_zclPollCtrlSetShortPollIntervalCmdHandler((zcl_setShortPollInterval_t *)cmdPayload);
					break;
				default:
					break;
			}
		}
	}

	return status;
}
#endif	/* ZCL_POLL_CTRL */

#ifdef ZCL_THERMOSTAT

status_t app_thermostatCb(zclIncomingAddrInfo_t *pAddrInfo, uint8_t cmdId, void *cmdPayload) {

//    printf("app_thermostatCb(). pAddrInfo->dirCluster: %0x%x, cmdId: 0x%x\r\n", pAddrInfo->dirCluster, cmdId);

    status_t status = ZCL_STA_SUCCESS;

    if (pAddrInfo->dstEp != APP_ENDPOINT1 ||
        pAddrInfo->dirCluster != ZCL_FRAME_CLIENT_SERVER_DIR) {
        return status;
    }

    switch (cmdId) {
//        case ZCL_CMD_THERMOSTAT_SETPOINT_RAISE_LOWER: {
//            zcl_thermostat_setpointRaiseLowerCmd_t *cmd = (zcl_thermostat_setpointRaiseLowerCmd_t*)cmdPayload;
//            if (cmd->mode == ADJUST_HEAT_SETPOINT) {
//                int8_t raise_lower = cmd->amount / 10;
//                if (raise_lower >= -5 && raise_lower <= 5) {
//                    int16_t setpoint = g_zcl_thermostatAttrs.occupiedHeatingSetpoint;
//                    setpoint += raise_lower * 100;
//                    App_Link_SendSettingI16(LNK_CMD_SETPOINT_HEAT, setpoint);
//                }
//            }
//            break;
//        }
        case ZCL_CMD_THERMOSTAT_SET_WEEKLY_SCHEDULE:
            status = App_Thermostat_SetWeeklySchedule(
                         (zcl_thermostat_setWeeklyScheduleCmd_t *)cmdPayload);
            break;
        case ZCL_CMD_THERMOSTAT_GET_WEEKLY_SCHEDULE:
            status = App_Thermostat_GetWeeklySchedule(pAddrInfo,
                         (zcl_thermostat_getWeeklyScheduleCmd_t *)cmdPayload);
            break;
        default:
            break;
    }

    return status;
}

status_t app_thermostat_uicCb(zclIncomingAddrInfo_t *pAddrInfo, uint8_t cmdId, void *cmdPayload) {

//    printf("app_thermostatCb(). pAddrInfo->dirCluster: %0x%x, cmdId: 0x%x\r\n", pAddrInfo->dirCluster, cmdId);

//    uint8_t *cmdData = (uint8_t*)cmdPayload;

    status_t status = ZCL_STA_SUCCESS;

//    if(pAddrInfo->dstEp == APP_ENDPOINT1) {
//        switch(cmdId) {
//            case ZCL_CMD_UP_OPEN:
//                printf("CMD Open\r\n");
//                remote_cmd_ocs(pAddrInfo->dstEp, cmdId);
//                break;
//            case ZCL_CMD_DOWN_CLOSE:
//                printf("CMD Close\r\n");
//                remote_cmd_ocs(pAddrInfo->dstEp, cmdId);
//                break;
//            case ZCL_CMD_STOP:
//                printf("CMD Stop\r\n");
//                remote_cmd_ocs(pAddrInfo->dstEp, cmdId);
//                break;
//            case ZCL_CMD_GO_TO_LIFT_PERCENTAGE:
//                printf("CMD go to lift percentage. Payload: 0x%x\r\n", *cmdData);
//                if (*cmdData <= 100) remote_cmd_goToLiftPer(pAddrInfo->dstEp, *cmdData);
//                else status = ZCL_STA_INVALID_VALUE;
//                break;
//            default:
//                break;
//        }
//    }


    return status;
}

#endif /* ZCL_THERMOSTAT */

#ifdef ZCL_FANCONTROL

status_t app_fancontrolCb(zclIncomingAddrInfo_t *pAddrInfo, uint8_t cmdId, void *cmdPayload) {

//    printf("app_thermostatCb().\r\n");

    status_t status = ZCL_STA_SUCCESS;

    return status;
}

#endif /* ZCL_FANCONTROL */

/*********************************************************************
 * @fn      app_timeCb
 *
 * @brief   Handler for ZCL Time command.
 *
 * @param   pAddrInfo
 * @param   cmdId
 * @param   cmdPayload
 *
 * @return  status_t
 */
status_t app_timeCb(zclIncomingAddrInfo_t *pAddrInfo, uint8_t cmdId, void *cmdPayload) {

//    printf("app_timeCb. cmd: 0x%x\r\n", cmdId);

    return ZCL_STA_SUCCESS;
}


/*********************************************************************
 * @fn      app_displayMoveToLevelProcess
 *
 * @brief
 *
 * @param   cmdId
 * @param   cmd
 *
 * @return  None
 */

static void app_displayMoveToLevelProcess(uint8_t endpoint, uint8_t cmdId, moveToLvl_t *cmd) {

    if (endpoint == APP_ENDPOINT1) {
        /* EP1 Level Control = day brightness (0..maxLevelA, matches CA51F2) */
        uint8_t v = app_clamp_u8_max(cmd->level, g_zcl_levelAttrs.maxLevelA);
        g_zcl_levelAttrs.currentLevelA = v;
        /* 0 return = "stashed in pending until the handshake" (or queue       */
        /* full, also stashed) - the value is NOT lost, so never restart the   */
        /* handshake here (same contract as App_Thermostat_WriteAttr).         */
        App_Link_SendSettingU8(LNK_CMD_BRIGHT_DAY, LNK_T_U8, v);
    } else if (endpoint == APP_ENDPOINT2) {
        /* EP2 Level Control = night brightness (0..maxLevelB)               */
        uint8_t v = app_clamp_u8_max(cmd->level, g_zcl_levelAttrs.maxLevelB);
        g_zcl_levelAttrs.currentLevelB = v;
        App_Link_SendSettingU8(LNK_CMD_BRIGHT_NIGHT, LNK_T_U8, v);
    }
}


/*********************************************************************
 * @fn      app_displaylevelCb
 *
 * @brief   Handler for ZCL LEVEL command. This function will set LEVEL attribute first.
 *
 * @param   pAddrInfo
 * @param   cmd - level cluster command id
 * @param   cmdPayload
 *
 * @return  status_t
 */

status_t app_displayLevelCb(zclIncomingAddrInfo_t *pAddrInfo, u8 cmdId, void *cmdPayload) {
    if(pAddrInfo->dstEp == APP_ENDPOINT1 || pAddrInfo->dstEp == APP_ENDPOINT2) {
        switch(cmdId){
            case ZCL_CMD_LEVEL_MOVE_TO_LEVEL:
            case ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF:
                app_displayMoveToLevelProcess(pAddrInfo->dstEp, cmdId, (moveToLvl_t *)cmdPayload);
                break;
            default:
                break;
        }
    }

    return ZCL_STA_SUCCESS;
}

/*********************************************************************
 * @fn      app_humidityCb
 *
 * @brief   Handler for ZCL Humidity command.
 *
 * @param   pAddrInfo
 * @param   cmdId
 * @param   cmdPayload
 *
 * @return  status_t
 */
status_t app_humidityCb(zclIncomingAddrInfo_t *pAddrInfo, uint8_t cmdId, void *cmdPayload) {

//    printf("app_humidityCb. cmd: 0x%x\r\n", cmdId);

    return ZCL_STA_SUCCESS;
}


