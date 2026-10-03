#ifndef TLSR8258_SRC_INCLUDE_APP_OTA_H_
#define TLSR8258_SRC_INCLUDE_APP_OTA_H_

#include "tl_common.h"

/* Zigbee-OTA completion hook: read the image the SDK stored at 0x77000 and
   either reboot into it (TLSR image) or forward it to the CA51F2 over UART1. */
void App_Ota_HandleImage(void);
/* OTA transfer state machine; call from app_task(). */
void App_Ota_Poll(void);
uint8_t App_Ota_IsActive(void);
uint8_t App_Ota_InfoAllowed(void);

#endif /* TLSR8258_SRC_INCLUDE_APP_OTA_H_ */
