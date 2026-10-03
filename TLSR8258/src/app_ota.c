#include "app_main.h"
#include "app_ota.h"
#include "ca51f2_ota.h"
#include "link_proto.h"
#include "utility.h"

/*******************************************************************************/
enum {
	ST_START,
	ST_WAIT_RSP,
	ST_WAIT_READY,
	ST_SEND_BLOCK,
	ST_WAIT_ACK,
	ST_WAIT_DONE,
	ST_WAIT_DONE_ACK,
	ST_WAIT_BOOT,
	ST_ERROR
};

#define OTA_IMG_ADDR       0x77000UL
#define OTA_START_MS       200
#define OTA_READY_MS       500
#define OTA_ACK_MS         LNK_OTA_TLSR_ACK_TIMEOUT_MS
#define OTA_DONE_MS        1000
#define OTA_BOOT_RETRY_MS  5000
#define OTA_START_TRIES    3
#define OTA_READY_TRIES    5
#define OTA_BLOCK_TRIES    5
#define OTA_FULL_TRIES     3
#define OTA_DONE_TRIES     3
#define OTA_BOOT_TRIES     6

#define OTA_TOKEN_NONE     0
#define OTA_TOKEN_MATCH    1
#define OTA_TOKEN_DONE     2

static uint32_t ota_bin_size;
static uint16_t ota_img_ver;
static uint8_t ota_active;
static uint8_t ota_raw_mode;
static uint8_t ota_start_accepted;
static uint8_t ota_start_seq;
static uint8_t st;
static uint8_t start_tries;
static uint8_t ready_tries;
static uint8_t blk_tries;
static uint8_t full_tries;
static uint8_t done_tries;
static uint8_t boot_tries;
static uint32_t blk_off;
static uint32_t last_blk_off;
static uint8_t blk_len;
static uint16_t ack_off;
static uint8_t ota_rsp_buf[LNK_OTA_RSP_LEN];
static uint8_t ota_rsp_len;
static uint32_t wait_t0;

static uint8_t ota_tx[LNK_OTA_BLOCK_MAX + 6];
static const uint8_t ota_restart = LNK_OTA_RESTART;

static uint32_t ota_u32(const uint8_t *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint8_t ota_crc8(const uint8_t *p, uint8_t n) {
	uint8_t c = 0x00;
	uint8_t i;

	while (n--) {
		c ^= *p++;
		for (i = 0; i < 8; i++) {
			c = (c & 0x01) ? (uint8_t)((c >> 1) ^ UART_CRC8_REFLECTED)
			               : (uint8_t)(c >> 1);
		}
	}
	return c;
}

static uint8_t ota_timeout(uint32_t ms) {
	return (uint32_t)(clock_time() - wait_t0) >= (ms * CLOCK_16M_SYS_TIMER_CLK_1MS);
}

static void ota_send_block_at(uint32_t off) {
	uint32_t left = ota_bin_size - off;

	blk_len = (left > LNK_OTA_BLOCK_MAX) ? LNK_OTA_BLOCK_MAX : (uint8_t)left;
	ota_tx[0] = LNK_OTA_SYNC;
	ota_tx[1] = (uint8_t)(off >> 8);
	ota_tx[2] = (uint8_t)off;
	ota_tx[3] = blk_len;
	flash_read(OTA_IMG_ADDR + CA51F2_OTA_HDR_SIZE + off, blk_len, ota_tx + 4);
	ota_tx[4 + blk_len] = ota_crc8(ota_tx + 1, (uint8_t)(3 + blk_len));
	ack_off = (uint16_t)off;
	ota_rsp_len = 0;
	App_Uart_SendRaw(ota_tx, (uint16_t)(4 + blk_len + 1));
}

static void ota_enter_raw(void) {
	App_Uart_SetRaw(1);
	ota_raw_mode = 1;
}

static void ota_start_optimistic_raw(void) {
	ota_enter_raw();
	ready_tries = 0;
	ota_rsp_len = 0;
	wait_t0 = clock_time();
	st = ST_WAIT_READY;
}

static void ota_block_failure(void) {
	if (blk_tries < OTA_BLOCK_TRIES) {
		st = ST_SEND_BLOCK;
		return;
	}
	if (full_tries + 1 < OTA_FULL_TRIES) {
		full_tries++;
		App_Uart_SendRaw(&ota_restart, 1);
		blk_off = 0;
		last_blk_off = 0;
		blk_tries = 0;
		ready_tries = 0;
		ota_rsp_len = 0;
		wait_t0 = clock_time();
		st = ST_WAIT_READY;
		return;
	}
	/* Terminal: release the link layer exactly like every other ST_ERROR      */
	/* site - with raw mode on and ota_active = 1 the frame TX/RX stay dead    */
	/* until a physical reboot, even though the CA51F2 may be healthy.         */
	st = ST_ERROR;
	App_Uart_SetRaw(0);
	ota_raw_mode = 0;
	ota_active = 0;
}

static uint8_t ota_take_token(uint16_t expected, uint8_t *status) {
	uint8_t b;
	uint16_t off;
	uint8_t first;

	while (ota_rsp_len < LNK_OTA_RSP_LEN) {
		if (!App_Uart_RawRead(&b)) return OTA_TOKEN_NONE;
		if (ota_rsp_len == 0) {
			if (b == LNK_OTA_DONE) return OTA_TOKEN_DONE;
			if (b != LNK_OTA_ACK && b != LNK_OTA_NAK) continue;
		}
		ota_rsp_buf[ota_rsp_len++] = b;
	}
	first = ota_rsp_buf[0];
	off = (uint16_t)(((uint16_t)ota_rsp_buf[1] << 8) | ota_rsp_buf[2]);
	ota_rsp_len = 0;
	if (off != expected) return OTA_TOKEN_NONE;
	*status = first;
	return OTA_TOKEN_MATCH;
}

static void ota_done_failure(void) {
	if (done_tries < OTA_DONE_TRIES) {
		done_tries++;
		ota_send_block_at(last_blk_off);
		blk_tries = 1;
		wait_t0 = clock_time();
		st = ST_WAIT_DONE_ACK;
		return;
	}
	/* Terminal: the CA51F2 has usually flashed everything and booted the new  */
	/* image by now (it sends DONE and jumps to 0 after the success hold) -    */
	/* release the link so the fresh handshake can complete.                   */
	st = ST_ERROR;
	App_Uart_SetRaw(0);
	ota_raw_mode = 0;
	ota_active = 0;
}

void App_Ota_HandleImage(void) {
	uint8_t hdr[CA51F2_OTA_HDR_SIZE];
	uint32_t crc_hdr;
	uint32_t crc;
	uint32_t left;
	uint32_t addr;
	uint8_t buf[128];

	if (ota_active) return;
	flash_read(OTA_IMG_ADDR, CA51F2_OTA_HDR_SIZE, hdr);
	if (!(hdr[0] == 'C' && hdr[1] == 'A' && hdr[2] == '5' &&
	      hdr[3] == '1' && hdr[4] == 'F' && hdr[5] == '2')) {
		APP_DEBUG(DEBUG_OTA_EN, "OTA: not a CA51F2 image, rebooting\r\n");
		ota_mcuReboot();
		return;
	}

	ota_bin_size = ota_u32(hdr + CA51F2_OTA_OFF_SIZE);
	ota_img_ver  = (uint16_t)(hdr[CA51F2_OTA_OFF_VERSION] |
	                          (hdr[CA51F2_OTA_OFF_VERSION + 1] << 8));
	APP_DEBUG(DEBUG_OTA_EN, "OTA: hdr %02x %02x %02x %02x %02x %02x ver=%x size=%d ca51=%d v%x\r\n",
	          hdr[0], hdr[1], hdr[2], hdr[3], hdr[4], hdr[5],
	          ota_img_ver, (int)ota_bin_size,
	          (int)App_Link_Ca51f2Alive(), App_Link_Ca51f2Ver());

	if (!App_Link_Ca51f2Alive()) {
		APP_DEBUG(DEBUG_OTA_EN, "OTA: CA51F2 not present, skipping\r\n");
		return;
	}
	if (ota_img_ver == 0) {
		APP_DEBUG(DEBUG_OTA_EN, "OTA: bad image version\r\n");
		return;
	}
	if (ota_img_ver == App_Link_Ca51f2Ver()) {
		APP_DEBUG(DEBUG_OTA_EN, "OTA: CA51F2 already v%x, skipping\r\n", ota_img_ver);
		return;
	}
	if (ota_bin_size < LNK_OTA_BLOCK_MAX || ota_bin_size > LNK_OTA_PROGRAM_MAX) {
		APP_DEBUG(DEBUG_OTA_EN, "OTA: bad image size\r\n");
		return;
	}

	crc_hdr = ota_u32(hdr + CA51F2_OTA_OFF_CRC32);
	crc = 0xFFFFFFFFUL;
	left = ota_bin_size;
	addr = OTA_IMG_ADDR + CA51F2_OTA_HDR_SIZE;
	while (left) {
		uint32_t n = (left > sizeof(buf)) ? sizeof(buf) : left;
		flash_read(addr, n, buf);
		crc = xcrc32(buf, n, crc);
		addr += n;
		left -= n;
	}
	if (crc != crc_hdr) {
		APP_DEBUG(DEBUG_OTA_EN, "OTA: CRC mismatch, skipping\r\n");
		return;
	}

	ota_start_seq = App_Link_AllocSeq();
	ota_start_accepted = 0;
	ota_raw_mode = 0;
	start_tries = 0;
	ready_tries = 0;
	blk_tries = 0;
	full_tries = 0;
	done_tries = 0;
	blk_off = 0;
	last_blk_off = 0;
	ack_off = 0;
	ota_rsp_len = 0;
	ota_active = 1;
	st = ST_START;
	App_Link_OtaRspClear();
	APP_DEBUG(DEBUG_OTA_EN, "OTA: CA51F2 image v%x size %d\r\n",
	          ota_img_ver, (int)ota_bin_size);
}

void App_Ota_Poll(void) {
	uint8_t b;
	uint8_t s;
	uint8_t token;
	uint8_t status;

	if (!ota_active) return;

	switch (st) {
	case ST_START:
		if (App_Link_SendOtaStartWithSeq(ota_start_seq, ota_bin_size, ota_img_ver)) {
			start_tries++;
			wait_t0 = clock_time();
			st = ST_WAIT_RSP;
		}
		break;

	case ST_WAIT_RSP:
		s = App_Link_OtaRspStatus();
		if (s == LNK_ST_OK) {
			ota_start_accepted = 1;
			ota_start_optimistic_raw();
		}
		else if (s == LNK_ST_BUSY) {
			if (!ota_start_accepted) {
				st = ST_ERROR;
				App_Uart_SetRaw(0);
				ota_raw_mode = 0;
				ota_active = 0;
			}
			else if (start_tries < OTA_START_TRIES) {
				if (App_Link_SendOtaStartWithSeq(ota_start_seq, ota_bin_size, ota_img_ver)) {
					start_tries++;
					wait_t0 = clock_time();
				}
			}
			else {
				ota_start_optimistic_raw();
			}
		}
		else if (s != 0xFF) {
			st = ST_ERROR;
			if (!ota_raw_mode) {
				App_Uart_SetRaw(0);
				ota_active = 0;
			}
		}
		else if (ota_timeout(OTA_START_MS)) {
			if (start_tries < OTA_START_TRIES) {
				if (App_Link_SendOtaStartWithSeq(ota_start_seq, ota_bin_size, ota_img_ver)) {
					start_tries++;
					wait_t0 = clock_time();
				}
			}
			else {
				ota_start_optimistic_raw();
			}
		}
		break;

	case ST_WAIT_READY:
		while (App_Uart_RawRead(&b)) {
			if (b == LNK_OTA_READY) {
				blk_off = 0;
				blk_tries = 0;
				ota_rsp_len = 0;
				wait_t0 = clock_time();
				st = ST_SEND_BLOCK;
				break;
			}
		}
		if (st == ST_WAIT_READY && ota_timeout(OTA_READY_MS)) {
			if (++ready_tries >= OTA_READY_TRIES) {
				st = ST_ERROR;
				App_Uart_SetRaw(0);
				ota_raw_mode = 0;
				ota_active = 0;
			}
			else {
				App_Uart_SendRaw(&ota_restart, 1);
				ota_rsp_len = 0;
				wait_t0 = clock_time();
			}
		}
		break;

	case ST_SEND_BLOCK:
		ota_send_block_at(blk_off);
		blk_tries++;
		wait_t0 = clock_time();
		st = ST_WAIT_ACK;
		break;

	case ST_WAIT_ACK:
		token = ota_take_token(ack_off, &status);
		if (token == OTA_TOKEN_MATCH) {
			if (status == LNK_OTA_ACK) {
				last_blk_off = blk_off;
				if (blk_off == 0) {
					APP_DEBUG(DEBUG_OTA_EN, "OTA: ack off=0 len=%d\r\n", (int)blk_len);
				}
				blk_off += blk_len;
				blk_tries = 0;
				st = (blk_off >= ota_bin_size) ? ST_WAIT_DONE : ST_SEND_BLOCK;
				if (st == ST_WAIT_DONE) {
					done_tries = 0;
					wait_t0 = clock_time();
				}
			}
			else if (status == LNK_OTA_NAK) {
				ota_block_failure();
			}
		}
		if (st == ST_WAIT_ACK && ota_timeout(OTA_ACK_MS)) ota_block_failure();
		break;

	case ST_WAIT_DONE:
		token = ota_take_token(last_blk_off, &status);
		if (token == OTA_TOKEN_DONE) {
			App_Uart_SetRaw(0);
			ota_raw_mode = 0;
			App_Link_RestartInfo();
			wait_t0 = clock_time();
			st = ST_WAIT_BOOT;
			boot_tries = 0;
		}
		else if (token == OTA_TOKEN_MATCH && status == LNK_OTA_NAK) {
			ota_done_failure();
		}
		if (st == ST_WAIT_DONE && ota_timeout(OTA_DONE_MS)) ota_done_failure();
		break;

	case ST_WAIT_DONE_ACK:
		token = ota_take_token(last_blk_off, &status);
		if (token == OTA_TOKEN_DONE) {
			App_Uart_SetRaw(0);
			ota_raw_mode = 0;
			App_Link_RestartInfo();
			wait_t0 = clock_time();
			st = ST_WAIT_BOOT;
			boot_tries = 0;
		}
		else if (token == OTA_TOKEN_MATCH) {
			if (status == LNK_OTA_ACK) {
				wait_t0 = clock_time();
				st = ST_WAIT_DONE;
			}
			else if (status == LNK_OTA_NAK) {
				ota_done_failure();
			}
		}
		if (st == ST_WAIT_DONE_ACK && ota_timeout(OTA_ACK_MS)) ota_done_failure();
		break;

	case ST_WAIT_BOOT:
		if (App_Link_Ca51f2Alive() && App_Link_Ca51f2Ver() == ota_img_ver) {
			flash_erase(OTA_IMG_ADDR);
			SYSTEM_RESET();
		}
		else if (ota_timeout(OTA_BOOT_RETRY_MS)) {
			if (++boot_tries >= OTA_BOOT_TRIES) {
				/* The CA51F2 answers but never reports the flashed version    */
				/* (image/header mismatch): give up instead of restarting the  */
				/* Info handshake forever with ota_active = 1 - that state     */
				/* drops every incoming frame until a physical reboot. The     */
				/* OTA slot is kept; a later hub delivery starts a clean run.  */
				st = ST_ERROR;
				ota_active = 0;
			}
			else {
				App_Link_RestartInfo();
				wait_t0 = clock_time();
			}
		}
		break;

	case ST_ERROR:
		break;

	default:
		break;
	}
}

uint8_t App_Ota_IsActive(void) {
	return ota_active;
}

uint8_t App_Ota_InfoAllowed(void) {
	return (uint8_t)(ota_active && st == ST_WAIT_BOOT);
}
