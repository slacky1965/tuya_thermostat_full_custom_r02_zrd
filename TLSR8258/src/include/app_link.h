#ifndef TLSR8258_SRC_INCLUDE_APP_LINK_H_
#define TLSR8258_SRC_INCLUDE_APP_LINK_H_

#include "tl_common.h"
#include "link_proto.h"     /* shared entity IDs / status codes (include_common) */
#include "app_uart.h"

/* ZT3L side of the CA51F2 <-> ZT3L link protocol (serial.md).
   Boot handshake initiator: after a 5 s power-on delay send an Info Req, retry
   3x/100 ms then repeat every 30 s until the first reply; answer Net-status
   requests and push a Net-status report on change. */
void App_Link_Init(void);
void App_Link_Poll(void);
/* Frame callback registered with App_Uart_Init(). */
void App_Link_OnFrame(const uart_frame_t *f);
/* Force the next Net-status report to be sent (e.g. after a ZDO leave).       */
void App_Link_NetForce(void);
/* Send the current time (utc + local, seconds) to the CA51F2 (reliable).       */
uint8_t App_Link_SendTime(uint32_t utc, uint32_t local);
/* Forward a scalar setting from the network to the CA51F2 (reliable).      */
uint8_t App_Link_SendSettingU8(uint8_t cmd, uint8_t type, uint8_t v);
uint8_t App_Link_SendSettingI16(uint8_t cmd, int16_t v);
/* Wireless climate sensor (WS) telemetry to the CA51F2 (reliable, not      */
/* stashed as a pending setting - dropped while the peer is down).          */
uint8_t App_Link_SendWsTemp(int16_t v);
uint8_t App_Link_SendWsHumid(uint16_t v);
/* Forward one weekly-schedule day (0 = Monday .. 6 = Sunday) to the CA51F2.   */
uint8_t App_Link_SendSchedule(uint8_t day);
/* CA51F2 firmware version from its Info payload (0 until the first Info RSP).  */
uint16_t App_Link_Ca51f2Ver(void);
/* 1 once the CA51F2 has answered our Info request.                            */
uint8_t App_Link_Ca51f2Alive(void);
/* Ask the CA51F2 to start the OTA data phase (bin_size u32 BE + ver u16 BE).   */
void App_Link_SendOtaStart(uint32_t size, uint16_t ver);
uint8_t App_Link_AllocSeq(void);
uint8_t App_Link_SendOtaStartWithSeq(uint8_t seq, uint32_t size, uint16_t ver);
/* Last OTA_START reply status (0xFF until a reply arrives).                    */
uint8_t App_Link_OtaRspStatus(void);
void App_Link_OtaRspClear(void);
void App_Link_RestartInfo(void);

#endif /* TLSR8258_SRC_INCLUDE_APP_LINK_H_ */
