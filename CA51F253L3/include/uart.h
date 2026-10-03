#ifndef _UART_H_
#define _UART_H_
#include "include/stdint.h"

/* Ring buffer sizes come from config.h (UART0/1/2_TX/RX_BUF_SIZE); include   */
/* config.h before uart.h.                                                    */
typedef struct {
	uint8_t head;
	uint8_t tail;
}T_Buf_Info;


#ifdef _UART_C_
#ifdef UART0_EN
	T_Buf_Info xdata	uart0_send;
	T_Buf_Info xdata	uart0_rev;
	uint8_t xdata uart0_tx_buf[UART0_TX_BUF_SIZE];
	uint8_t xdata uart0_rx_buf[UART0_RX_BUF_SIZE];
	bit uart0_tx_flag;
#endif
#ifdef UART1_EN
	T_Buf_Info xdata	uart1_send;
	T_Buf_Info xdata	uart1_rev;
	uint8_t xdata uart1_tx_buf[UART1_TX_BUF_SIZE];
	uint8_t xdata uart1_rx_buf[UART1_RX_BUF_SIZE];
	bit uart1_tx_flag;
#endif
#ifdef UART2_EN
	T_Buf_Info xdata	uart2_send;
	T_Buf_Info xdata	uart2_rev;
	uint8_t xdata uart2_tx_buf[UART2_TX_BUF_SIZE];
	uint8_t xdata uart2_rx_buf[UART2_RX_BUF_SIZE];
	bit uart2_tx_flag;
#endif
#else
#ifdef UART0_EN
	extern T_Buf_Info xdata	uart0_send;
	extern T_Buf_Info xdata	uart0_rev;
	extern uint8_t xdata uart0_tx_buf[UART0_TX_BUF_SIZE];
	extern uint8_t xdata uart0_rx_buf[UART0_RX_BUF_SIZE];
	extern bit uart0_tx_flag;	
#endif	
#ifdef UART1_EN
	extern T_Buf_Info xdata	uart1_send;
	extern T_Buf_Info xdata	uart1_rev;
	extern uint8_t xdata uart1_tx_buf[UART1_TX_BUF_SIZE];
	extern uint8_t xdata uart1_rx_buf[UART1_RX_BUF_SIZE];
	extern bit uart1_tx_flag;	
#endif
#ifdef UART2_EN
	extern T_Buf_Info xdata	uart2_send;
	extern T_Buf_Info xdata	uart2_rev;
	extern uint8_t xdata uart2_tx_buf[UART2_TX_BUF_SIZE];
	extern uint8_t xdata uart2_rx_buf[UART2_RX_BUF_SIZE];
	extern bit uart2_tx_flag;	
#endif
#endif
#ifndef UART0_EN
	  #define Uart0_PutChar(n)
#endif
#ifndef UART1_EN
	  #define Uart1_PutChar(n)
#endif
#ifndef UART2_EN
	  #define Uart2_PutChar(n)
#endif
#ifdef UART0_EN
void Uart0_PutChar(uint8_t bdat);
void Uart0_Initial(uint32_t baudrate);
#endif	

#ifdef UART1_EN
void Uart1_PutChar(uint8_t bdat);
void Uart1_Initial(void);
/* SDCC only builds the vector table from ISR prototypes visible in the file   */
/* with main(), so this declaration must stay in a header main.c includes.     */
void UART1_ISR (void) __interrupt (6);
/*******************************************************************************/
/* CA51F2 <-> ZT3L link protocol (serial.md v2, header 0x66 0xBB).             */
/* Frame-level constants (header, flags, payload size, CRC poly) are SHARED    */
/* with the ZT3L/TLSR8258 side in include_common/link_frame.h.                 */
/*******************************************************************************/
#include "../include_common/link_frame.h"

/* Frame as received / to be sent (big-endian multi-byte numbers, CRC-8/MAXIM
   over Header..Payload incl., init 0x00). Payload points at a static parser
   buffer - the callback must consume it synchronously.                        */
typedef struct {
	uint8_t seq;            /* increment by the initiator, echoed in replies   */
	uint8_t flags;          /* UART_F_RSP / UART_F_ACK / UART_F_CMD, rest = 0  */
	uint8_t status;         /* replies only: 0 = OK, != 0 = error code         */
	uint8_t cmd;            /* command/data ID (ranges reserved, serial.md)    */
	uint8_t type;           /* 0=bool 1=enum 2=u8 3=i8 4=u16 5=i16 6=u32       */
	                        /* 7=i32 8=float 10=string 11=RAW; only meaningful */
	                        /* when len>0 (absent from the wire when len==0)   */
	uint8_t len;            /* Payload length, 0..UART_FRAME_PAYLOAD           */
	uint8_t payload[UART_FRAME_PAYLOAD];
} uart_frame_t;

typedef void (*uart_frame_cb_t)(const uart_frame_t xdata *f);

void Uart1_Protocol_Init(uart_frame_cb_t cb);   /* cb==NULL: frames discarded                     */
void Uart1_Poll(void);                          /* feed parser from the UART1 RX ring buffer      */
void Uart1_SendFrame(uint8_t seq, uint8_t flags, uint8_t status,
                     uint8_t cmd, uint8_t type, uint8_t len,
                     const uint8_t xdata *payload);
/* 1 when the UART1 TX ring is empty and no byte is in flight. */
uint8_t Uart1_TxIdle(void);
#endif	
#ifdef UART2_EN
void Uart2_PutChar(uint8_t bdat);
void Uart2_Initial(uint32_t baudrate);
#endif	

#ifdef PRINT_EN
void uart_printf(char *fmt,...);
#endif

#endif /* _UART_H_ */
