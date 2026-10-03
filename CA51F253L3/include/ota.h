#ifndef _OTA_H_
#define _OTA_H_
/***********************************************************************************/
/* CA51F2 receive side of the CA51F2-over-Zigbee OTA.                              */
/* OTA_START is a normal link frame; after it is acknowledged the app hands off    */
/* to the XRAM flasher (src/flasher) and the link layer is no longer used.         */
/***********************************************************************************/
#include "include/stdint.h"
#include "include/config.h"
#include "include/link_proto.h"

#if LNK_OTA_PROGRAM_MAX != FLASH_CODE_SIZE
#error OTA program limit does not match flash layout
#endif

/* XRAM cell where the app publishes bin_size (u16 LE) for the flasher.           */
#define OTA_SIZE_CELL_XRAM   0x07F0u

void Ota_OnStart(link_ctx_t xdata *cx);
void Ota_Poll(void);
uint8_t Ota_IsPending(void);

#endif
