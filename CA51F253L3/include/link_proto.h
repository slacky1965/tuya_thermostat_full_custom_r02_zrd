#ifndef _LINK_PROTO_H_
#define _LINK_PROTO_H_

/********************************************************************************/
/* CA51F2 <-> ZT3L link: dispatcher + handler API. The protocol constants       */
/* (entity IDs, status codes, enums, INFO layout) are SHARED with the TLSR8258  */
/* side and live in the self-contained include_common/link_proto.h (root).      */
/* The frame layer (66 BB ... CRC) is the local uart.c; handlers never re-check */
/* CRC. This header pulls in the shared map and adds the C51 code model types.  */
/********************************************************************************/
#include "include/stdint.h"
#include "include/uart.h"
#include "../include_common/link_proto.h"

/*** Handler / dispatcher API **************************************************/
/* Result of a handler: reply status, data type and payload length.            */
typedef struct
{
    uint8_t status;       /* LNK_ST_*                                     */
    uint8_t type;         /* data type as in the frame (uart.h comment)   */
    uint8_t len;          /* payload bytes written to the output buffer   */
} link_res_t;

/* Handler context: a single pointer argument so indirect calls fit into the    */
/* C51 register parameter window (C212 otherwise).                              */
typedef struct
{
    const uart_frame_t xdata *f;
    link_res_t xdata *r;
} link_ctx_t;

typedef void (*link_cmd_h_t)(link_ctx_t xdata *cx);

/* Reset the link module state; call once from main() at boot (xdata statics     */
/* are not cleared by SDCC/8051 reset).                                          */
void Link_Init(void);
/* Frame callback registered with uart.c. Dispatches by Cmd ID, replies to       */
/* requests/commands (ACK=1), applies reports/announcements (ACK=0).             */
void Link_OnFrame(const uart_frame_t xdata *f);
/* Peer liveness: 1 once the ZT3L has requested our Info on boot (h_info).       */
uint8_t Link_PeerAlive(void);
/* Report accepted internal (0x02) and external (0x03) temperatures in c100;     */
/* call once per second while the peer is alive (send/keepalive policy is here). */
void Link_ReportTemps(int16_t in_c100, uint8_t in_ok, int16_t ext_c100, uint8_t ext_ok);
/* Drive the reliable-message queue (ACK/retry); call from the main loop.        */
void Link_Poll(void);
/* One-second service: hourly clock resync while the network is connected.       */
void Link_Tick1s(void);
/* Announce a (re)boot to the peer so it runs a fresh Info handshake; call once  */
/* from main() after the normal-mode boot (retried every 3 s until the peer OK). */
void Link_BootNotify(void);
/* Last known network status (LNK_NET_*), for the UI.                            */
uint8_t Link_NetStatus(void);
/* Last wireless climate sensor (WS) telemetry from the ZT3L (ZCL x100):       */
/* temperature 0.01 degC (rounded to whole percents by the ZT3L), humidity     */
/* 0.01 %RH (0..9900). RAM only.                                               */
int16_t  Link_WsTemp(void);
uint16_t Link_WsHumid(void);
/* Freshness window: 1 while a WS temperature arrived < WS_FRESH_SECS ago      */
/* (config.h); Link_Init boots it stale, so ONLY a temperature arrival opens   */
/* it (a humidity-only frame never does). Drives the relay override, the       */
/* display override, the sun symbol and the validity of BOTH WS values.        */
uint8_t  Link_WsFresh(void);
/* Report System Mode (0x0A) and Running State (0x09) when they change; call   */
/* from the main loop (both are sent reliably, with ACK/retry).                */
void Link_UpdateStates(uint8_t power_on, uint8_t relay_on);
/* Consume an incoming System Mode command; returns 1 and writes the mode.     */
uint8_t Link_TakeSysMode(uint8_t xdata *mode);
/* Consume an incoming keypad-lock change (0x17); main() blinks the padlock    */
/* while OFF so a remote lock toggle is visible on the dark display.           */
uint8_t Link_TakeLockChanged(void);
/* Consume an incoming scalar-setting change (h_setting); returns 1 and writes */
/* the LNK_CMD_* id. main() then shows the value for 7 s with the "Set"        */
/* indicator, exactly like a manual setpoint edit.                             */
uint8_t Link_TakeSettingChanged(uint8_t xdata *cmd);
/* Ask the peer to restore factory defaults (reliable, with ACK/retry).        */
uint8_t Link_SendFactoryReset(void);
/* Send a changed setting to the peer reliably (ACK/retry).                    */
uint8_t Link_SendI16(uint8_t cmd, int16_t v);
uint8_t Link_SendU8(uint8_t cmd, uint8_t type, uint8_t v);
/* Send one weekday's schedule (day 0..6 = Mon..Sun) reliably.                 */
uint8_t Link_SendSchedule(uint8_t day);
void Link_RetryPending(void);

#endif
