#ifndef SRC_ZCL_ZCL_APP_FANCONTROL_H_
#define SRC_ZCL_ZCL_APP_FANCONTROL_H_

#include "app_cfg.h"

#if ZCL_FANCONTROL_SUPPORT
#define ZCL_FANCONTROL
#endif

#ifdef ZCL_FANCONTROL

#define ZCL_ATTRID_HVAC_FANCONTROL_FANMODE          0x0000
#define ZCL_ATTRID_HVAC_FANCONTROL_FANMODESEQ       0x0001

#define ZCL_ATTRID_HVAC_FANCONTROL_CUSTOM_CONTROL   0xF000

#define FANMODE_OFF                                 0x00
#define FANMODE_LOW                                 0x01
#define FANMODE_MEDIUM                              0x02
#define FANMODE_HIGH                                0x03
#define FANMODE_ON                                  0x04
#define FANMODE_AUTO                                0x05
#define FANMODE_SMART                               0x06

#define FANMODESEQ_LOW_MED_HIGH                     0x00
#define FANMODESEQ_LOW_HIGH                         0x01
#define FANMODESEQ_LOW_MED_HIGH_AUTO                0x02
#define FANMODESEQ_LOW_HIGH_AUTO                    0x03
#define FANMODESEQ_ON_AUTO                          0x04

#define FAN_CUSTOM_CONTROL_OFF                      0x00
#define FAN_CUSTOM_CONTROL_ON                       0x01

_CODE_ZCL_ status_t zcl_fancontrol_register(u8 endpoint, u16 manuCode, u8 arrtNum, const zclAttrInfo_t attrTbl[], cluster_forAppCb_t cb);

#endif

#endif /* SRC_ZCL_ZCL_APP_FANCONTROL_H_ */
