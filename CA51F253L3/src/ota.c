/******************************************************************************/
#include "include/stdint.h"
#include "include/config.h"
#include "include/ca51f2sfr.h"
#include "include/ca51f2xsfr.h"
#include "include/ota.h"
#include "include/link_proto.h"
#include "include/relay.h"
#include "include/uart.h"
#include "include/led.h"
#include "include/chars.h"
#include "debug.h"

extern const uint8_t  code flasher_blob[];
extern const uint16_t code flasher_blob_len;
/******************************************************************************/

static uint8_t  ota_pending = 0;
static uint8_t  ota_start_seq;
static uint16_t ota_size;
static uint16_t ota_start_version;
static uint8_t  ota_start_rsp;

void Ota_OnStart(link_ctx_t xdata *cx) {
	const uart_frame_t xdata *f = cx->f;
	uint16_t size;
	uint16_t version;

	/* size is u32 BE on the wire; the program area fits in 16 bits, so any   */
	/* image with a non-zero high half is above LNK_OTA_PROGRAM_MAX anyway    */
	/* and gets the same RANGE answer as before.                              */
	if(f->payload[0] || f->payload[1]) {
		cx->r->status = LNK_ST_RANGE;
		return;
	}
	size = (uint16_t)(((uint16_t)f->payload[2] << 8) | f->payload[3]);
	version = (uint16_t)(((uint16_t)f->payload[4] << 8) | f->payload[5]);
	if(size < LNK_OTA_BLOCK_MAX || size > LNK_OTA_PROGRAM_MAX) {
		cx->r->status = LNK_ST_RANGE;
		return;
	}
	if(version == 0) {
		cx->r->status = LNK_ST_VALUE;
		return;
	}
	if(ota_pending) {
		if(ota_start_seq == f->seq && ota_size == size &&
		   ota_start_version == version) {
			cx->r->status = ota_start_rsp;
			cx->r->type = 11;
			cx->r->len = 0;
			return;
		}
		cx->r->status = LNK_ST_BUSY;
		cx->r->len = 0;
		return;
	}
	ota_start_seq = f->seq;
	ota_size = size;
	ota_start_version = version;
	ota_start_rsp = LNK_ST_OK;
	Relay_Set(0);
	ota_pending = 1;
	cx->r->status = LNK_ST_OK;
	cx->r->type = 11;
	cx->r->len = 0;
}

uint8_t Ota_IsPending(void) {
	return ota_pending;
}

void Ota_Poll(void) {
	uint16_t i;
	uint8_t xdata *dst;
	uint16_t xdata *size_cell;

	if(!ota_pending) return;
	/* Wait until the OTA_START reply has fully left the UART1 TX ring. */
	if(!Uart1_TxIdle()) return;

	/* Update-in-progress indication: "UP" on the big digits. The REG-COM driver
	   scans LXDAT in hardware, so this survives the handoff to the flasher.
	   INDEX3 = tens, INDEX2 = units (see led.h). */
	Led_SetBrightness(LED_BRIGHTNESS_MAX);
	Led_Backlight(BL_KEYS,  LED_BRIGHTNESS_MAX);
	Led_Backlight(BL_GREEN, LED_BRIGHTNESS_MAX);
	Led_SymInit();
	Led_Char(3, CH_U);
	Led_Char(2, CH_P);
	Led_Flush();

	size_cell = (uint16_t xdata *)OTA_SIZE_CELL_XRAM;
	*size_cell = ota_size;             /* flasher reads bin_size from XRAM */

	EA = 0;                            /* ISR vectors point into erased flash */
	dst = (uint8_t xdata *)0x0000;
	for(i = 0; i < flasher_blob_len; i++) {
		dst[i] = flasher_blob[i];
	}
	MECON |= 0x80;                     /* REMAP=1: XRAM -> code 0x8000 */
	((void (code *)(void))0x8000)();   /* never returns                */
}
