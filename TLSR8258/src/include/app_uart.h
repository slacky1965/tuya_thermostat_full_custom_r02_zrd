#ifndef TLSR8258_SRC_INCLUDE_APP_UART_H_
#define TLSR8258_SRC_INCLUDE_APP_UART_H_

#include "tl_common.h"
#include "link_frame.h"

/* Link frame as received / to be sent (mirrors the CA51F2 uart.h type).        */
/* Multi-byte numbers are big-endian; payload is a parser-owned static buffer.  */
typedef struct {
	uint8_t seq;                         /* initiator counter, echoed in replies  */
	uint8_t flags;                       /* UART_F_RSP / UART_F_ACK / UART_F_CMD  */
	uint8_t status;                      /* replies only: 0 = OK, != 0 = error    */
	uint8_t cmd;                         /* command/data ID (link_proto.h)        */
	uint8_t type;                        /* data type; only meaningful when len>0 */
	uint8_t len;                         /* payload length, 0..UART_FRAME_PAYLOAD */
	uint8_t payload[UART_FRAME_PAYLOAD];
} uart_frame_t;

typedef void (*uart_frame_cb_t)(const uart_frame_t *f);

/* UART0 on the ZT3L board: TX = PB1, RX = PB7, 115200 8N1 (board_tuya.h).      */
#define APP_UART_BAUDRATE    115200

/* Init UART0 + DMA RX; cb receives every CRC-valid frame (called from Poll).   */
void App_Uart_Init(uart_frame_cb_t cb);
/* Feed pending RX bytes into the frame parser (call periodically, e.g. from    */
/* app_task); dispatches completed frames to the callback.                      */
void App_Uart_Poll(void);
/* Build and send one frame (CRC-8/MAXIM); returns 1 on success, 0 if busy.     */
uint8_t App_Uart_SendFrame(uint8_t seq, uint8_t flags, uint8_t status,
                           uint8_t cmd, uint8_t type, uint8_t len,
                           const uint8_t *payload);

/* Raw mode: after App_Uart_SetRaw(1) received bytes bypass the frame parser and
   are read one at a time with App_Uart_RawGet() (0 = empty). Used by the OTA
   data phase; App_Uart_SetRaw(0) restores normal frame parsing.               */
void App_Uart_SetRaw(uint8_t on);
void App_Uart_RawFlush(void);
uint8_t App_Uart_RawGet(void);
uint8_t App_Uart_RawRead(uint8_t *b);
/* Raw TX through the SDK DMA (no frame header, no CRC).                       */
void App_Uart_SendRaw(const uint8_t *p, uint16_t len);

#endif /* TLSR8258_SRC_INCLUDE_APP_UART_H_ */
