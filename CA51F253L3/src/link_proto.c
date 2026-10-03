/********************************************************************************/
/* CA51F2 <-> ZT3L link: protocol dispatcher. Matches incoming frames to        */
/* entity handlers by Cmd ID, routes requests/commands to a reply, and applies  */
/* reports without answering.                                                   */
/*                                                                              */
/* CRC-8/MAXIM is checked in the parser (uart.c); frames with a bad CRC never   */
/* reach this module, so no redundant CRC validation is done here.              */
/*                                                                              */
/* Extending: add a new handler (see h_time/h_info as examples), add its Cmd    */
/* ID to lnk_table[], and optionally extend serial.md.                          */
/********************************************************************************/
#include "include/stdint.h"
#include <stddef.h>
#include "include/config.h"
#include "include/ca51f2sfr.h"
#include "include/ca51f2xsfr.h"
#include "include/gpiodef_f2.h"
#include "include/system_clock.h"

#include "include/uart.h"
#include "include/link_proto.h"
#include "include/debug.h"
#include "include/rtc.h"
#include "include/settings.h"
#include "include/buttons.h"
#include "include/led.h"
#include "include/ota.h"
//#include <string.h>
/*******************************************************************************/
/* Firmware identity (reported in LNK_CMD_INFO): identity lives in config.h.   */
/*******************************************************************************/
static const uint8_t code lnk_model[] = DEVICE_MODEL;
/* Compile-time guard: the model string must fit inside the Info frame.       */
typedef char lnk_model_fits[(sizeof(DEVICE_MODEL) - 1 <= LNK_INFO_MODEL_MAX) ? 1 : -1];

static uint8_t xdata link_seq  = 0;               /* outgoing frame sequence       */
static uint8_t xdata link_tx[UART_FRAME_PAYLOAD]; /* scratch reply buffer          */
static link_res_t xdata link_result;
static link_ctx_t xdata link_context;
static uint8_t xdata link_session;

typedef struct {
	uint8_t valid;
	uint8_t seq;
	uint8_t cmd;
	uint8_t status;
	uint8_t type;
	uint8_t len;
} lnk_dedup_t;

#define LNK_DEDUP_PAYLOAD LNK_SCHED_DAY_LEN
static lnk_dedup_t xdata link_dedup;
static uint8_t xdata link_dedup_payload[LNK_DEDUP_PAYLOAD];

/* --- Reliable outgoing messages -------------------------------------------   */
/* Messages that need a delivery confirmation are sent with ACK and retried     */
/* with the SAME SEQ: 3 attempts 100 ms apart, then the cycle repeats every     */
/* 30 s until the peer replies (serial.md retry policy). One slot per command;  */
/* a newer value for a queued command replaces the pending one.                 */
#define LNK_TXQ_N             6
#define LNK_TXQ_GENERAL_N     4
#define LNK_TXQ_TEMP_BASE     LNK_TXQ_GENERAL_N
#define LNK_TEMP_N            2
#define LNK_TXQ_PAYLOAD       LNK_SCHED_DAY_LEN
#define LNK_TXQ_RSP_TIMEOUT   10     /* 100 ms to wait for a reply          */
#define LNK_TXQ_TRIES         3      /* attempts per cycle                  */
#define LNK_TXQ_CYCLE         3000   /* 30 s between cycles                 */
#define LNK_INFO_NET_DELAY    10     /* 100 ms: Net-status Req after Info   */
#define LNK_BOOT_RETRY_SECS   3      /* resend the boot notice until acked  */

#define LNK_TIME_PERIOD_SECS  3600   /* hourly clock resync while connected  */

#define LNK_TXQ_FREE          0
#define LNK_TXQ_WAIT_SEND     1
#define LNK_TXQ_WAIT_RSP      2

typedef struct {
	uint8_t  state;
	uint8_t  flags;
	uint8_t  cmd;
	uint8_t  type;
	uint8_t  len;
	uint16_t deadline;
	uint8_t  seq;
	uint8_t  tries;
	uint8_t  session;
	uint8_t  payload[LNK_TXQ_PAYLOAD];
} lnk_txq_t;

static lnk_txq_t xdata lnk_txq[LNK_TXQ_N];

/* --- Temperature reporting (0x02 internal, 0x03 external) ------------------   */
/* Each accepted value is sent once after a handshake, then whenever it changes. */
/* A missing sensor is sent as 0 and repeated at least every keepalive interval. */
#define LNK_TEMP_KEEPALIVE_SECS  (15 * 60)


/* --- State dump (LNK_CMD_STATE_ALL) ---------------------------------------   */
/* On request the CA51F2 sends its whole configuration one frame at a time,     */
/* each with ACK: the next frame is sent only after the peer's reply. On no     */
/* reply the same SEQ is retried 3x/100 ms, then the cycle repeats every 30 s.  */
/* Temperatures are NOT part of the dump - they are reported separately         */
/* (Link_ReportTemps: first after the handshake, then on change).               */
#define LNK_DUMP_SINGLES      13
#define LNK_DUMP_SCHED        7
#define LNK_DUMP_N            (LNK_DUMP_SINGLES + LNK_DUMP_SCHED)
#define LNK_DUMP_TRIES        3
#define LNK_DUMP_RSP_TIMEOUT  10     /* 100 ms per attempt                          */
#define LNK_DUMP_CYCLE        3000   /* 30 s between cycles                         */
#define LNK_DUMP_PERM_MAX     3      /* permanent rejections before giving up       */

typedef struct {
	uint8_t peer_alive;
	uint8_t boot_pending;
	uint8_t boot_seq;
	uint8_t boot_age;
	uint8_t net_status;
	uint8_t time_asked;
	uint16_t time_age;
	uint8_t dump_active;
	uint8_t dump_idx;
	uint8_t dump_seq;
	uint8_t dump_cmd;
	uint8_t dump_tries;
	uint8_t dump_perm;
	uint8_t sysmode_tx;
	uint8_t running_tx;
	uint8_t running_current;
	uint8_t sysmode_val;
	uint8_t sysmode_pending;
	uint8_t lock_changed;
	uint8_t setting_changed;
	uint8_t setting_cmd;
	uint8_t temp_first[2];
	int16_t temp_last[2];
	uint16_t temp_age[2];
	uint16_t dump_deadline;
} link_session_t;

static link_session_t xdata link_s;

#define link_peer_alive      link_s.peer_alive
#define link_boot_pending    link_s.boot_pending
#define link_boot_seq        link_s.boot_seq
#define link_boot_age        link_s.boot_age
#define link_net_status      link_s.net_status
#define link_time_asked      link_s.time_asked
#define link_time_age        link_s.time_age
#define link_dump_active     link_s.dump_active
#define link_dump_idx        link_s.dump_idx
#define link_dump_seq        link_s.dump_seq
#define link_dump_cmd        link_s.dump_cmd
#define link_dump_tries      link_s.dump_tries
#define link_dump_perm       link_s.dump_perm
#define link_dump_deadline   link_s.dump_deadline
#define link_sysmode_tx      link_s.sysmode_tx
#define link_running_tx      link_s.running_tx
#define link_running_current link_s.running_current
#define link_sysmode_val     link_s.sysmode_val
#define link_sysmode_pending link_s.sysmode_pending
#define link_lock_changed    link_s.lock_changed
#define link_setting_changed link_s.setting_changed
#define link_setting_cmd     link_s.setting_cmd
#define temp_first           link_s.temp_first
#define temp_last            link_s.temp_last
#define temp_age             link_s.temp_age

/* Next outgoing SEQ (own counter, 1..255, never 0).                          */
static uint8_t lnk_next_seq(void) {
	if (++link_seq == 0) link_seq = 1;
	return link_seq;
}

#define LINK_SETTINGS_RETRY_NONE 0xFF
static uint8_t xdata link_settings_retry;
static uint8_t xdata link_factory_pending;

/* Wireless climate sensor (WS) telemetry (declared here: Link_Tick1s ages      */
/* ws_age before its early returns). Temperature/humidity arrive via h_ws();    */
/* humidity has NO counter of its own - it rides on ws_age via Link_WsFresh.    */
static int16_t  xdata ws_temp_c100;      /* last WS temperature, ZCL x100       */
static uint16_t xdata ws_humid_c100;     /* last WS humidity,    ZCL x100       */
static uint16_t xdata ws_age;            /* s since the first/last WS           */
                                         /* TEMPERATURE frame; Link_Init boots  */
                                         /* it STALE (>= WS_FRESH_SECS), so a   */
                                         /* humidity-only frame can never open  */
                                         /* the freshness window (bench bug     */
                                         /* 2026-09-30). Humidity rides on it.  */

static uint8_t lnk_txq_post(uint8_t cmd, uint8_t flags, uint8_t type, uint8_t len,
                            const uint8_t xdata *payload, uint16_t delay);

/********************************************************************************/
/* Queue a message for reliable delivery (ACK + retries). `delay` is in 10 ms   */
/* ticks before the first attempt.                                              */
/********************************************************************************/
static uint8_t lnk_txq_post(uint8_t cmd, uint8_t flags, uint8_t type, uint8_t len,
                            const uint8_t xdata *payload, uint16_t delay) {
	lnk_txq_t xdata *q = 0;
	uint8_t i;
	uint8_t j;
	uint8_t first;

	if(!link_peer_alive || Ota_IsPending()) return 0;
	if(len > LNK_TXQ_PAYLOAD) return 0;
	first = (cmd == LNK_CMD_TEMP_LOCAL || cmd == LNK_CMD_TEMP_OUTDOOR) ?
	        LNK_TXQ_TEMP_BASE : 0;
	for(i = first; i < first + (first ? LNK_TEMP_N : LNK_TXQ_GENERAL_N); i++) {
		if(lnk_txq[i].state != LNK_TXQ_FREE && lnk_txq[i].cmd == cmd) {
			q = &lnk_txq[i];
			break;
		}
		if(!q && lnk_txq[i].state == LNK_TXQ_FREE) q = &lnk_txq[i];
	}
	if(!q) return 0;
	if(q->state != LNK_TXQ_FREE &&
	   (cmd == LNK_CMD_TEMP_LOCAL || cmd == LNK_CMD_TEMP_OUTDOOR) &&
	   q->len == 2 && q->payload[0] == payload[0] && q->payload[1] == payload[1])
		return 1;
	q->flags = flags;
	q->cmd = cmd;
	q->type = type;
	q->len = len;
	for(j = 0; j < len; j++) q->payload[j] = payload[j];
	q->session = link_session;
	q->seq = lnk_next_seq();
	q->tries = 0;
	q->state = LNK_TXQ_WAIT_SEND;
	q->deadline = (uint16_t)(BTN_Tick10ms() + delay);
	return 1;
}

/*********************************************************************************/
/* Ask the ZT3L for the current time (reliably, ACK). Only while the peer        */
/* reports the network as connected; sent at startup and then every hour. When   */
/* the network is left the request stops and the RTC keeps running with the time */
/* it last held (nothing here touches it).                                       */
/*********************************************************************************/
static void link_request_time(void) {
	if(link_time_asked) return;
	if(link_net_status != LNK_NET_CONNECTED) return;
	if(lnk_txq_post(LNK_CMD_TIME, UART_F_ACK, 0, 0, 0, 0)) {
		link_time_asked = 1;
		link_time_age   = 0;
	}
}
/********************************************************************************/
/* Apply a new network status; on the FREE -> CONNECTED edge request the time.  */
/********************************************************************************/
static void link_net_apply(uint8_t st) {
	uint8_t was = link_net_status;

	link_net_status = st;
	if(was != LNK_NET_CONNECTED && st == LNK_NET_CONNECTED) {
		link_time_asked = 0;
		link_time_age   = 0;
		link_request_time();
	}
	else if(st != LNK_NET_CONNECTED) {
		link_time_age = 0;      /* not connected: stop the periodic resync      */
	}
}
/********************************************************************************/
/* One-second tick: while connected, resync the clock once an hour.             */
/********************************************************************************/
void Link_Tick1s(void) {
	/* WS freshness ages unconditionally - before every early return, so a stale */
	/* wireless sensor drops out even while the Zigbee network is down. During   */
	/* OTA pending main() skips this call entirely; harmless there because the   */
	/* relay is forced OFF and a CA51F2 reboot re-clears the xdata anyway.       */
	if(ws_age < WS_FRESH_SECS) ws_age++;
	if(Ota_IsPending()) return;
	/* Boot notice retry: independent of the network state.                    */
	if(link_boot_pending) {
		if(++link_boot_age >= LNK_BOOT_RETRY_SECS) {
			link_boot_age = 0;
			Uart1_SendFrame(link_boot_seq, UART_F_ACK | UART_F_CMD, 0,
			                LNK_CMD_CA51F2_BOOT, 0, 0, 0);
		}
	}
	if(link_net_status != LNK_NET_CONNECTED) return;
	if(link_time_age < LNK_TIME_PERIOD_SECS) link_time_age++;
	if(link_time_age >= LNK_TIME_PERIOD_SECS) {
		link_time_asked = 0;    /* hourly resync: bypass the once-per-link guard */
		link_request_time();
	}
}
/********************************************************************************/
/* Boot notice: tell the peer we (re)started so it can run a fresh Info         */
/* handshake. Sent immediately and repeated every LNK_BOOT_RETRY_SECS seconds   */
/* until the peer answers OK (see Link_Tick1s).                                 */
/********************************************************************************/
void Link_BootNotify(void) {
	if(Ota_IsPending()) return;
	link_boot_pending = 1;
	link_boot_age     = 0;
	link_boot_seq     = lnk_next_seq();
	Uart1_SendFrame(link_boot_seq, UART_F_ACK | UART_F_CMD, 0,
	                LNK_CMD_CA51F2_BOOT, 0, 0, 0);
}
/*******************************************************************************/
/* Advance the state dump to the next frame (or finish it).                    */
/*******************************************************************************/
static void link_dump_advance(void) {
	if(++link_dump_idx >= LNK_DUMP_N) {
		link_dump_active = 0;
	}
	else {
		link_dump_seq      = lnk_next_seq();
		link_dump_tries    = 0;
		link_dump_perm     = 0;                /* accepted frame: streak cleared       */
		link_dump_deadline = BTN_Tick10ms();   /* send the next one now                */
	}
}

/* Build and send one state-dump frame; defined at the end of the file (the     */
/* dense switch there triggers SDCC warning 110, suppressed locally).           */
static void link_dump_send(uint8_t i);

/* Apply one scalar setting written from the network; defined at the end of the */
/* file (dense switch -> SDCC warning 110 suppressed locally).                  */
static void h_setting(link_ctx_t xdata *cx);

static uint8_t lnk_dedup_replay(const uart_frame_t xdata *f) {
	if(!link_dedup.valid || link_dedup.cmd != f->cmd ||
	   link_dedup.seq != f->seq) return 0;
	Uart1_SendFrame(f->seq, UART_F_RSP, link_dedup.status, f->cmd,
	                link_dedup.type, link_dedup.len, link_dedup_payload);
	return 1;
}

static void lnk_dedup_store(const uart_frame_t xdata *f,
                            const link_res_t xdata *r) {
	uint8_t i;

	link_dedup.valid  = 1;
	link_dedup.seq    = f->seq;
	link_dedup.cmd    = f->cmd;
	link_dedup.status = r->status;
	link_dedup.type   = r->type;
	link_dedup.len    = r->len;
	for(i = 0; i < r->len; i++) link_dedup_payload[i] = link_tx[i];
}

static void link_session_begin(void) {
	uint8_t xdata *p = (uint8_t xdata *)&link_s;
	uint8_t i;

	link_session++;
	link_dedup.valid = 0;
	for(i = 0; i < sizeof(link_s); i++) p[i] = 0;
	link_sysmode_tx = 0xFF;
	link_running_tx = 0xFF;
	link_settings_retry = LINK_SETTINGS_RETRY_NONE;
	link_factory_pending = 0;
	temp_first[0] = 1;
	temp_first[1] = 1;
	for(i = 0; i < LNK_TXQ_N; i++) lnk_txq[i].state = LNK_TXQ_FREE;
}

/********************************************************************************/
/* Reset the link module state. MUST be called from main() at boot: SDCC does   */
/* not clear uninitialised xdata (XSEG) and an 8051 reset keeps RAM, so these   */
/* statics would otherwise keep their values from the previous run (the MCU     */
/* would think the peer is alive and start reporting before the handshake).     */
/********************************************************************************/
void Link_Init(void) {
	link_seq = 0;
	link_session = 0;
	link_dedup.valid = 0;
	/* boot STALE: no WS temperature seen yet - a humidity frame arriving     */
	/* before the first temperature must not open the freshness window.       */
	ws_age = WS_FRESH_SECS;
	link_session_begin();
}
/*******************************************************************************/
/* Big-endian helpers (used by handlers that pack/unpack multi-byte values).   */
/*******************************************************************************/
static uint16_t  lnk_get16(const uint8_t xdata *p) {
	return ((uint16_t)p[0] << 8) | p[1];
}
static uint32_t lnk_get32(const uint8_t xdata *p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
	     | ((uint32_t)p[2] <<  8) | p[3];
}
static void lnk_put16(uint8_t xdata *p, uint16_t v) {
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)(v);
}
/********************************************************************************/
/* Handler: LNK_CMD_INFO (0x01)  -  identity/version handshake.                 */
/* Role: Req/DLC=0 -> reply; Rep/DLC>0 -> informational only (apply nothing)    */
/*   (length varies with peer model, range-checked). Cmd not defined for        */
/*   Info (just Req/Rep).                                                       */
/* Payload layout: ver_proto(u8) + ver_app(u16 BE) + chipid(u16 BE) + model.    */
/********************************************************************************/
static void h_info(link_ctx_t xdata *cx) {
	const uart_frame_t xdata *f = cx->f;
	link_res_t xdata *r = &link_result;
	uint8_t i;
	uint8_t n = sizeof(lnk_model) - 1;

	if (f->len == 0) {
		/* Request (ACK=1, DLC=0): reply with our identity, peer is alive     */
		link_session_begin();
		link_peer_alive = 1;
		link_tx[LNK_INFO_VER_PROTO] = LNK_PROTO_VER;
		link_tx[LNK_INFO_VER_APP]   = (uint8_t)(APP_VERSION >> 8);
		link_tx[LNK_INFO_VER_APP+1] = (uint8_t)APP_VERSION;
		link_tx[LNK_INFO_CHIPID]    = (uint8_t)(CHIP_ID >> 8);
		link_tx[LNK_INFO_CHIPID+1]  = (uint8_t)CHIP_ID;
		for (i = 0; i < n; i++) link_tx[LNK_INFO_MODEL + i] = lnk_model[i];
		r->len    = LNK_INFO_MODEL + n;
		/* Ask for the network status 100 ms after this reply. The peer then  */
		/* requests the whole configuration (LNK_CMD_STATE_ALL). Re-prime the */
		/* state sentinels so the next Link_UpdateStates just records them.   */
		lnk_txq_post(LNK_CMD_NET_STATUS, UART_F_ACK, 0, 0, 0, LNK_INFO_NET_DELAY);
		return;
	}
	/* Report/Reply carrying the peer identity: informational only            */
	if(f->payload[LNK_INFO_VER_PROTO] != LNK_PROTO_VER) {
		r->status = LNK_ST_BAD_VER;
	}
}
/*********************************************************************************/
/* Handler: LNK_CMD_TIME (0x00)  -  clock synchronisation.                       */
/* Cmd (ACK=1, CMD=1, DLC=8): apply the received time, answer OK.                */
/* Rep (ACK=0, DLC=8): apply received time (no answer).                          */
/* Payload: utc(4 B BE) + local(4 B BE); only LOCAL drives this MCU's clock -    */
/* the utc half had a single reader and lost it with the dead TIME-Req answer    */
/* path (the ZT3L never sends a TIME Req - it only pushes Cmds; no STATE_ALL     */
/* side effect: TIME is exempt from the ZT3L resync rule).                       */
/*********************************************************************************/
static void h_time(link_ctx_t xdata *cx) {
	const uart_frame_t xdata *f = cx->f;

	RTC_SetUnixTime(lnk_get32(f->payload + 4));
}
/********************************************************************************/
/* Handler: LNK_CMD_SCHED_MON..SUN (0x20..0x26) - weekly schedule, one day.     */
/* Req (ACK=1, DLC=0): reply with the 24-byte day image (RAW).                  */
/* Cmd/Rep (DLC=24): apply the day image; Cmd replies OK, Rep applies silently. */
/* Payload: 6 entries x 4 B BE = { u16 transTime (minutes since midnight,       */
/*          0xFFFF = unused), i16 heatTemp (x100 deg C) }.                      */
/********************************************************************************/
static schedule_t xdata *sched_day(uint8_t cmd) {
	/* h_sched is registered only for LNK_CMD_SCHED_MON..SUN (0x20..0x26). */
	return Sched_DayRow((uint8_t)(cmd - LNK_CMD_SCHED_MON));
}

static void h_sched(link_ctx_t xdata *cx) {
	const uart_frame_t xdata *f = cx->f;
	link_res_t xdata *r = &link_result;
	schedule_t xdata *day;
	uint8_t i;
	/* payload base cached: the two passes below walk it with i*4 offsets   */
	const uint8_t xdata *pay = f->payload;

	/* Validate every entry before touching the stored image. Unused slots      */
	/* (minute == 0xFFFF) skip the temperature check: the host is allowed to    */
	/* leave that field at any value (often 0) for a slot it does not use.      */
	day = sched_day(f->cmd);
	for(i = 0; i < LNK_SCHED_N; i++) {
		uint16_t t = lnk_get16(pay + i * LNK_SCHED_ENTRY);
		int16_t  h = (int16_t)lnk_get16(pay + i * LNK_SCHED_ENTRY + 2);

		if(t == LNK_SCHED_TIME_EMPTY) {
			continue;
		}
		if(t > LNK_SCHED_DAY_MAX_MIN) {
			r->status = LNK_ST_RANGE;
			return;
		}
		/* Sanity is the fixed ABS range, NOT the configurable limits (CA-2):    */
		/* min/maxHeatSetpointLimit is policy applied when the setpoint is used  */
		/* (Sched_ActiveSetpoint), so a temporary limit change can never reject  */
		/* a stored schedule.                                                    */
		if(h < ABS_MIN_HEATSETPOINT_LIMIT_DEF ||
		   h > ABS_MAX_HEATSETPOINT_LIMIT_DEF) {
			r->status = LNK_ST_RANGE;
			return;
		}
	}
	{
		uint8_t changed = 0;

		for(i = 0; i < LNK_SCHED_N; i++) {
			uint16_t t = lnk_get16(pay + i * LNK_SCHED_ENTRY);
			int16_t  h = (int16_t)lnk_get16(pay + i * LNK_SCHED_ENTRY + 2);

			if(day[i].minute != t || day[i].temperature != h) {
				day[i].minute      = t;
				day[i].temperature = h;
				changed = 1;
			}
		}
		if(changed && !settings_save()) {
			r->status = LNK_ST_FLASH;
			return;
		}
	}
}
/********************************************************************************/
/* Handler: LNK_CMD_NET_STATUS (0x1F)  -  network status.                       */
/* CA51F2 is the requester (its reply is consumed in Link_OnFrame). As a server */
/* it only applies the peer's unsolicited report (ACK=0); a request from the    */
/* peer is answered UNSUPPORT (this board has no network status to serve).      */
/* Payload: enum u8, LNK_NET_*.                                                 */
/********************************************************************************/
static void h_net(link_ctx_t xdata *cx) {
	const uart_frame_t xdata *f = cx->f;
	link_res_t xdata *r = &link_result;

	if (f->flags & UART_F_ACK) {
		r->status = LNK_ST_UNSUPPORT;
		return;
	}
	link_net_apply(f->payload[0]);
	DEBUG(LINK_EN, { debug_kv("net", (uint16_t)f->payload[0]); });
}
/********************************************************************************/
/* Handler: LNK_CMD_SYSTEM_MODE (0x0A)  -  system mode.                         */
/* Cmd (ACK=1, CMD=1, DLC=1): the peer asks us to switch the system mode;       */
/* main() applies it (Link_TakeSysMode) and we reply OK.                        */
/* Payload: enum u8, LNK_SYSMODE_*.                                             */
/********************************************************************************/
static void h_sysmode(link_ctx_t xdata *cx) {
	const uart_frame_t xdata *f = cx->f;
	link_res_t xdata *r = &link_result;

	link_sysmode_val     = f->payload[0];
	link_sysmode_pending = 1;
}
/********************************************************************************/
/* Handler: LNK_CMD_STATE_ALL (0x28)  -  report the whole configuration.        */
/* Cmd (ACK=1, CMD=1): reply OK and start the state dump (one frame every       */
/* LNK_DUMP_GAP ticks, 100 ms).                                                 */
/********************************************************************************/
static void h_state_all(link_ctx_t xdata *cx) {
	(void)cx;

	link_dump_idx      = 0;
	link_dump_seq      = lnk_next_seq();
	link_dump_tries    = 0;
	link_dump_perm     = 0;
	link_dump_deadline = BTN_Tick10ms();   /* first frame now */
	link_dump_active   = 1;
}
/***********************************************************************************************************************/
/* Wireless climate sensor (WS) telemetry: LNK_CMD_TEMP_NET (0x04, i16 x100) and                                       */
/* LNK_CMD_HUMID_NET (0x06, u16 x100). The ZT3L has already validated the ZCL report bounds; we only store the last    */
/* value in RAM and reply OK (default result); freshness (Link_WsFresh) drives the relay/display override.             */
/***********************************************************************************************************************/
static void h_ws(link_ctx_t xdata *cx) {
	const uart_frame_t xdata *f = cx->f;

	if(f->cmd == LNK_CMD_TEMP_NET) {
		ws_temp_c100 = (int16_t)lnk_get16(f->payload);
		/* ONLY a temperature arrival opens/extends the window (a humidity-only */
		/* frame must never do it: ws_temp_c100 could still be the initial 0    */
		/* and would drive display/relay).                                      */
		ws_age = 0;
		DEBUG(TEMP_EN, { debug_kv("wt", (uint16_t)ws_temp_c100); });
	}
	else {
		ws_humid_c100 = lnk_get16(f->payload);
		DEBUG(TEMP_EN, { debug_kv("wh", ws_humid_c100); });
	}
}
/*******************************************************************************/
/* Dispatch table (cmd -> handler). Unknown Cmd IDs fall through to the        */
/* default UNSUPPORT handler automatically.                                    */
/*******************************************************************************/
#define LNK_ROLE_REPORT 1
#define LNK_ROLE_REQ    2
#define LNK_ROLE_CMD    4

typedef struct {
	uint8_t cmd;
	uint8_t len;
	uint8_t type;
	uint8_t roles;
	link_cmd_h_t fn;
} lnk_cmd_t;

static const lnk_cmd_t code lnk_table[] =
{
	{ LNK_CMD_TIME, 8, LNK_T_RAW, LNK_ROLE_REPORT | LNK_ROLE_CMD, h_time },
	{ LNK_CMD_INFO, LNK_INFO_MODEL, LNK_T_RAW, LNK_ROLE_REPORT | LNK_ROLE_REQ, h_info },
	{ LNK_CMD_SETPOINT_HEAT, 2, LNK_T_I16, LNK_ROLE_REPORT | LNK_ROLE_CMD, h_setting },
	{ LNK_CMD_SYSTEM_MODE, 1, LNK_T_ENUM, LNK_ROLE_CMD, h_sysmode },
	{ LNK_CMD_PROG_MODE, 1, LNK_T_BITMAP8, LNK_ROLE_REPORT | LNK_ROLE_CMD, h_setting },
	{ LNK_CMD_SENSOR_SRC, 1, LNK_T_ENUM, LNK_ROLE_REPORT | LNK_ROLE_CMD, h_setting },
	{ LNK_CMD_CAL_ACTIVE, 1, LNK_T_I8, LNK_ROLE_REPORT | LNK_ROLE_CMD, h_setting },
	{ LNK_CMD_CAL_EXTERNAL, 1, LNK_T_I8, LNK_ROLE_REPORT | LNK_ROLE_CMD, h_setting },
	{ LNK_CMD_HYSTERESIS, 1, LNK_T_I8, LNK_ROLE_REPORT | LNK_ROLE_CMD, h_setting },
	{ LNK_CMD_LIMIT_MIN, 2, LNK_T_I16, LNK_ROLE_REPORT | LNK_ROLE_CMD, h_setting },
	{ LNK_CMD_LIMIT_MAX, 2, LNK_T_I16, LNK_ROLE_REPORT | LNK_ROLE_CMD, h_setting },
	{ LNK_CMD_KEY_LOCK, 1, LNK_T_ENUM, LNK_ROLE_REPORT | LNK_ROLE_CMD, h_setting },
	{ LNK_CMD_BRIGHT_DAY, 1, LNK_T_U8, LNK_ROLE_REPORT | LNK_ROLE_CMD, h_setting },
	{ LNK_CMD_BRIGHT_NIGHT, 1, LNK_T_U8, LNK_ROLE_REPORT | LNK_ROLE_CMD, h_setting },
	{ LNK_CMD_NET_STATUS, 1, LNK_T_ENUM, LNK_ROLE_REPORT | LNK_ROLE_REQ, h_net },
	{ LNK_CMD_SCHED_MON, LNK_SCHED_DAY_LEN, LNK_T_RAW, LNK_ROLE_REPORT | LNK_ROLE_CMD, h_sched },
	{ LNK_CMD_STATE_ALL, 0, 0, LNK_ROLE_CMD, h_state_all },
	{ LNK_CMD_OTA_START, 6, LNK_T_RAW, LNK_ROLE_CMD, Ota_OnStart },
	{ LNK_CMD_TEMP_NET, 2, LNK_T_I16, LNK_ROLE_REPORT | LNK_ROLE_CMD, h_ws },
	{ LNK_CMD_HUMID_NET, 2, LNK_T_U16, LNK_ROLE_REPORT | LNK_ROLE_CMD, h_ws },
};

#define LNK_SCHED_TABLE_INDEX 15
#define LNK_TABLE_N  (sizeof(lnk_table) / sizeof(lnk_table[0]))

/******************************************************************************/
/* Send a frame with the next outgoing SEQ.                                   */
/******************************************************************************/
static uint8_t lnk_validate(const uart_frame_t xdata *f, uint8_t xdata *index) {
	const lnk_cmd_t code *e;
	uint8_t i;
	uint8_t role;
	uint8_t len;
	/* Header bytes are cached once: every f->field read through the pointer  */
	/* costs a pointer reload plus an inc-dptr chain per field offset, which  */
	/* was the single largest code generator in this function.                */
	uint8_t flags = f->flags;
	uint8_t cmd   = f->cmd;
	uint8_t flen  = f->len;
	uint8_t type  = f->type;

	if(cmd >= LNK_CMD_SCHED_MON && cmd <= LNK_CMD_SCHED_SUN)
		*index = LNK_SCHED_TABLE_INDEX;
	else {
		*index = LNK_TABLE_N;
		for(i = 0; i < LNK_TABLE_N; i++) {
			if(lnk_table[i].cmd == cmd) {
				*index = i;
				break;
			}
		}
	}
	if(flags & (uint8_t)~(UART_F_RSP | UART_F_ACK | UART_F_CMD) ||
	   ((flags & (UART_F_RSP | UART_F_ACK)) ==
	    (UART_F_RSP | UART_F_ACK)) ||
	   ((flags & UART_F_CMD) && !(flags & UART_F_ACK)))
		return LNK_ST_BAD_FLAGS;
	if(flen > UART_FRAME_PAYLOAD) return LNK_ST_BAD_LEN;
	if(*index == LNK_TABLE_N) {
		if(flags & UART_F_RSP) return flen ? LNK_ST_BAD_LEN : LNK_ST_OK;
		return LNK_ST_UNSUPPORT;
	}
	e = &lnk_table[*index];

	if(flags & UART_F_RSP) {
		/* An error reply may carry a detail payload: the DLC must NOT reject  */
		/* it - a dropped RSP never matches the txq slot, so the command would */
		/* retransmit forever while the real status is swallowed. The payload  */
		/* itself is ignored (only OK replies are parsed below).               */
		if(f->status != LNK_ST_OK) return LNK_ST_OK;
		if(cmd == LNK_CMD_INFO) {
			if(flen < LNK_INFO_MODEL || flen > LNK_INFO_MAX_LEN)
				return LNK_ST_BAD_LEN;
			return type == LNK_T_RAW ? LNK_ST_OK : LNK_ST_BAD_TYPE;
		}
		if(cmd == LNK_CMD_TIME && flen) {
			if(flen != 8) return LNK_ST_BAD_LEN;
			return type == LNK_T_RAW ? LNK_ST_OK : LNK_ST_BAD_TYPE;
		}
		if(cmd == LNK_CMD_NET_STATUS) {
			if(flen != 1) return LNK_ST_BAD_LEN;
			return type == LNK_T_ENUM ? LNK_ST_OK : LNK_ST_BAD_TYPE;
		}
		return flen ? LNK_ST_BAD_LEN : LNK_ST_OK;
	}

	role = flags == 0 ? LNK_ROLE_REPORT :
	       flags == UART_F_ACK ? LNK_ROLE_REQ : LNK_ROLE_CMD;
	if(!(e->roles & role)) return LNK_ST_BAD_FLAGS;
	len = e->len;
	if(cmd == LNK_CMD_INFO) {
		if(flags == UART_F_ACK) len = 0;
		else if(flen < LNK_INFO_MODEL || flen > LNK_INFO_MAX_LEN)
			return LNK_ST_BAD_LEN;
	}
	else if(cmd == LNK_CMD_NET_STATUS && flags == UART_F_ACK) len = 0;
	if(flen != len) return LNK_ST_BAD_LEN;
	if(len && type != e->type) return LNK_ST_BAD_TYPE;
	return LNK_ST_OK;
}

static void lnk_send_error(const uart_frame_t xdata *f, uint8_t status) {
	if(!(f->flags & UART_F_RSP) && (f->flags & UART_F_ACK))
		Uart1_SendFrame(f->seq, UART_F_RSP, status, f->cmd, 0, 0, 0);
}
/*******************************************************************************/
/* Main dispatcher: registered as the uart.c frame callback via                */
/*******************************************************************************/
void Link_OnFrame(const uart_frame_t xdata *f) {
	uint8_t i;
	uint8_t st;
	uint8_t cmd_index;
	/* Cached header bytes: the frame is assembled by the main-loop parser, so  */
	/* it cannot change while we dispatch; re-reading f->field would reload the */
	/* pointer and walk inc-dptr per field for every use (see lnk_validate).    */
	uint8_t flags = f->flags;
	uint8_t cmd   = f->cmd;
	uint8_t seq   = f->seq;
	uint8_t fstat = f->status;
	uint8_t flen  = f->len;

	st = lnk_validate(f, &cmd_index);
	if(st != LNK_ST_OK) {
		lnk_send_error(f, st);
		return;
	}

	if(flags & UART_F_RSP) {
		if(link_dump_active && seq == link_dump_seq && cmd == link_dump_cmd) {
			if(fstat == LNK_ST_OK) {
				link_dump_advance();
			}
			else if((fstat & 0xF0) == 0x30 ||
			        (fstat & 0xF0) == 0x40) {
				/* Transient state error (NOT_READY/BUSY/...): fast retry,    */
				/* and it clears the permanent-rejection streak.              */
				link_dump_tries = 0;
				link_dump_perm  = 0;
				link_dump_deadline = (uint16_t)(BTN_Tick10ms() +
					LNK_DUMP_RSP_TIMEOUT);
			}
			else {
				/* Permanent rejection (UNSUPPORT/RANGE/BAD_TYPE/...): slow    */
				/* cycle, and abort after LNK_DUMP_PERM_MAX - otherwise ONE    */
				/* frame repeats every 30 s forever and the rest of the dump   */
				/* never goes out (version skew the dump cannot fix).          */
				link_dump_tries = 0;
				link_dump_deadline = (uint16_t)(BTN_Tick10ms() +
					LNK_DUMP_CYCLE);
				if(++link_dump_perm >= LNK_DUMP_PERM_MAX) {
					link_dump_active = 0;
					link_dump_perm   = 0;
				}
			}
			return;
		}
		if(link_boot_pending && cmd == LNK_CMD_CA51F2_BOOT &&
		   seq == link_boot_seq) {
			if(fstat == LNK_ST_OK) link_boot_pending = 0;
			return;
		}
		for(i = 0; i < LNK_TXQ_N; i++) {
			if(lnk_txq[i].state == LNK_TXQ_WAIT_RSP &&
			   lnk_txq[i].session == link_session &&
			   lnk_txq[i].seq == seq && lnk_txq[i].cmd == cmd) {
				if(fstat == LNK_ST_BUSY) {
					/* Peer is explicitly busy (its OTA window): keep the        */
					/* message alive but exhaust the fast retries - Link_Poll    */
					/* then falls back to the slow 30 s cycle. Treating BUSY     */
					/* like a rejection (unconditional free) would silently      */
					/* drop the value.                                           */
					lnk_txq[i].tries = LNK_TXQ_TRIES;
					break;
				}
				if(cmd == LNK_CMD_NET_STATUS &&
				   fstat == LNK_ST_OK && flen == 1) {
					link_net_apply(f->payload[0]);
					DEBUG(LINK_EN, { debug_kv("net", (uint16_t)f->payload[0]); });
				}
				else if(fstat == LNK_ST_OK &&
				        cmd >= LNK_CMD_TEMP_LOCAL &&
				        cmd <= LNK_CMD_TEMP_OUTDOOR) {
					temp_last[i - LNK_TXQ_TEMP_BASE]  = lnk_get16(lnk_txq[i].payload);
					temp_age[i - LNK_TXQ_TEMP_BASE]   = 0;
					temp_first[i - LNK_TXQ_TEMP_BASE] = 0;
				}
				lnk_txq[i].state = LNK_TXQ_FREE;
				break;
			}
		}
		return;
	}

	/* Pre-handshake exceptions: only the handshake itself, the peer's          */
	/* net-status push/request and the OTA start. Everything else - including   */
	/* TIME and the schedule days - is a mutating/late frame: NOT_READY. The    */
	/* old `TIME/SCHED && flags == ACK` clause existed for their Req roles,     */
	/* both removed as dead (TIME 2026-09-30, SCHED 2026-10-02).                */
	if(!link_peer_alive && cmd != LNK_CMD_INFO &&
	   cmd != LNK_CMD_NET_STATUS && cmd != LNK_CMD_OTA_START) {
		lnk_send_error(f, LNK_ST_NOT_READY);
		return;
	}
	if(Ota_IsPending() && cmd != LNK_CMD_OTA_START) {
		lnk_send_error(f, LNK_ST_BUSY);
		return;
	}
	if((flags & UART_F_ACK) && lnk_dedup_replay(f)) return;
	link_dedup.valid = 0;
	/* Initialise default result: reply with OK, type RAW(11), zero-length       */

	/* (handlers that produce data override r.len and optionally r.type).         */
	link_result.status = LNK_ST_OK;
	link_result.type   = LNK_T_RAW;
	link_result.len    = 0;
	link_context.f = f;
	link_context.r = &link_result;

	lnk_table[cmd_index].fn(&link_context);

	/* Only answer requests (ACK=1) and commands (ACK=1 + CMD=1).                 */
	/* Reports (ACK=0) are applied silently above and produce no reply.           */
	if (flags & UART_F_ACK) {
		if(link_result.len > LNK_DEDUP_PAYLOAD) link_result.len = 0;
		lnk_dedup_store(f, &link_result);
		Uart1_SendFrame(seq, UART_F_RSP, link_result.status, cmd,
		                link_result.type, link_result.len, link_tx);
	}
}
/*******************************************************************************/
/* Peer liveness accessor: 1 once the ZT3L has requested our Info on boot.     */
/*******************************************************************************/
uint8_t Link_PeerAlive(void) {
	return link_peer_alive;
}
/********************************************************************************/
/* Report one temperature stream (0 = internal 0x02, 1 = external 0x03) with    */
/* the change/keepalive policy. Called once per second.                         */
/********************************************************************************/
static void link_temp_one(uint8_t i, int16_t c100, uint8_t ok) {
	int16_t v = ok ? c100 : 0;

	if(temp_age[i] < 0xFFFF) temp_age[i]++;
	if(temp_first[i] || v != temp_last[i] ||
	   (v == 0 && temp_age[i] > LNK_TEMP_KEEPALIVE_SECS)) {
		link_tx[0] = (uint8_t)((uint16_t)v >> 8);
		link_tx[1] = (uint8_t)v;
		lnk_txq_post((uint8_t)(LNK_CMD_TEMP_LOCAL + i),
		             UART_F_ACK, LNK_T_I16, 2, link_tx, 0);
	}
}

/*******************************************************************************/
/* Report accepted internal (0x02) and external (0x03) temperatures in c100.   */
/* Call once per second while the peer is alive.                               */
/*******************************************************************************/
void Link_ReportTemps(int16_t in_c100, uint8_t in_ok, int16_t ext_c100, uint8_t ext_ok) {
	link_temp_one(0, in_c100, in_ok);
	link_temp_one(1, ext_c100, ext_ok);
}
/*******************************************************************************/
/* Reliable-message service: sends queued messages and drives their ACK/retry  */
/* cycle. Call from the main loop; timing uses the real 10 ms tick.            */
/*******************************************************************************/
void Link_Poll(void) {
	uint8_t i;

	for(i = 0; i < LNK_TXQ_N; i++) {
		lnk_txq_t xdata *q = &lnk_txq[i];

		if(q->state == LNK_TXQ_FREE) continue;
		if(q->session != link_session) {
			q->state = LNK_TXQ_FREE;
			continue;
		}
		if((int16_t)(BTN_Tick10ms() - q->deadline) < 0) continue;

		if(q->state == LNK_TXQ_WAIT_SEND) {
			Uart1_SendFrame(q->seq, q->flags, 0, q->cmd, q->type, q->len, q->payload);
			q->tries    = 1;
			q->state    = LNK_TXQ_WAIT_RSP;
			q->deadline = (uint16_t)(BTN_Tick10ms() + LNK_TXQ_RSP_TIMEOUT);
			continue;
		}
		/* WAIT_RSP: retry the same SEQ, then wait a cycle.                     */
		if(q->tries < LNK_TXQ_TRIES) {
			Uart1_SendFrame(q->seq, q->flags, 0, q->cmd, q->type, q->len, q->payload);
			q->tries++;
			q->deadline = (uint16_t)(BTN_Tick10ms() + LNK_TXQ_RSP_TIMEOUT);
		}
		else {
			q->tries    = 0;
			q->state    = LNK_TXQ_WAIT_SEND;
			q->deadline = (uint16_t)(BTN_Tick10ms() + LNK_TXQ_CYCLE);
		}
	}

	/* state dump: send the current frame, advance on the reply                 */
	if(link_dump_active) {
		if((int16_t)(BTN_Tick10ms() - link_dump_deadline) >= 0) {
			if(link_dump_tries < LNK_DUMP_TRIES) {
				link_dump_send(link_dump_idx);
				link_dump_tries++;
				link_dump_deadline = (uint16_t)(BTN_Tick10ms() + LNK_DUMP_RSP_TIMEOUT);
			}
			else {
				link_dump_tries    = 0;
				link_dump_deadline = (uint16_t)(BTN_Tick10ms() + LNK_DUMP_CYCLE);
			}
		}
	}
}
/*******************************************************************************/
/* Last known network status (LNK_NET_*), for the UI.                          */
/*******************************************************************************/
uint8_t Link_NetStatus(void) {
	return link_net_status;
}
/************************************************************************************************************************/
/* Last wireless climate sensor (WS) telemetry from the ZT3L (ZCL x100): temperature 0.01 degC, humidity 0.01 %RH.      */
/* RAM only; freshness below is the only validity gate for BOTH values.                                                 */
/************************************************************************************************************************/
int16_t Link_WsTemp(void) {
	return ws_temp_c100;
}
uint16_t Link_WsHumid(void) {
	return ws_humid_c100;
}
/***********************************************************************************************************************/
/* Freshness: 1 only while a WS TEMPERATURE arrived less than WS_FRESH_SECS ago. The humidity rides on the same flag - */
/* no counter of its own. Link_Init boots STALE, so the first temperature arrival is what opens the window.            */
/***********************************************************************************************************************/
uint8_t Link_WsFresh(void) {
	return (uint8_t)(ws_age < WS_FRESH_SECS);
}
/********************************************************************************/
/* Consume an incoming System Mode command (set by h_sysmode). Returns 1 and    */
/* writes the requested mode; 0 if there is nothing pending.                    */
/********************************************************************************/
uint8_t Link_TakeSysMode(uint8_t xdata *mode) {
	if(!link_sysmode_pending) return 0;
	*mode = link_sysmode_val;
	link_sysmode_pending = 0;
	return 1;
}
/********************************************************************************/
/* Consume an incoming keypad-lock change (LNK_CMD_KEY_LOCK, set by h_setting). */
/* Returns 1 once per change; main() blinks the padlock while OFF.              */
/********************************************************************************/
uint8_t Link_TakeLockChanged(void) {
	if(!link_lock_changed) return 0;
	link_lock_changed = 0;
	return 1;
}
/**********************************************************************************************************************/
/* Consume an incoming scalar-setting change (set by h_setting after a real change + save). Returns 1 once and        */
/* writes the LNK_CMD_* id; main() shows the value for 7 s with the "Set" indicator.                                  */
/**********************************************************************************************************************/
uint8_t Link_TakeSettingChanged(uint8_t xdata *cmd) {
	if(!link_setting_changed) return 0;
	*cmd = link_setting_cmd;
	link_setting_changed = 0;
	return 1;
}
/********************************************************************************/
/* Report System Mode (0x0A) and Running State (0x09) when they change. Call    */
/* from the main loop; both are sent with ACK and retried until confirmed.      */
/********************************************************************************/
void Link_UpdateStates(uint8_t power_on, uint8_t relay_on) {
	uint8_t v;

	v = power_on ? LNK_SYSMODE_HEAT : LNK_SYSMODE_OFF;
	if(link_sysmode_tx == 0xFF) {
		link_sysmode_tx = v;           /* first call after the handshake: record */
	}
	else if(v != link_sysmode_tx &&
	        lnk_txq_post(LNK_CMD_SYSTEM_MODE, UART_F_ACK | UART_F_CMD,
	                     LNK_T_ENUM, 1, &v, 0)) {
		link_sysmode_tx = v;
	}

	v = relay_on ? LNK_RUN_HEAT : 0x00;
	link_running_current = v;
	if(link_running_tx == 0xFF) {
		link_running_tx = v;
	}
	else if(v != link_running_tx) {
		link_tx[0] = 0;                  /* bitmap16: high byte             */
		link_tx[1] = v;                  /* low byte = LNK_RUN_*            */
		if(lnk_txq_post(LNK_CMD_RUNNING, UART_F_ACK | UART_F_CMD,
		                LNK_T_BITMAP16, 2, link_tx, 0))
			link_running_tx = v;
	}
}
/********************************************************************************/
/* Ask the peer to restore factory defaults (e.g. to (re)join the network).     */
/* Sent reliably (ACK|CMD, DLC=0), retried until confirmed.                     */
/********************************************************************************/
uint8_t Link_SendFactoryReset(void) {
	uint8_t accepted;

	accepted = lnk_txq_post(LNK_CMD_FACTORY_RESET,
	                       UART_F_ACK | UART_F_CMD, 0, 0, 0, 0);
	if(accepted) link_factory_pending = 0;
	else link_factory_pending = 1;
	return accepted;
}
/********************************************************************************/
/* Send one changed setting to the peer reliably (ACK|CMD, retried until        */
/* confirmed). i16 values go as 2 big-endian bytes; u8/enum/bool as 1 byte.     */
/********************************************************************************/
uint8_t Link_SendI16(uint8_t cmd, int16_t v) {
	uint8_t accepted;

	link_tx[0] = (uint8_t)((uint16_t)v >> 8);
	link_tx[1] = (uint8_t)v;
	accepted = lnk_txq_post(cmd, UART_F_ACK | UART_F_CMD, LNK_T_I16, 2, link_tx, 0);
	if(!accepted) link_settings_retry = 0;
	return accepted;
}

uint8_t Link_SendU8(uint8_t cmd, uint8_t type, uint8_t v) {
	uint8_t accepted;

	accepted = lnk_txq_post(cmd, UART_F_ACK | UART_F_CMD, type, 1, &v, 0);
	if(!accepted) link_settings_retry = 0;
	return accepted;
}

static void lnk_schedule_pack(uint8_t day) {
	schedule_t xdata *row = Sched_DayRow(day);
	uint8_t i;

	for(i = 0; i < LNK_SCHED_N; i++) {
		lnk_put16(link_tx + i * LNK_SCHED_ENTRY,     row[i].minute);
		lnk_put16(link_tx + i * LNK_SCHED_ENTRY + 2,
		          (uint16_t)row[i].temperature);
	}
}

/********************************************************************************/
/* Send one weekday's schedule (24 B) to the peer reliably. `day` is 0..6       */
/* (Mon..Sun).                                                                  */
/********************************************************************************/
uint8_t Link_SendSchedule(uint8_t day) {
	lnk_schedule_pack(day);
	return lnk_txq_post((uint8_t)(LNK_CMD_SCHED_MON + day),
	             UART_F_ACK | UART_F_CMD, LNK_T_RAW,
	             LNK_SCHED_DAY_LEN, link_tx, 0);
}
/********************************************************************************/
/* Build and send one state-dump frame (step 0..LNK_DUMP_N-1) as a report.      */
/* SDCC 4.6 has no #pragma enable_warning, so this disable lasts to the end of  */
/* the file: it covers link_dump_send AND h_setting, both dense switches whose  */
/* warning 110 ("conditional flow changed by optimizer") is harmless. Keep any  */
/* future code that must be warning-checked ABOVE this point.                   */
/********************************************************************************/
#pragma disable_warning 110
typedef struct {
	uint8_t off;
	uint8_t kind;       /* 0 = wire u8, 1 = wire i8, 2 = wire i16             */
	uint8_t special;    /* 4 = keypad-lock field: raise link_lock_changed     */
	uint8_t err;        /* status when the semantic gate rejects the write    */
} hset_item_t;

static const hset_item_t code hset_items[] = {
	{ offsetof(settings_t, sensosUsed), 0, 0, LNK_ST_VALUE },
	{ offsetof(settings_t, localTemperatureCalibration), 1, 0, LNK_ST_RANGE },
	{ offsetof(settings_t, outTemperatureCalibration), 1, 0, LNK_ST_RANGE },
	{ offsetof(settings_t, deadBand), 1, 0, LNK_ST_RANGE },
	{ offsetof(settings_t, progMode), 0, 0, LNK_ST_VALUE },
	{ offsetof(settings_t, minHeatSetpointLimit), 2, 0, LNK_ST_RANGE },
	{ offsetof(settings_t, maxHeatSetpointLimit), 2, 0, LNK_ST_RANGE },
	{ offsetof(settings_t, occupiedHeatingSetpoint), 2, 0, LNK_ST_RANGE },
	{ offsetof(settings_t, keypadLockout), 0, 4, LNK_ST_VALUE },
	{ offsetof(settings_t, currentLevel_day), 0, 0, LNK_ST_RANGE },
	{ offsetof(settings_t, currentLevel_night), 0, 0, LNK_ST_RANGE }
};

static const uint8_t code hset_index_map[32] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 7,
	0xff, 0xff, 0xff, 4, 0, 1, 2, 3,
	5, 6, 0xff, 0xff, 0xff, 0xff, 0xff, 8,
	0xff, 0xff, 9, 10, 0xff, 0xff, 0xff, 0xff
};

static const uint8_t code hset_cmd[11] = {
	LNK_CMD_SENSOR_SRC, LNK_CMD_CAL_ACTIVE, LNK_CMD_CAL_EXTERNAL,
	LNK_CMD_HYSTERESIS, LNK_CMD_PROG_MODE, LNK_CMD_LIMIT_MIN,
	LNK_CMD_LIMIT_MAX, LNK_CMD_SETPOINT_HEAT, LNK_CMD_KEY_LOCK,
	LNK_CMD_BRIGHT_DAY, LNK_CMD_BRIGHT_NIGHT
};

static const uint8_t code hset_type[11] = {
	LNK_T_ENUM, LNK_T_I8, LNK_T_I8, LNK_T_I8, LNK_T_BITMAP8,
	LNK_T_I16, LNK_T_I16, LNK_T_I16, LNK_T_ENUM, LNK_T_U8, LNK_T_U8
};

static uint8_t lnk_setting_prepare(uint8_t k) {
	const hset_item_t code *e = &hset_items[k];
	uint8_t xdata *field = (uint8_t xdata *)&settings + e->off;

	if(e->kind == 1) {
		link_tx[0] = *field;              /* wire i8: one raw byte           */
		return 1;
	}
	if(e->kind == 2) {
		lnk_put16(link_tx, (uint16_t)(field[0] | ((uint16_t)field[1] << 8)));
		return 2;
	}
	link_tx[0] = *field;
	return 1;
}

static void link_dump_send(uint8_t i) {
	uint8_t xdata *p = link_tx;
	uint8_t cmd  = 0;
	uint8_t type = LNK_T_RAW;
	uint8_t len  = 0;
	uint8_t k;

	if(i < 11) {
		k = i;
		cmd = hset_cmd[k];
		type = hset_type[k];
		len = lnk_setting_prepare(k);
	}
	else if(i == 11) {
		cmd = LNK_CMD_SYSTEM_MODE;
		type = LNK_T_ENUM;
		len = 1;
		p[0] = (link_sysmode_tx == 0xFF) ? LNK_SYSMODE_OFF : link_sysmode_tx;
	}
	else if(i == 12) {
		cmd = LNK_CMD_RUNNING;
		type = LNK_T_BITMAP16;
		len = 2;
		p[0] = 0;
		p[1] = link_running_current;
	}
	else {
		uint8_t day_idx = (uint8_t)(i - LNK_DUMP_SINGLES);

		cmd  = (uint8_t)(LNK_CMD_SCHED_MON + day_idx);
		type = LNK_T_RAW;
		len  = LNK_SCHED_DAY_LEN;
		lnk_schedule_pack(day_idx);
	}

	if(cmd) {
		link_dump_cmd = cmd;
		Uart1_SendFrame(link_dump_seq, UART_F_ACK | UART_F_CMD, 0, cmd, type, len, link_tx);
	}
}

void Link_RetryPending(void) {
	uint8_t len;

	if(!link_peer_alive || Ota_IsPending()) return;
	if(link_factory_pending &&
	   lnk_txq_post(LNK_CMD_FACTORY_RESET, UART_F_ACK | UART_F_CMD,
	                0, 0, 0, 0))
		link_factory_pending = 0;
	if(link_settings_retry == LINK_SETTINGS_RETRY_NONE) return;

	len = lnk_setting_prepare(link_settings_retry);
	if(lnk_txq_post(hset_cmd[link_settings_retry], UART_F_ACK | UART_F_CMD,
	                hset_type[link_settings_retry], len, link_tx, 0)) {
		if(++link_settings_retry >= 11) link_settings_retry = LINK_SETTINGS_RETRY_NONE;
	}
	/* Post refused (queue full / silent peer): HOLD the index. Rewinding to 0 */
	/* re-posted already-accepted settings forever and replaced in-flight      */
	/* WAIT_RSP slots with fresh SEQs at the caller's rate; the walk resumes   */
	/* when a slot frees and still stops at LINK_SETTINGS_RETRY_NONE.          */
}

/*********************************************************************************/
/* Handler: scalar settings written from the network (ZT3L -> CA51F2).           */
/* Cmd (ACK=1, CMD=1, DLC=1/2): store in `settings`, persist. Rep (ACK=0): same, */
/* applied silently. The semantic gate runs AFTER the field write and rolls the  */
/* field back when the whole image would become invalid (out-of-range value ->   */
/* LNK_ST_RANGE, bad enum -> LNK_ST_VALUE); settings_save() no longer validates. */
/* The image is only rewritten when the value actually changed.                  */
/*********************************************************************************/
static void h_setting(link_ctx_t xdata *cx) {
	const uart_frame_t xdata *f = cx->f;
	link_res_t xdata *r = &link_result;
	const hset_item_t code *e;
	uint8_t k;
	int16_t v;
	int16_t old;
	uint8_t xdata *field;
	/* descriptor bytes cached: each e->field read is a code-space movc    */
	uint8_t kind;
	uint8_t special;
	uint8_t off;

	k = f->cmd < 32 ? hset_index_map[f->cmd] : 0xff;
	if(k == 0xff) {
		r->status = LNK_ST_UNSUPPORT;
		return;
	}
	e = &hset_items[k];
	kind    = e->kind;
	special = e->special;
	off     = e->off;
	if(kind == 0) v = f->payload[0];
	else if(kind == 1) v = (int16_t)(int8_t)f->payload[0];   /* wire i8      */
	else v = (int16_t)lnk_get16(f->payload);
	field = (uint8_t xdata *)&settings + off;
	if(kind == 0) old = *field;
	else if(kind == 1) old = (int8_t)*field;
	else old = (int16_t)(field[0] | ((uint16_t)field[1] << 8));
	if(old == v) return;
	if(kind == 0) *field = (uint8_t)v;
	else if(kind == 1) *field = (uint8_t)(int8_t)v;
	else {
		field[0] = (uint8_t)v;
		field[1] = (uint8_t)(v >> 8);
	}
	/* Ingress gate: the write must keep the WHOLE image semantically valid, */
	/* otherwise roll this field back and report the descriptor's status.    */
	if(!settings_ranges_valid(&settings)) {
		if(kind == 0) *field = (uint8_t)old;
		else if(kind == 1) *field = (uint8_t)(int8_t)old;
		else {
			field[0] = (uint8_t)old;
			field[1] = (uint8_t)(old >> 8);
		}
		r->status = e->err;
		return;
	}
	if(!settings_save()) {
		r->status = LNK_ST_FLASH;
		return;
	}
	/* The keypad lock has its OWN padlock channel and no 7 s value window (it */
	/* used to print 0/1/2 on the big digits): main() redraws SYM_LOCK and     */
	/* spikes the brightness from here and nowhere else.                       */
	if(special == 4) {
		link_lock_changed = 1;
		return;
	}
	/* The value really changed and is persisted: announce it so main() can show */
	/* it for 7 s with the "Set" indicator (same window as an arrow edit).       */
	link_setting_cmd     = f->cmd;
	link_setting_changed = 1;
}
