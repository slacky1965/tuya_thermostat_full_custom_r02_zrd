#include "app_main.h"
#include "app_ota.h"

static bool boot_announce_sent = false;

app_ctx_t g_appCtx = {
        .bdbFBTimerEvt = NULL,
        .factory_resetTimerEvt = NULL,
};

//uint32_t count_restart = 0;

#ifdef ZCL_OTA
extern ota_callBack_t app_otaCb;

//running code firmware information
ota_preamble_t app_otaInfo = {
    .fileVer            = FILE_VERSION,
    .imageType          = IMAGE_TYPE,
    .manufacturerCode   = MANUFACTURER_CODE_TELINK,
};
#endif


//Must declare the application call back function which used by ZDO layer
const zdo_appIndCb_t appCbLst = {
    bdb_zdoStartDevCnf,           //start device cnf cb
    NULL,                         //reset cnf cb
    NULL,                         //device announce indication cb
    app_leaveIndHandler,          //leave ind cb
    app_leaveCnfHandler,          //leave cnf cb
    app_nwkUpdateIndicateHandler, //nwk update ind cb
    NULL,                         //permit join ind cb
    NULL,                         //nlme sync cnf cb
    NULL,                         //tc join ind cb
    NULL,                         //tc detects that the frame counter is near limit
};

/**
 *  @brief Definition for bdb commissioning setting
 */
bdb_commissionSetting_t g_bdbCommissionSetting = {
    .linkKey.tcLinkKey.keyType = SS_GLOBAL_LINK_KEY,
    .linkKey.tcLinkKey.key = (uint8_t *)tcLinkKeyCentralDefault,             //can use unique link key stored in NV

    .linkKey.distributeLinkKey.keyType = MASTER_KEY,
    .linkKey.distributeLinkKey.key = (uint8_t *)linkKeyDistributedMaster,    //use linkKeyDistributedCertification before testing

    .linkKey.touchLinkKey.keyType = MASTER_KEY,
    .linkKey.touchLinkKey.key = (uint8_t *)touchLinkKeyMaster,               //use touchLinkKeyCertification before testing

#if TOUCHLINK_SUPPORT
    .touchlinkEnable = 1,                                               /* enable touch-link */
#else
    .touchlinkEnable = 0,                                               /* disable touch-link */
#endif
    .touchlinkChannel = DEFAULT_CHANNEL,                                /* touch-link default operation channel for target */
    .touchlinkLqiThreshold = 0xA0,                                      /* threshold for touch-link scan req/resp command  */
};

/**********************************************************************
 * LOCAL VARIABLES
 */


/**********************************************************************
 * FUNCTIONS
 */


/*********************************************************************
 * @fn      stack_init
 *
 * @brief   This function initialize the ZigBee stack and related profile. If HA/ZLL profile is
 *          enabled in this application, related cluster should be registered here.
 *
 * @param   None
 *
 * @return  None
 */
void stack_init(void)
{
    /* Initialize ZB stack */
    zb_init();

    /* Register stack CB */
    zb_zdoCbRegister((zdo_appIndCb_t *)&appCbLst);
}

/*********************************************************************
 * @fn      user_app_init
 *
 * @brief   This function initialize the application(Endpoint) information for this node.
 *
 * @param   None
 *
 * @return  None
 */
void user_app_init(void)
{

#if ZCL_POLL_CTRL_SUPPORT
    af_powerDescPowerModeUpdate(POWER_MODE_RECEIVER_COMES_PERIODICALLY);
#else
    af_powerDescPowerModeUpdate(POWER_MODE_RECEIVER_COMES_WHEN_STIMULATED);
#endif

    /* Initialize ZCL layer                                       */
    /* Register Incoming ZCL Foundation command/response messages */
    zcl_init(app_zclProcessIncomingMsg);

    /* register endPoint */
    af_endpointRegister(APP_ENDPOINT1, (af_simple_descriptor_t *)&app_ep1Desc, zcl_rx_handler, NULL);
    af_endpointRegister(APP_ENDPOINT2, (af_simple_descriptor_t *)&app_ep2Desc, zcl_rx_handler, NULL);

    zcl_reportingTabInit();
//    thermostat_settings_restore();

    /* Register ZCL specific cluster information */
    zcl_register(APP_ENDPOINT1, APP_EP1_CB_CLUSTER_NUM, (zcl_specClusterInfo_t *)g_appEp1ClusterList);
    zcl_register(APP_ENDPOINT2, APP_EP2_CB_CLUSTER_NUM, (zcl_specClusterInfo_t *)g_appEp2ClusterList);

#if ZCL_GP_SUPPORT
    /* Initialize GP */
    gp_init(APP_ENDPOINT1);
#endif

#if ZCL_OTA_SUPPORT
    ota_init(OTA_TYPE_CLIENT, (af_simple_descriptor_t *)&app_ep1Desc, &app_otaInfo, &app_otaCb);
#endif


    /* Link transport + protocol (CA51F2 <-> ZT3L). */
    App_Uart_Init(App_Link_OnFrame);
    App_Link_Init();
}

void app_task(void) {

    App_Uart_Poll();               /* decode any pending link frames     */
    App_Link_Poll();               /* boot handshake / net-status report */
    App_Ota_Poll();                /* CA51F2 OTA transfer, if active     */

    if (!boot_announce_sent && zb_isDeviceJoinedNwk()) {
        zb_zdoSendDevAnnance();
        boot_announce_sent = true;
        APP_DEBUG(DEBUG_ZB_CB_EN, "Sent announcement\r\n");
    }

    if(bdb_isIdle()) {
        report_handler();
    }
}

extern volatile uint16_t T_evtExcept[4];

static void appSysException(void) {

    APP_DEBUG(DEBUG_ZB_CB_EN, "app_sysException, line: %d, event: %d, reset\r\n", T_evtExcept[0], T_evtExcept[1]);

#if 1
    SYSTEM_RESET();
#else
    led_on(LED_STATUS);
    while(1);
#endif
}

/*********************************************************************
 * @fn      user_init
 *
 * @brief   User level initialization code.
 *
 * @param   isRetention - if it is waking up with ram retention.
 *
 * @return  None
 */
void user_init(bool isRetention) {

    (void)isRetention;

    start_message();

    /* Initialize Stack */
    stack_init();

    /* Initialize user application */
    user_app_init();

    /* Register except handler for test */
    sys_exceptHandlerRegister(appSysException);


    /* User's Task */
#if ZBHCI_EN
    zbhciInit();
    ev_on_poll(EV_POLL_HCI, zbhciTask);
#endif
    ev_on_poll(EV_POLL_IDLE, app_task);

    /* Read the pre-install code from NV */
    if(bdb_preInstallCodeLoad(&g_appCtx.tcLinkKey.keyType, g_appCtx.tcLinkKey.key) == RET_OK){
        g_bdbCommissionSetting.linkKey.tcLinkKey.keyType = g_appCtx.tcLinkKey.keyType;
        g_bdbCommissionSetting.linkKey.tcLinkKey.key = g_appCtx.tcLinkKey.key;
    }

    /* Set default reporting configuration */
    uint8_t reportableChange = 0x00;

    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
                            ZCL_ATTRID_HVAC_THERMOSTAT_LOCAL_TEMPERATURE, 0, 3600, (uint8_t *)&reportableChange);
    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
                            ZCL_ATTRID_HVAC_THERMOSTAT_OCCUPIED_HEATING_SETPOINT, 0, 3600, (uint8_t *)&reportableChange);
    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
                            ZCL_ATTRID_HVAC_THERMOSTAT_SYS_MODE, 0, 3600, (uint8_t *)&reportableChange);
    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
                            ZCL_ATTRID_HVAC_THERMOSTAT_RUNNING_STATE, 0, 3600, (uint8_t *)&reportableChange);
    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
                            ZCL_ATTRID_HVAC_THERMOSTAT_PROGRAMMING_OPERATION_MODE, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
//                            ZCL_ATTRID_HVAC_THERMOSTAT_CUSTOM_SENSOR_USED, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
//                            ZCL_ATTRID_HVAC_THERMOSTAT_MIN_SETPOINT_DEAD_BAND, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
//                            ZCL_ATTRID_HVAC_THERMOSTAT_MIN_HEAT_SETPOINT_LIMIT, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
//                            ZCL_ATTRID_HVAC_THERMOSTAT_MAX_HEAT_SETPOINT_LIMIT, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
//                            ZCL_ATTRID_HVAC_THERMOSTAT_MIN_COOL_SETPOINT_LIMIT, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
//                            ZCL_ATTRID_HVAC_THERMOSTAT_MAX_COOL_SETPOINT_LIMIT, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
//                            ZCL_ATTRID_HVAC_THERMOSTAT_OUTDOOR_TEMPERATURE, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
//                            ZCL_ATTRID_HVAC_THERMOSTAT_CUSTOM_FROST_PROTECT, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
//                            ZCL_ATTRID_HVAC_THERMOSTAT_CUSTOM_HEAT_PROTECT, 0, 3600, (uint8_t *)&reportableChange);
////    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_USER_INTERFACE_CONFIG,
////                            ZCL_ATTRID_HVAC_TEMPERATURE_DISPLAY_MODE, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_USER_INTERFACE_CONFIG,
//                            ZCL_ATTRID_HVAC_KEYPAD_LOCKOUT, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_GEN_LEVEL_CONTROL,
//                            ZCL_ATTRID_LEVEL_CURRENT_LEVEL, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT2, HA_PROFILE_ID, ZCL_CLUSTER_GEN_LEVEL_CONTROL,
//                            ZCL_ATTRID_LEVEL_CURRENT_LEVEL, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
//                            ZCL_ATTRID_HVAC_THERMOSTAT_CUSTOM_ECO_MODE, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
//                            ZCL_ATTRID_HVAC_THERMOSTAT_CUSTOM_ECO_MODE_HEAT_TEMPERATURE, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
//                            ZCL_ATTRID_HVAC_THERMOSTAT_CUSTOM_ECO_MODE_COOL_TEMPERATURE, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
//                            ZCL_ATTRID_HVAC_THERMOSTAT_CUSTOM_FROST_PROTECT_ONOFF, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
//                            ZCL_ATTRID_HVAC_THERMOSTAT_CUSTOM_SCHEDULE_MODE, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
//                            ZCL_ATTRID_HVAC_THERMOSTAT_CUSTOM_SOUND, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
//                            ZCL_ATTRID_HVAC_THERMOSTAT_CUSTOM_LEVEL, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
//                            ZCL_ATTRID_HVAC_THERMOSTAT_CUSTOM_INVERSION, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_THERMOSTAT,
//                            ZCL_ATTRID_HVAC_THERMOSTAT_CUSTOM_EXT_TEMP_CALIBRATION, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_FAN_CONTROL,
//                            ZCL_ATTRID_HVAC_FANCONTROL_FANMODE, 0, 3600, (uint8_t *)&reportableChange);
//    bdb_defaultReportingCfg(APP_ENDPOINT1, HA_PROFILE_ID, ZCL_CLUSTER_HAVC_FAN_CONTROL,
//                            ZCL_ATTRID_HVAC_FANCONTROL_CUSTOM_CONTROL, 0, 3600, (uint8_t *)&reportableChange);

    /* Initialize BDB */
    bdb_init((af_simple_descriptor_t *)&app_ep1Desc, &g_bdbCommissionSetting, &g_zbBdbCb, 1);

    rf_setTxPower(MY_RF_POWER_INDEX);
}

