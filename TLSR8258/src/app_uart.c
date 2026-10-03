#include "tl_common.h"
#include "app_cfg.h"
#include "app_utility.h"
#include "app_uart.h"

/*********************************************************************************/
/* CA51F2 <-> ZT3L link transport (serial.md v2): 66 BB | SEQ | FLAGS | STATUS | */
/* CMD | DLC | [TYPE] | PAYLOAD(<=64) | CRC-8/MAXIM. Mirrors the CA51F2 parser.  */
/*                                                                               */
/* RX: the SDK drv_uart moves bytes to uart_rx_dma (u32 len + payload) and       */
/* calls uart_rx_cb() from the DMA IRQ. The callback only pushes bytes into a    */
/* ring; frame reassembly runs in App_Uart_Poll() (main context), so several     */
/* frames glued into one DMA chunk are handled too.                              */
/* TX: frame is built in uart_tx and handed to drv_uart_tx_start() (DMA).        */
/*********************************************************************************/
#define APP_UART_RX_DMA_SIZE   132      /* 4 (length header) + 128 payload      */
#define APP_UART_RING_SIZE     256      /* power of two: mask instead of %      */

#define FRM_SYNC1   0
#define FRM_SYNC2   1
#define FRM_HDR     2                   /* SEQ FLAGS STATUS CMD DLC TYPE (6)   */
#define FRM_PAY     3
#define FRM_CRC     4

static __attribute__((aligned(4))) uint8_t uart_rx_dma[APP_UART_RX_DMA_SIZE];
static uint8_t uart_ring[APP_UART_RING_SIZE];
static volatile uint16_t uart_ring_head;    /* written by the RX IRQ            */
static volatile uint16_t uart_ring_tail;    /* read by App_Uart_Poll()          */
static volatile uint32_t uart_rx_overflow;

static uint8_t uart_tx[UART_FRAME_MAX];

static uart_frame_cb_t uart_frame_cb;
static uart_frame_t    uart_frame;      /* reassembly buffer                    */
static volatile uint8_t uart_fsm_st;
static volatile uint8_t uart_fsm_i;
static volatile uint8_t uart_fsm_crc;
static volatile uint16_t uart_rx_epoch;
static uint16_t uart_rx_epoch_seen;

/* Raw mode (OTA data phase): bytes bypass the frame parser and land in a ring.  */
#define APP_UART_RAW_SIZE   256
static uint8_t  uart_raw;
static uint8_t  uart_raw_buf[APP_UART_RAW_SIZE];
static volatile uint16_t uart_raw_head;
static volatile uint16_t uart_raw_tail;
static uint8_t  uart_raw_tx[160];

#if DEBUG_PKT_EN
/* Frame dump on the debug console (app_cfg.h: DEBUG_PKT_EN). Every frame sent
   by the TLSR is printed as "TLSR -> <hex>", every frame accepted from the
   CA51F2 as "CA51 -> <hex>" and a CRC mismatch as "CA51 <! <hex>". Raw (OTA)
   bytes bypass uart_feed(), so the OTA data phase is never logged.          */
#define UART_CAP_MAX   UART_FRAME_MAX
static uint8_t uart_cap[UART_CAP_MAX];      /* frame bytes as seen on the wire */
static uint8_t uart_cap_i;

static void uart_dump(const char *pfx, const uint8_t *b, uint16_t n) {
	uint16_t i;

	APP_DEBUG(DEBUG_PKT_EN, "%s", pfx);
	for (i = 0; i < n; i++) {
		APP_DEBUG(DEBUG_PKT_EN, "%02x ", b[i]);
	}
	APP_DEBUG(DEBUG_PKT_EN, " 0x%08x\r\n", clock_time());
}
#endif

static uint8_t uart_crc8(uint8_t crc, uint8_t b) {
	uint8_t i;

	crc ^= b;
	for (i = 0; i < 8; i++) {
		crc = (crc & 0x01) ? (uint8_t)((crc >> 1) ^ UART_CRC8_REFLECTED)
		                   : (uint8_t)(crc >> 1);
	}
	return crc;
}

static void uart_resync(uint8_t b) {
	uart_fsm_st = FRM_SYNC1;
	uart_fsm_i  = 0;
#if DEBUG_PKT_EN
	uart_cap_i = 0;
#endif
	if(b == UART_FRAME_H0) {
		uart_fsm_crc = uart_crc8(0x00, UART_FRAME_H0);
		uart_fsm_st = FRM_SYNC2;
#if DEBUG_PKT_EN
		uart_cap[uart_cap_i++] = UART_FRAME_H0;   /* b is the next frame's header */
#endif
	}
}

static void uart_feed(uint8_t b) {
#if DEBUG_PKT_EN
	/* Mirror the parser: keep the bytes of the frame being reassembled so a
	   complete line can be printed once the CRC byte arrives.                */
	if (uart_fsm_st == FRM_SYNC1) {
		uart_cap_i = 0;
		if (b == UART_FRAME_H0) uart_cap[uart_cap_i++] = b;
	}
	else if (uart_fsm_st == FRM_SYNC2 && b == UART_FRAME_H0) {
		uart_cap_i = 0;                       /* back-to-back header: restart  */
		uart_cap[uart_cap_i++] = b;
	}
	else if (uart_cap_i < UART_CAP_MAX) {
		uart_cap[uart_cap_i++] = b;
	}
#endif
	switch (uart_fsm_st) {
	case FRM_SYNC1:
		if (b == UART_FRAME_H0) {
			uart_fsm_crc = uart_crc8(0x00, UART_FRAME_H0);
			uart_fsm_st = FRM_SYNC2;
		}
		break;

	case FRM_SYNC2:
		if (b == UART_FRAME_H1) {
			uart_fsm_crc = uart_crc8(uart_fsm_crc, UART_FRAME_H1);
			uart_fsm_i = 0;
			uart_fsm_st = FRM_HDR;
		}
		else if (b == UART_FRAME_H0) {
			uart_fsm_crc = uart_crc8(0x00, UART_FRAME_H0);
			uart_fsm_st = FRM_SYNC2;    /* back-to-back header byte: re-sync */
		}
		else {
			uart_fsm_st = FRM_SYNC1;    /* not the trailer: rescan */
		}
		break;

	case FRM_HDR:
		uart_fsm_crc = uart_crc8(uart_fsm_crc, b);
		if (uart_fsm_i == 0)      uart_frame.seq    = b;
		else if (uart_fsm_i == 1) uart_frame.flags  = b;
		else if (uart_fsm_i == 2) uart_frame.status = b;
		else if (uart_fsm_i == 3) uart_frame.cmd    = b;
		else if (uart_fsm_i == 4) {
			uart_frame.len = b;
			if (b > UART_FRAME_PAYLOAD) {
				uart_resync(b);
				return;
			}
			if (b == 0) {
				/* DLC=0: no Type and no Payload, CRC comes right after */
				uart_fsm_i = 0;
				uart_fsm_st = FRM_CRC;
				break;
			}
		}
		else                    uart_frame.type  = b;
		if (++uart_fsm_i < 6) break;
		uart_fsm_i = 0;
		uart_fsm_st = FRM_PAY;
		break;

	case FRM_PAY:
		uart_fsm_crc = uart_crc8(uart_fsm_crc, b);
		uart_frame.payload[uart_fsm_i++] = b;
		if (uart_fsm_i >= uart_frame.len) {
			uart_fsm_st = FRM_CRC;
		}
		break;

	case FRM_CRC:
		if (b == uart_fsm_crc) {
#if DEBUG_PKT_EN
			uart_dump("CA51 -> ", uart_cap, uart_cap_i);
			uart_cap_i = 0;
#endif
			if (uart_frame_cb) uart_frame_cb(&uart_frame);
			uart_fsm_st = FRM_SYNC1;
		}
		else {
#if DEBUG_PKT_EN
			uart_dump("CA51 <! ", uart_cap, uart_cap_i);
			uart_cap_i = 0;
#endif
			uart_resync(b);
		}
		break;

	default:
		uart_fsm_st = FRM_SYNC1;
		break;
	}
}

/* RX DMA callback (IRQ context): push the received bytes into the ring.        */
static void uart_rx_cb(void) {
	/* uart_rx_dma[0..3] = little-endian received length, payload follows.      */
	uint32_t len = (uint32_t)uart_rx_dma[0]
	             | ((uint32_t)uart_rx_dma[1] << 8)
	             | ((uint32_t)uart_rx_dma[2] << 16)
	             | ((uint32_t)uart_rx_dma[3] << 24);
	uint8_t *p = uart_rx_dma + 4;
	uint16_t i;

	if (len > (APP_UART_RX_DMA_SIZE - 4)) len = APP_UART_RX_DMA_SIZE - 4;
	for (i = 0; i < (uint16_t)len; i++) {
		uint16_t nh = (uint16_t)((uart_ring_head + 1) & (APP_UART_RING_SIZE - 1));
		if (nh == uart_ring_tail) {
			uart_rx_overflow++;
			if(uart_rx_epoch < 0xFFFF) uart_rx_epoch++;
			continue;
		}
		uart_ring[uart_ring_head] = p[i];
		uart_ring_head = nh;
	}
}

void App_Uart_Init(uart_frame_cb_t cb) {
	uart_frame_cb = cb;
	uart_fsm_st  = FRM_SYNC1;
	uart_fsm_i   = 0;
    uart_fsm_crc = 0x00;
    uart_rx_epoch = 0;
    uart_rx_epoch_seen = 0;
	uart_ring_head = 0;
	uart_ring_tail = 0;
	uart_rx_overflow = 0;
#if DEBUG_PKT_EN
	uart_cap_i = 0;
#endif

	drv_uart_pin_set(GPIO_UART_TX, GPIO_UART_RX);
	drv_uart_init(APP_UART_BAUDRATE, uart_rx_dma, APP_UART_RX_DMA_SIZE, uart_rx_cb);
}

void App_Uart_Poll(void) {
    uint32_t irq;
    uint16_t epoch;
    uint8_t b;

    while(1) {
        irq = drv_disable_irq();
        epoch = uart_rx_epoch;
        if(epoch != uart_rx_epoch_seen) {
            uart_fsm_st = FRM_SYNC1;
            uart_fsm_i = 0;
            uart_rx_epoch_seen = epoch;
        }
        if(uart_ring_tail == uart_ring_head) {
            drv_restore_irq(irq);
            break;
        }
        b = uart_ring[uart_ring_tail];
        uart_ring_tail = (uint16_t)((uart_ring_tail + 1) & (APP_UART_RING_SIZE - 1));
        if(epoch != uart_rx_epoch) {
            uart_fsm_st = FRM_SYNC1;
            uart_fsm_i = 0;
            uart_rx_epoch_seen = uart_rx_epoch;
            drv_restore_irq(irq);
            continue;
        }
        drv_restore_irq(irq);
        if(uart_raw) {
            uint16_t nh = (uint16_t)((uart_raw_head + 1) & (APP_UART_RAW_SIZE - 1));
            if(nh != uart_raw_tail) {
                uart_raw_buf[uart_raw_head] = b;
                uart_raw_head = nh;
            }
            else {
                uart_rx_overflow++;
            }
        }
        else {
            uart_feed(b);
        }
    }
}

void App_Uart_SetRaw(uint8_t on) {
	uint8_t next = on ? 1 : 0;

	if (uart_raw == next) return;
	uart_raw = next;
	uart_ring_tail = uart_ring_head;
	uart_raw_head = 0;
	uart_raw_tail = 0;
	uart_fsm_st = FRM_SYNC1;
	uart_fsm_i  = 0;
}

void App_Uart_RawFlush(void) {
	uart_raw_head = 0;
	uart_raw_tail = 0;
}

uint8_t App_Uart_RawRead(uint8_t *b) {
	if (uart_raw_head == uart_raw_tail) return 0;
	*b = uart_raw_buf[uart_raw_tail];
	uart_raw_tail = (uint16_t)((uart_raw_tail + 1) & (APP_UART_RAW_SIZE - 1));
	return 1;
}

/* returns 0 when empty */
uint8_t App_Uart_RawGet(void) {
	uint8_t b;
	return App_Uart_RawRead(&b) ? b : 0;
}

/* Raw TX (bypasses the frame builder); the buffer must stay valid during DMA.  */
void App_Uart_SendRaw(const uint8_t *p, uint16_t len) {
	uint16_t i;
	if (len > sizeof(uart_raw_tx)) len = sizeof(uart_raw_tx);
	for (i = 0; i < len; i++) uart_raw_tx[i] = p[i];
	drv_uart_tx_start(uart_raw_tx, (uint32_t)len);
}

uint8_t App_Uart_SendFrame(uint8_t seq, uint8_t flags, uint8_t status,
                           uint8_t cmd, uint8_t type, uint8_t len,
                           const uint8_t *payload) {
	uint8_t crc;
	uint8_t i;
	uint8_t ok;
	uint8_t *p = uart_tx;

	if (uart_raw) return 0;

	if (len > UART_FRAME_PAYLOAD) len = UART_FRAME_PAYLOAD;

	*p++ = UART_FRAME_H0; crc = uart_crc8(0x00, UART_FRAME_H0);
	*p++ = UART_FRAME_H1; crc = uart_crc8(crc, UART_FRAME_H1);
	*p++ = seq;           crc = uart_crc8(crc, seq);
	*p++ = flags;         crc = uart_crc8(crc, flags);
	*p++ = status;        crc = uart_crc8(crc, status);
	*p++ = cmd;           crc = uart_crc8(crc, cmd);
	*p++ = len;           crc = uart_crc8(crc, len);
	if (len) {
		*p++ = type;      crc = uart_crc8(crc, type);
	}
	for (i = 0; i < len; i++) {
		*p++ = payload[i];
		crc = uart_crc8(crc, payload[i]);
	}
	*p++ = crc;

#if DEBUG_PKT_EN
	uart_dump("TLSR -> ", uart_tx, (uint16_t)(p - uart_tx));
#endif
	ok = drv_uart_tx_start(uart_tx, (uint32_t)(p - uart_tx));
#if DEBUG_PKT_EN
	/* "TLSR !!" = the driver refused the frame (buffer pool / raw mode): the
	   line above is what we WANTED on the wire, this one means it is not.   */
	if(!ok) uart_dump("TLSR !! ", uart_tx, (uint16_t)(p - uart_tx));
#endif
	return ok;
}
