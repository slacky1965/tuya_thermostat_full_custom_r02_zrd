/* Define this TU as THE owner of the UART buffer bodies: uart.h emits the
   uart0/1/2 ring buffers and flags only when _UART_C_ is set, every other
   TU sees externs (this used to come from the file's include guard, which
   was removed 2026-10-02 at the owner's request - the switch stays). */
#define _UART_C_
#include "include/stdint.h"
#include "include/config.h"		
#include "include/ca51f2sfr.h"
#include "include/ca51f2xsfr.h"
#include "include/gpiodef_f2.h"
#include "include/system_clock.h"

#include "include/uart.h"
#include "include/debug.h"
#include "include/mdu.h"
#include <intrins.h>
#include <absacc.h>
/* the vendor uart_printf() below (PRINT_EN) is the only user of these;   */
/* the clang (mcs51-llvm) build is freestanding and ships none of them    */
#if defined(PRINT_EN)
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#endif
/**************************************************************************/
/**************************************************************************/
#ifdef UART0_EN
void Uart0_Initial(uint32_t baudrate) {
	uint16_t value_temp;

	P31F = P31_UART0_RX_SETTING;
	P30F = P30_UART0_TX_SETTING;
	
	uart0_send.head=0;
	uart0_send.tail=0;
	uart0_rev.head=0;
	uart0_rev.tail=0;
	uart0_tx_flag=0;

/**************************************************************************/
//Timer2 as UART0 baudrate generator
	{
		/* MDU temporaries: static xdata storage for the xdata-pointer API  */
		/* (clang puts automatics on the hardware stack).                   */
		static uint32_t xdata q;
		static uint32_t xdata r;
		if(!Mdu_DivMod32((uint32_t)FOSC, (uint32_t)baudrate * 32U,
		                  &q, &r)) {
			value_temp = UART0_RELOAD_FALLBACK;
		}
		else {
			value_temp = (uint16_t)(0x10000 - q);
		}
	}
	T2CON = 	0x24;
	T2CH  = 	(uint8_t)(value_temp>>8);
	T2CL  = 	(uint8_t)(value_temp);
	TH2   = 	(uint8_t)(value_temp>>8);
	TL2   = 	(uint8_t)(value_temp);;	
	TR2   = 	1;
/****************************************************************************/


/****************************************************************************/
//Timer1 as UART0 baudrate generator

//  TMOD = (TMOD&0xCF)|0x20;
//	TH1 = 0xff;		//19200
//	TL1 = 0xff;
//	ET1=0;
//	TR1=1;
//	PCON |= 0x80;
/****************************************************************************/


	S0CON = 0x50;	 
	ES0 = 1;
}
void Uart0_PutChar(uint8_t bdat) {
	uint8_t free_space;
	uint8_t tail_tmp;
	while(1) {				
		tail_tmp = uart0_send.tail;	
		if(uart0_send.head < tail_tmp) {
			free_space = tail_tmp - uart0_send.head;
		}
		else {
			free_space = UART0_TX_BUF_SIZE + tail_tmp - uart0_send.head;
		}		
		if(free_space > 1) {
			ES0 = 0; 
			uart0_send.head++;
			uart0_send.head %= UART0_TX_BUF_SIZE;
			uart0_tx_buf[uart0_send.head] = bdat;			
			if(!uart0_tx_flag) {
				ES0 = 1;				
				uart0_send.tail++;
				uart0_send.tail %= UART0_TX_BUF_SIZE;		
				S0BUF=uart0_tx_buf[uart0_send.tail];				
				uart0_tx_flag = 1;		
			}
			else {
				ES0 = 1;	
			}			
			break;
		}
	}
}
void UART0_ISR (void) __interrupt (4) {	
	if(RI0) {
		RI0 = 0;
		uart0_rev.head++;
		uart0_rev.head %= UART0_RX_BUF_SIZE;
		uart0_rx_buf[uart0_rev.head]=S0BUF;
	}
	if(TI0) {	
		TI0 = 0;		
		if(uart0_send.head!=uart0_send.tail) {
			uart0_send.tail++;
			uart0_send.tail %= UART0_TX_BUF_SIZE;
			S0BUF=uart0_tx_buf[uart0_send.tail];				
		}
		else {
			uart0_tx_flag=0;
		}	
	}
}
#endif
#ifdef UART1_EN
/********************************************************************************/
/* CA51F2 <-> ZT3L link framing (serial.md v2): 66 BB | SEQ | FLAGS | STATUS |  */
/* CMD | DLC | [TYPE] | PAYLOAD(<=64) | CRC. Type is present only when          */
/* DLC>0 (no payload = no type; CRC comes right after DLC).                     */
/* CRC-8/MAXIM (poly 0x31, reflected 0x8C, init 0x00, xorout 0) covers          */
/* Header..Payload. Parser resyncs on CRC mismatch / oversized DLC.             */
/********************************************************************************/
#define FRM_SYNC1   0
#define FRM_SYNC2   1
#define FRM_HDR     2               /* SEQ FLAGS STATUS CMD DLC TYPE (6 bytes) */
#define FRM_PAY     3
#define FRM_CRC     4

static uart_frame_cb_t  uart_frame_cb;
static uart_frame_t  xdata uart_frame; /* parser reassembly buffer (payload 64B */
static volatile uint8_t uart_fsm_st;
static volatile uint8_t uart_fsm_i;
static volatile uint8_t uart_fsm_crc;
static volatile uint16_t uart_rx_epoch;
static uint16_t uart_rx_epoch_seen;
static volatile uint16_t xdata uart1_rx_overflow;

/*********************************************************************************/
/* UART link trace (debug): dump every RX/TX frame as raw hex, one line each.    */
/*   L> 66 BB .. CRC    sent by us                                               */
/*   L< 66 BB .. CRC    received and accepted                                    */
/*   L< 66 BB .. CRC !  received but rejected (bad CRC / oversized DLC)          */
/* The bytes are printed AS THEY ARRIVE (streaming) instead of being copied into */
/* a uart_raw[] image of the whole frame: that copy alone cost 73 B of XRAM,     */
/* which a debug build cannot spare, and the reject marker therefore moved from  */
/* the line prefix to its end - at the first byte the CRC is not known yet.      */
/* Errors are always receive-side: TX is fire-and-forget into the ring.          */
/* Gated by LINK_EN (debug.h). Parsing runs in the main loop, never in the RX    */
/* ISR (the ISR only fills the ring), so these blocking prints are safe.         */
/*********************************************************************************/
#if UART_DEBUG && LINK_EN
static uint8_t uart_raw_open;      /* a line is open: 1 -> suffix still owed   */

static void link_raw_reset(void) {
	if(uart_raw_open) debug_puts("\r\n");   /* aborted frame: close its line  */
	uart_raw_open = 1;
	debug_puts("L<");
}

static void link_raw_add(uint8_t b) {
	debug_putc(' ');
	debug_hex((uint16_t)b, 2);
}

static void link_raw_dump(uint8_t bad) {
	/* No "is a line open?" guard: every caller follows a header byte, and the
	   flag is only read by link_raw_reset() to close a line cut short by a
	   mid-frame resync.                                                      */
	debug_puts(bad ? "!\r\n" : "\r\n");
	uart_raw_open = 0;
}

#else
#define link_raw_reset()
#define link_raw_add(b)
#define link_raw_dump(bad)
#endif

static uint8_t uart_crc8(uint8_t crc, uint8_t b) {
	uint8_t i;

	crc ^= b;
	for(i = 0; i < 8; i++) {
		crc = (crc & 0x01) ? (uint8_t)((crc >> 1) ^ 0x8C) : (uint8_t)(crc >> 1);
	}
	return crc;
}

static void uart_resync(uint8_t b) {
	uart_fsm_st = FRM_SYNC1;
	uart_fsm_i  = 0;
	if(b == UART_FRAME_H0) {
		link_raw_reset();
		link_raw_add(b);
		uart_fsm_crc = uart_crc8(0x00, UART_FRAME_H0);
		uart_fsm_st = FRM_SYNC2;
	}
}

static void uart_feed(uint8_t b) {
	switch(uart_fsm_st) {
	case FRM_SYNC1:
		if(b == UART_FRAME_H0) {
			link_raw_reset();
			link_raw_add(b);
			uart_fsm_crc = uart_crc8(0x00, UART_FRAME_H0);
			uart_fsm_st = FRM_SYNC2;
		}
		break;

	case FRM_SYNC2:
		if(b == UART_FRAME_H1) {
			link_raw_add(b);
			uart_fsm_crc = uart_crc8(uart_fsm_crc, UART_FRAME_H1);
			uart_fsm_i = 0;
			uart_fsm_st = FRM_HDR;
		}
		else if(b == UART_FRAME_H0) {
			link_raw_reset();
			link_raw_add(b);
			uart_fsm_crc = uart_crc8(0x00, UART_FRAME_H0);
			uart_fsm_st = FRM_SYNC2;    /* back-to-back header byte: re-sync */
		}
		else {
			link_raw_reset();
			uart_fsm_st = FRM_SYNC1;    /* not the trailer: rescan */
		}
		break;

	case FRM_HDR:
		link_raw_add(b);
		uart_fsm_crc = uart_crc8(uart_fsm_crc, b);
		if(uart_fsm_i == 0)      uart_frame.seq    = b;
		else if(uart_fsm_i == 1) uart_frame.flags  = b;
		else if(uart_fsm_i == 2) uart_frame.status = b;
		else if(uart_fsm_i == 3) uart_frame.cmd    = b;
		else if(uart_fsm_i == 4) {
			uart_frame.len = b;
			if(b > UART_FRAME_PAYLOAD) {
				link_raw_dump(1);       /* oversized DLC: drop */
				uart_resync(b);
				return;
			}
			if(b == 0) {
				/* DLC=0: no Type and no Payload, CRC comes right after */
				uart_fsm_i = 0;
				uart_fsm_st = FRM_CRC;
				break;
			}
		}
		else                    uart_frame.type  = b;
		if(++uart_fsm_i < 6) break;
		uart_fsm_i = 0;
		uart_fsm_st = FRM_PAY;
		break;

	case FRM_PAY:
		link_raw_add(b);
		uart_fsm_crc = uart_crc8(uart_fsm_crc, b);
		uart_frame.payload[uart_fsm_i++] = b;
		if(uart_fsm_i >= uart_frame.len) {
			uart_fsm_st = FRM_CRC;
		}
		break;

	case FRM_CRC:
		link_raw_add(b);
		if(b == uart_fsm_crc) {
			link_raw_dump(0);
			if(uart_frame_cb) uart_frame_cb(&uart_frame);
		}
		else {
			link_raw_dump(1);           /* CRC mismatch: show the frame */
			uart_resync(b);
			return;
		}
		uart_fsm_st = FRM_SYNC1;
		break;

	default:
		uart_fsm_st = FRM_SYNC1;
		break;
	}
}

void Uart1_Protocol_Init(uart_frame_cb_t cb) {
	uart_frame_cb = cb;
	uart_fsm_st = FRM_SYNC1;
	uart_rx_epoch = 0;
}

void Uart1_Poll(void) {
	uint16_t epoch;
	uint8_t b;

	while(1) {
		ES1 = 0;
		epoch = uart_rx_epoch;
		if(epoch != uart_rx_epoch_seen) {
			uart_rx_epoch_seen = epoch;
			goto reset_rx;
		}
		if(uart1_rev.tail == uart1_rev.head) {
			ES1 = 1;
			break;
		}
		uart1_rev.tail++;
		uart1_rev.tail %= UART1_RX_BUF_SIZE;
		b = uart1_rx_buf[uart1_rev.tail];
		if(epoch != uart_rx_epoch) {
			uart_rx_epoch_seen = uart_rx_epoch;
			goto reset_rx;
		}
		ES1 = 1;
		uart_feed(b);
		continue;

reset_rx:
		uart_fsm_st = FRM_SYNC1;
		link_raw_reset();
		ES1 = 1;
	}
}

void Uart1_SendFrame(uint8_t seq, uint8_t flags, uint8_t status,
                     uint8_t cmd, uint8_t type, uint8_t len,
                     const uint8_t xdata *payload) {
	static uint8_t xdata tx[UART_FRAME_MAX];
	uint8_t crc;
	uint8_t i;
	uint8_t n = 0;

	if(len > UART_FRAME_PAYLOAD) len = UART_FRAME_PAYLOAD;

	tx[n++] = UART_FRAME_H0;
	tx[n++] = UART_FRAME_H1;
	tx[n++] = seq;
	tx[n++] = flags;
	tx[n++] = status;
	tx[n++] = cmd;
	tx[n++] = len;
	if(len) tx[n++] = type;
	for(i = 0; i < len; i++) tx[n++] = payload[i];

	crc = 0x00;
	for(i = 0; i < n; i++) crc = uart_crc8(crc, tx[i]);
	tx[n++] = crc;

	/* Queue the WHOLE frame first, so the TX ISR sends the bytes back-to-back. */
	/* The debug log is blocking (polled UART0) and is emitted afterwards - if  */
	/* it were interleaved per byte it would open gaps on the wire.             */
	for(i = 0; i < n; i++) Uart1_PutChar(tx[i]);

	DEBUG(LINK_EN, {
		debug_puts("L>");
		for(i = 0; i < n; i++) {
			debug_putc(' ');
			debug_hex((uint16_t)tx[i], 2);
		}
		debug_puts("\r\n"); });
}
/********************************************************************************/
void Uart1_Initial(void) {
	uint16_t value_temp;

	uart1_send.head=0;
	uart1_send.tail=0;
	uart1_rev.head=0;
	uart1_rev.tail=0;
	uart1_tx_flag=0;
	uart1_rx_overflow=0;

	/* FOSC and UART1_BAUTRATE are compile-time constants, so this folds to
	   a constant reload (no 32-bit divide / MDU call at runtime).          */
	value_temp = UART1_RELOAD_VALUE;


	GPIO_Init(P67F,P67_UART1_RX_SETTING);
	GPIO_Init(P66F,P66_UART1_TX_SETTING);


	S1RELH = (uint8_t)(value_temp>>8);
	S1RELL = (uint8_t)(value_temp);
	
	S1CON = 0xD0;
	ES1 =	1;	
}
void Uart1_PutChar(uint8_t bdat) {
	uint8_t free_space;
	uint8_t tail_tmp;
	while(1) {		
		tail_tmp = uart1_send.tail;
		if(uart1_send.head < tail_tmp) {
			free_space = tail_tmp - uart1_send.head;
		}
		else {
			free_space = UART1_TX_BUF_SIZE + tail_tmp - uart1_send.head;
		}		
		if(free_space > 1) {
			ES1 = 0; 
			uart1_send.head++;
			uart1_send.head %= UART1_TX_BUF_SIZE;
			uart1_tx_buf[uart1_send.head] = bdat;
			if(!uart1_tx_flag) {
				ES1 = 1;
				uart1_send.tail++;
				uart1_send.tail %= UART1_TX_BUF_SIZE;		
				S1BUF = uart1_tx_buf[uart1_send.tail];				
				uart1_tx_flag = 1;		
			}
			else {
				ES1 = 1;	
			}			
			break;
		}
	}
}
uint8_t Uart1_TxIdle(void) {
	return (uint8_t)((uart1_send.head == uart1_send.tail) && !uart1_tx_flag);
}
void UART1_ISR (void) __interrupt (6) {
	if(S1CON & 0x01) {
		uint8_t next;

		S1CON = (S1CON&0xFC)|0x01;
		next = (uint8_t)(uart1_rev.head + 1);
		if(next >= UART1_RX_BUF_SIZE) next = 0;
		if(next == uart1_rev.tail) {
			(void)S1BUF;
			if(uart1_rx_overflow < 0xFFFF) uart1_rx_overflow++;
			if(uart_rx_epoch < 0xFFFF) uart_rx_epoch++;
		}
		else {
			uart1_rev.head = next;
			uart1_rx_buf[uart1_rev.head] = S1BUF;
		}
	}
	if(S1CON & 0x02) {
		S1CON = (S1CON&0xFC)|0x02;			
		if(uart1_send.head!=uart1_send.tail) {
			uart1_send.tail++;
			uart1_send.tail %= UART1_TX_BUF_SIZE;
			S1BUF=uart1_tx_buf[uart1_send.tail];				
		}
		else {
			uart1_tx_flag=0;
		}		
	}
}
#endif
#ifdef UART2_EN
void Uart2_Initial(uint32_t baudrate) {
	uint16_t value_temp;

	uart2_send.head=0;
	uart2_send.tail=0;
	uart2_rev.head=0;
	uart2_rev.tail=0;
	uart2_tx_flag=0;

	GPIO_Init(P60F,P60_UART2_RX_SETTING);
	GPIO_Init(P61F,P61_UART2_TX_SETTING);

	{
		/* MDU temporaries: static xdata storage for the xdata-pointer API  */
		/* (clang puts automatics on the hardware stack).                   */
		static uint32_t xdata q;
		static uint32_t xdata r;
		if(!Mdu_DivMod32((uint32_t)FOSC, (uint32_t)baudrate * 32U,
		                  &q, &r)) {
			value_temp = UART2_RELOAD_FALLBACK;
		}
		else {
			value_temp = (uint16_t)(0x400 - q);
		}
	}
	S2RELH = (uint8_t)(value_temp>>8);
	S2RELL = (uint8_t)(value_temp);
	
	S2CON = 0xD0;
	INT3EN =	1;	
}
void Uart2_PutChar(uint8_t bdat) {
	uint8_t free_space;
	uint8_t tail_tmp;
	while(1) {		
		tail_tmp = uart2_send.tail;
		if(uart2_send.head < tail_tmp) {
			free_space = tail_tmp - uart2_send.head;
		}
		else {
			free_space = UART2_TX_BUF_SIZE + tail_tmp - uart2_send.head;
		}		
		if(free_space > 1) {
			INT3EN = 0; 
			uart2_send.head++;
			uart2_send.head %= UART2_TX_BUF_SIZE;
			uart2_tx_buf[uart2_send.head] = bdat;			
			if(!uart2_tx_flag) {
				INT3EN = 1;
				uart2_send.tail++;
				uart2_send.tail %= UART2_TX_BUF_SIZE;			
				S2BUF = uart2_tx_buf[uart2_send.tail];				
				uart2_tx_flag = 1;		
			}
			else {
				INT3EN = 1;	
			}			
			break;
		}
	}
}
void UART2_ISR (void) __interrupt (8) {
	if(S2CON & 0x01) {
		S2CON = (S2CON&0xFC)|0x01;		
		uart2_rev.head++;
		uart2_rev.head %= UART2_RX_BUF_SIZE;
		uart2_rx_buf[uart2_rev.head]=S2BUF;
	}
	if(S2CON & 0x02) {
		S2CON = (S2CON&0xFC)|0x02;	
		if(uart2_send.head!=uart2_send.tail) {
			uart2_send.tail++;
			uart2_send.tail %= UART2_TX_BUF_SIZE;
			S2BUF=uart2_tx_buf[uart2_send.tail];				
		}
		else {
			uart2_tx_flag=0;
		}		
	}
}
#endif
#ifdef PRINT_EN
	#ifdef UART0_PRINT
		#define Uart_PutChar	Uart0_PutChar
	#elif defined  UART1_PRINT
		#define Uart_PutChar	Uart1_PutChar
	#elif defined  UART2_PRINT
		#define Uart_PutChar	Uart2_PutChar
	#endif
void UartPutStr(char *str) {
	while(*str) {	
 		Uart_PutChar(*str++);
	}
}
void uart_printf(char *fmt,...)  {
    va_list ap;
    char xdata string[256];
    va_start(ap,fmt);
    vsprintf(string,fmt,ap);
    UartPutStr(string);
    va_end(ap);
}
#endif
