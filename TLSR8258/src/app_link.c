#include "app_main.h"
#include "app_link.h"
#include "app_ota.h"

/*********************************************************************************/
/* CA51F2 <-> ZT3L link protocol (serial.md). ZT3L is the boot-handshake         */
/* initiator; timing runs off clock_time() (1 us tick) polled from app_task.     */
/*********************************************************************************/
/* clock_time() counts 16 MHz ticks (sys_tick_per_us = 16), NOT us, so express */
/* the delays through the SDK's 16M-based constants.                           */
#define LINK_BOOT_DELAY_TICKS   (1   * CLOCK_16M_SYS_TIMER_CLK_1S)   /* 1 s       */
#define LINK_REQ_TIMEOUT_TICKS  (100 * CLOCK_16M_SYS_TIMER_CLK_1MS)  /* 100 ms    */
#define LINK_REQ_CYCLE_TICKS    (30  * CLOCK_16M_SYS_TIMER_CLK_1S)   /* 30 s      */
#define LINK_REQ_TRIES          3                                    /* per cycle */

#define LNK_REQ_IDLE         0
#define LNK_REQ_WAIT_SEND    1
#define LNK_REQ_WAIT_RSP     2

#define LINK_STATE_REQ_WAIT_PENDING 2

/* Reliable outgoing queue (mirrors the CA51F2 lnk_txq): ACK + retry the same  */
/* SEQ 3x/100 ms, then repeat the cycle every 30 s until the peer replies.     */
#define LINK_TXQ_N             4
#define LINK_TXQ_PAYLOAD       LNK_SCHED_DAY_LEN
#define LINK_TXQ_RSP_TIMEOUT   (100 * CLOCK_16M_SYS_TIMER_CLK_1MS)
#define LINK_TXQ_CYCLE         (30  * CLOCK_16M_SYS_TIMER_CLK_1S)
#define LINK_TXQ_TRIES         3

#define LINK_TXQ_FREE          0
#define LINK_TXQ_WAIT_SEND     1
#define LINK_TXQ_WAIT_RSP      2

typedef struct {
	uint8_t  state;                        /* LINK_TXQ_*                    */
	uint8_t  flags;                        /* frame flags (ACK / ACK|CMD)   */
	uint8_t  cmd;
	uint8_t  type;
	uint8_t  len;
	uint8_t  seq;
	uint8_t  tries;
	uint8_t  session;
	uint32_t deadline;                     /* clock_time() of next action   */
	uint8_t  payload[LINK_TXQ_PAYLOAD];
} link_txq_t;

static link_txq_t link_txq[LINK_TXQ_N];

#define LINK_PENDING_N 13
typedef struct {
	uint8_t used;
	uint8_t cmd;
	uint8_t type;
	uint8_t len;
	uint8_t seq;
	int16_t value;
} link_pending_setting_t;

static link_pending_setting_t link_pending_settings[LINK_PENDING_N];
static uint8_t link_pending_schedule_mask;
static uint8_t link_pending_schedule_seq[7];

static uint8_t  link_seq;                              /* own outgoing SEQ      */
static uint8_t  link_session;
static uint8_t  link_tx[UART_FRAME_PAYLOAD];

typedef struct {
	uint8_t valid;
	uint8_t seq;
	uint8_t cmd;
	uint8_t status;
	uint8_t type;
	uint8_t len;
	uint8_t payload[UART_FRAME_PAYLOAD];
} link_dedup_t;

static link_dedup_t link_dedup;

static uint8_t  link_info_state;
static uint8_t  link_info_tries;
static uint8_t  link_info_seq;                         /* SEQ of the Info Req   */
static uint32_t link_info_deadline;
static uint8_t  link_handshake_done;
static uint8_t  link_net_reported;                     /* last pushed status    */
static uint8_t  link_state_req_done;                   /* config requested      */
static uint16_t link_ca51f2_ver;                       /* CA51F2 APP_VERSION    */
static uint8_t  link_ca51f2_alive;                     /* Info RSP received     */
static uint8_t  link_ota_rsp_status = 0xFF;            /* OTA_START RSP status  */
static uint8_t  link_ota_rsp_seq;

static uint8_t link_next_seq(void) {
	if (++link_seq == 0) link_seq = 1;
	return link_seq;
}

static uint8_t link_read_net_status(void) {
	return zb_isDeviceJoinedNwk() ? LNK_NET_CONNECTED : LNK_NET_FREE;
}

static uint8_t link_time_after(uint32_t deadline) {
	return ((int32_t)(clock_time() - deadline) >= 0) ? 1 : 0;
}

static uint8_t link_txq_post(uint8_t cmd, uint8_t flags, uint8_t type, uint8_t len,
                             const uint8_t *payload) {
	uint8_t i;
	uint8_t j;

	if (len > LINK_TXQ_PAYLOAD) return 0;

	for (i = 0; i < LINK_TXQ_N; i++) {
		if (link_txq[i].state != LINK_TXQ_FREE && link_txq[i].cmd == cmd) {
			link_txq[i].flags    = flags;
			link_txq[i].type     = type;
			link_txq[i].len      = len;
			for (j = 0; j < len; j++) link_txq[i].payload[j] = payload[j];
			link_txq[i].seq      = link_next_seq();
			link_txq[i].tries    = 0;
			link_txq[i].session  = link_session;
			link_txq[i].state    = LINK_TXQ_WAIT_SEND;
			link_txq[i].deadline = clock_time();
			return 1;
		}
	}
	for (i = 0; i < LINK_TXQ_N; i++) {
		if (link_txq[i].state == LINK_TXQ_FREE) {
			link_txq[i].flags    = flags;
			link_txq[i].cmd      = cmd;
			link_txq[i].type     = type;
			link_txq[i].len      = len;
			for (j = 0; j < len; j++) link_txq[i].payload[j] = payload[j];
			link_txq[i].seq      = link_next_seq();
			link_txq[i].tries    = 0;
			link_txq[i].session  = link_session;
			link_txq[i].state    = LINK_TXQ_WAIT_SEND;
			link_txq[i].deadline = clock_time();
			return 1;
		}
	}
	return 0;
}

static uint8_t link_txq_current_seq(uint8_t cmd) {
	uint8_t i;

	for(i = 0; i < LINK_TXQ_N; i++) {
		if(link_txq[i].state != LINK_TXQ_FREE && link_txq[i].cmd == cmd)
			return link_txq[i].seq;
	}
	return 0;
}

static void link_pending_setting_store(uint8_t cmd, uint8_t type, uint8_t len,
                                       uint8_t seq, int16_t value) {
	uint8_t i;

	for(i = 0; i < LINK_PENDING_N; i++) {
		if(link_pending_settings[i].used && link_pending_settings[i].cmd == cmd) {
			link_pending_settings[i].type = type;
			link_pending_settings[i].len = len;
			link_pending_settings[i].seq = seq;
			link_pending_settings[i].value = value;
			return;
		}
	}
	for(i = 0; i < LINK_PENDING_N; i++) {
		if(!link_pending_settings[i].used) {
			link_pending_settings[i].used = 1;
			link_pending_settings[i].cmd = cmd;
			link_pending_settings[i].type = type;
			link_pending_settings[i].len = len;
			link_pending_settings[i].seq = seq;
			link_pending_settings[i].value = value;
			return;
		}
	}
}

static void link_pending_schedule_store(uint8_t day, uint8_t seq) {
	if(day > 6) return;
	link_pending_schedule_mask |= (uint8_t)(1U << day);
	link_pending_schedule_seq[day] = seq;
}

static uint8_t link_pending_any(void) {
	uint8_t i;

	for(i = 0; i < LINK_PENDING_N; i++) {
		if(link_pending_settings[i].used) return 1;
	}
	return link_pending_schedule_mask != 0;
}

static uint8_t link_pending_queued(uint8_t cmd, uint8_t seq) {
	uint8_t i;

	if(!seq) return 0;
	for(i = 0; i < LINK_TXQ_N; i++) {
		if(link_txq[i].state != LINK_TXQ_FREE &&
		   link_txq[i].cmd == cmd && link_txq[i].seq == seq) return 1;
	}
	return 0;
}

static void link_pending_setting_send(uint8_t i) {
	link_pending_setting_t *pending = &link_pending_settings[i];
	uint8_t payload[2];

	if(!pending->used || link_pending_queued(pending->cmd, pending->seq)) return;
	if(pending->len == 1) {
		payload[0] = (uint8_t)pending->value;
	}
	else {
		payload[0] = (uint8_t)((uint16_t)pending->value >> 8);
		payload[1] = (uint8_t)pending->value;
	}
	if(link_txq_post(pending->cmd, UART_F_ACK | UART_F_CMD,
	                 pending->type, pending->len, payload))
		pending->seq = link_txq_current_seq(pending->cmd);
}

/* Ask the CA51F2 for its whole configuration. link_state_req_done flips to 1  */
/* ONLY when the frame actually went on the wire: the driver returns 0 while   */
/* busy (bench line "TLSR !!") or in raw/OTA mode, and a refused request must  */
/* be retried on the next poll instead of being silently forgotten.            */
static void link_state_request(void) {
	if(App_Uart_SendFrame(link_next_seq(), UART_F_ACK | UART_F_CMD,
	                      LNK_ST_OK, LNK_CMD_STATE_ALL, 0, 0, 0))
		link_state_req_done = 1;
}

static void link_pending_retry(void) {
	uint8_t i;

	if(!link_handshake_done || App_Ota_IsActive()) return;
	for(i = 0; i < LINK_PENDING_N; i++) link_pending_setting_send(i);
	for(i = 0; i < 7; i++) {
		if((link_pending_schedule_mask & (uint8_t)(1U << i)) &&
		   !link_pending_queued((uint8_t)(LNK_CMD_SCHED_MON + i),
	                            link_pending_schedule_seq[i]))
			App_Link_SendSchedule(i);
	}
	/* WAIT_PENDING (2) = requested only after the pending writes drained,     */
	/* 0 = re-armed by a handshake; in both states keep attempting until the   */
	/* send is accepted - a busy driver must not lose the dump.                */
	if(link_state_req_done != 1 && !link_pending_any()) {
		link_state_request();
	}
}

static void link_pending_response(uint8_t cmd, uint8_t seq) {
	uint8_t i;

	for(i = 0; i < LINK_PENDING_N; i++) {
		if(link_pending_settings[i].used &&
		   link_pending_settings[i].cmd == cmd &&
		   link_pending_settings[i].seq == seq) {
			link_pending_settings[i].used = 0;
			return;
		}
	}
	if(cmd >= LNK_CMD_SCHED_MON && cmd <= LNK_CMD_SCHED_SUN) {
		i = (uint8_t)(cmd - LNK_CMD_SCHED_MON);
		if(link_pending_schedule_seq[i] == seq) {
			link_pending_schedule_mask &= (uint8_t)~(1U << i);
			link_pending_schedule_seq[i] = 0;
		}
	}
}

static void link_txq_poll(void) {
	uint8_t i;

	for (i = 0; i < LINK_TXQ_N; i++) {
		link_txq_t *q = &link_txq[i];

		if (q->state == LINK_TXQ_FREE) continue;
		if (q->session != link_session) {
			q->state = LINK_TXQ_FREE;
			continue;
		}
		if (!link_time_after(q->deadline)) continue;

		if (q->state == LINK_TXQ_WAIT_SEND) {
			if (App_Uart_SendFrame(q->seq, q->flags, LNK_ST_OK, q->cmd, q->type, q->len, q->payload)) {
				q->tries    = 1;
				q->state    = LINK_TXQ_WAIT_RSP;
				q->deadline = clock_time() + LINK_TXQ_RSP_TIMEOUT;
			}
			continue;
		}
		if (q->tries < LINK_TXQ_TRIES) {
			if (App_Uart_SendFrame(q->seq, q->flags, LNK_ST_OK, q->cmd, q->type, q->len, q->payload)) {
				q->tries++;
				q->deadline = clock_time() + LINK_TXQ_RSP_TIMEOUT;
			}
			continue;
		}
		q->tries    = 0;
		q->state    = LINK_TXQ_WAIT_SEND;
		q->deadline = clock_time() + LINK_TXQ_CYCLE;
	}
}

/* (Re)arm the Info handshake: reset the flags, drop pending reliable sends and
   schedule the first Info request `delay_ticks` from now. Used on our own boot
   and when the CA51F2 reports that it (re)started. */
static void link_handshake_restart(uint32_t delay_ticks) {
	uint8_t i;

	if(++link_session == 0) link_session = 1;
	link_dedup.valid = 0;
	link_info_state     = LNK_REQ_WAIT_SEND;
	link_info_tries     = 0;
	link_info_seq       = link_next_seq();
	link_info_deadline  = clock_time() + delay_ticks;
	link_handshake_done = 0;
	link_state_req_done = 0;
	link_ca51f2_alive   = 0;
	link_ca51f2_ver     = 0;
	for (i = 0; i < LINK_TXQ_N; i++) link_txq[i].state = LINK_TXQ_FREE;
}

void App_Link_Init(void) {
	uint8_t i;

	link_seq = 0;
	link_dedup.valid = 0;
	for(i = 0; i < LINK_PENDING_N; i++) link_pending_settings[i].used = 0;
	link_pending_schedule_mask = 0;
	for(i = 0; i < 7; i++) link_pending_schedule_seq[i] = 0;
	link_ota_rsp_status = 0xFF;
	link_ota_rsp_seq = 0;
	link_handshake_restart(LINK_BOOT_DELAY_TICKS);
	link_net_reported = link_read_net_status();
}

/* Info request: 3 attempts 100 ms apart (same SEQ), then repeat every 30 s.    */
static void link_info_request(void) {
	if (!link_time_after(link_info_deadline)) return;

	if (link_info_state == LNK_REQ_WAIT_SEND) {
		if (App_Uart_SendFrame(link_info_seq, UART_F_ACK, 0, LNK_CMD_INFO, 0, 0, 0)) {
			link_info_tries    = 1;
			link_info_state    = LNK_REQ_WAIT_RSP;
			link_info_deadline = clock_time() + LINK_REQ_TIMEOUT_TICKS;
		}
		return;
	}
	/* WAIT_RSP: retry the same SEQ up to LINK_REQ_TRIES, then wait a cycle.    */
	if (link_info_tries < LINK_REQ_TRIES) {
		if (App_Uart_SendFrame(link_info_seq, UART_F_ACK, 0, LNK_CMD_INFO, 0, 0, 0)) {
			link_info_tries++;
			link_info_deadline = clock_time() + LINK_REQ_TIMEOUT_TICKS;
		}
		return;
	}
	link_info_tries    = 0;
	link_info_state    = LNK_REQ_WAIT_SEND;
	link_info_deadline = clock_time() + LINK_REQ_CYCLE_TICKS;
}

/* Push a Net-status report only when the status changed (after the handshake). */
static void link_net_report(void) {
	uint8_t st;

	if (!link_handshake_done) return;
	st = link_read_net_status();
	if (st == link_net_reported) return;
	if (App_Uart_SendFrame(link_next_seq(), 0x00, 0, LNK_CMD_NET_STATUS, LNK_T_ENUM, 1, &st)) {
		link_net_reported = st;
		APP_DEBUG(DEBUG_ZB_CB_EN, "net report %d\r\n", st);
	}
}

void App_Link_Poll(void) {
	if (link_info_state != LNK_REQ_IDLE &&
	    (!App_Ota_IsActive() || App_Ota_InfoAllowed())) {
		link_info_request();
	}
	if (!App_Ota_IsActive()) {
		link_txq_poll();
		link_pending_retry();
		link_net_report();
	}
}

/* Send the time to the CA51F2 (reliably): utc(4 BE) + local(4 BE).            */
uint8_t App_Link_SendTime(uint32_t utc, uint32_t local) {
	uint8_t p[8];

	p[0] = (uint8_t)(utc >> 24);
	p[1] = (uint8_t)(utc >> 16);
	p[2] = (uint8_t)(utc >> 8);
	p[3] = (uint8_t)utc;
	p[4] = (uint8_t)(local >> 24);
	p[5] = (uint8_t)(local >> 16);
	p[6] = (uint8_t)(local >> 8);
	p[7] = (uint8_t)local;
	return link_txq_post(LNK_CMD_TIME, UART_F_ACK | UART_F_CMD, LNK_T_RAW, 8, p);
}

/* Forward a scalar setting to the CA51F2 reliably (ACK|CMD, retried).      */
uint8_t App_Link_SendSettingI16(uint8_t cmd, int16_t v) {
	uint8_t p[2];
	uint8_t accepted;

	p[0] = (uint8_t)((uint16_t)v >> 8);
	p[1] = (uint8_t)v;
	if(!link_handshake_done) {
		link_pending_setting_store(cmd, LNK_T_I16, 2, 0, v);
		return 0;
	}
	accepted = link_txq_post(cmd, UART_F_ACK | UART_F_CMD, LNK_T_I16, 2, p);
	link_pending_setting_store(cmd, LNK_T_I16, 2,
	                           accepted ? link_txq_current_seq(cmd) : 0, v);
	return accepted;
}

uint8_t App_Link_SendSettingU8(uint8_t cmd, uint8_t type, uint8_t v) {
	uint8_t accepted;

	if(!link_handshake_done) {
		link_pending_setting_store(cmd, type, 1, 0, v);
		return 0;
	}
	accepted = link_txq_post(cmd, UART_F_ACK | UART_F_CMD, type, 1, &v);
	link_pending_setting_store(cmd, type, 1,
	                           accepted ? link_txq_current_seq(cmd) : 0, v);
	return accepted;
}

/* Forward one wireless climate sensor (WS) value to the CA51F2. Telemetry,     */
/* not a setting: reliable (ACK|CMD, retried), but NOT stashed in the pending-  */
/* settings buffer - while unsent the newest value simply replaces the old one  */
/* in the queue, and a down peer just drops it (the next sensor report arrives  */
/* within minutes).                                                             */
uint8_t App_Link_SendWsTemp(int16_t v) {
	uint8_t p[2];

	p[0] = (uint8_t)((uint16_t)v >> 8);
	p[1] = (uint8_t)v;
	return link_txq_post(LNK_CMD_TEMP_NET, UART_F_ACK | UART_F_CMD, LNK_T_I16, 2, p);
}

uint8_t App_Link_SendWsHumid(uint16_t v) {
	uint8_t p[2];

	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
	return link_txq_post(LNK_CMD_HUMID_NET, UART_F_ACK | UART_F_CMD, LNK_T_U16, 2, p);
}

/* Forward one schedule day (0 = Monday .. 6 = Sunday) to the CA51F2 reliably:   */
/* 6 entries x 4 B BE = { u16 minute, i16 temperature x100 }.                    */
uint8_t App_Link_SendSchedule(uint8_t day) {
	uint8_t p[LNK_SCHED_DAY_LEN];
	const schedule_t *row;
	uint8_t i;
	uint8_t accepted;

	if(day > 6) return 0;
	if(!link_handshake_done) {
		link_pending_schedule_store(day, 0);
		return 0;
	}

	row = (const schedule_t *)&g_zcl_scheduleData;
	row += (uint16_t)day * SCHED_PERIODS;

	for (i = 0; i < SCHED_PERIODS; i++) {
		p[i * LNK_SCHED_ENTRY]     = (uint8_t)(row[i].minute >> 8);
		p[i * LNK_SCHED_ENTRY + 1] = (uint8_t)row[i].minute;
		p[i * LNK_SCHED_ENTRY + 2] = (uint8_t)((uint16_t)row[i].temperature >> 8);
		p[i * LNK_SCHED_ENTRY + 3] = (uint8_t)row[i].temperature;
	}
	accepted = link_txq_post((uint8_t)(LNK_CMD_SCHED_MON + day),
	                         UART_F_ACK | UART_F_CMD, LNK_T_RAW,
	                         LNK_SCHED_DAY_LEN, p);
	link_pending_schedule_store(day, accepted ?
	                            link_txq_current_seq((uint8_t)(LNK_CMD_SCHED_MON + day)) : 0);
	return accepted;
}

/* Force the next Net-status report (the CA51F2's view may be stale after a     */
/* ZDO leave): invalidate the "last reported" value so link_net_report() sends. */
void App_Link_NetForce(void) {
	link_net_reported = 0xFF;
}

/* CA51F2 firmware version from its Info payload (0 until the first Info RSP).  */
uint16_t App_Link_Ca51f2Ver(void) {
	return link_ca51f2_ver;
}

/* 1 once the CA51F2 has answered our Info request.                            */
uint8_t App_Link_Ca51f2Alive(void) {
	return link_ca51f2_alive;
}

/* Ask the CA51F2 to start the OTA data phase (bin_size u32 BE + ver u16 BE).   */
uint8_t App_Link_AllocSeq(void) {
	return link_next_seq();
}

uint8_t App_Link_SendOtaStartWithSeq(uint8_t seq, uint32_t size, uint16_t ver) {
	uint8_t p[6];
	p[0] = (uint8_t)(size >> 24); p[1] = (uint8_t)(size >> 16);
	p[2] = (uint8_t)(size >> 8);  p[3] = (uint8_t)size;
	p[4] = (uint8_t)(ver >> 8);   p[5] = (uint8_t)ver;
	link_ota_rsp_seq = seq;
	if(!App_Uart_SendFrame(seq, UART_F_ACK | UART_F_CMD,
	                      LNK_ST_OK, LNK_CMD_OTA_START, LNK_T_RAW, 6, p)) {
		link_ota_rsp_seq = 0;
		return 0;
	}
	return 1;
}

void App_Link_SendOtaStart(uint32_t size, uint16_t ver) {
	(void)App_Link_SendOtaStartWithSeq(App_Link_AllocSeq(), size, ver);
}

/* Last OTA_START reply status (0xFF until a reply arrives).                    */
uint8_t App_Link_OtaRspStatus(void) {
	return link_ota_rsp_status;
}

void App_Link_OtaRspClear(void) {
	link_ota_rsp_status = 0xFF;
	link_ota_rsp_seq = 0;
}

void App_Link_RestartInfo(void) {
	link_handshake_restart(0);
}

/* Big-endian i16 from a 2-byte payload.                                       */
static int16_t app_link_i16(const uint8_t *p) {
	return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}

/********************************************************************************/
/* Apply an unsolicited report: the CA51F2's state dump (LNK_CMD_STATE_ALL).    */
/********************************************************************************/
static uint8_t app_link_apply(const uart_frame_t *f) {
	uint8_t  day;
	uint8_t  i;

	switch (f->cmd) {
	case LNK_CMD_TEMP_LOCAL:
		if (f->len == 2) {
			int16_t v = app_link_i16(f->payload);
			g_zcl_thermostatAttrs.localTemperature = v;
			/* raw x100 (2349 == 23.49 degC); OFF compiles the call out */
			APP_DEBUG(DEBUG_TEMPIN_EN, "Temp in:  %d.%d°C 0x%08x\r\n", (int)(v/100), (uint16_t)(v%100), clock_time());
		}
		break;
	case LNK_CMD_TEMP_OUTDOOR:
		if (f->len == 2) {
			int16_t v = app_link_i16(f->payload);
			g_zcl_thermostatAttrs.outDoorTemperature = v;
			APP_DEBUG(DEBUG_TEMPOUT_EN, "Temp out: %d.%d°C 0x%08x\r\n", (int)(v/100), (uint16_t)(v%100), clock_time());
		}
		break;
	case LNK_CMD_SENSOR_SRC:
		if (f->len == 1) g_zcl_thermostatAttrs.sensor_used = f->payload[0];
		break;
	case LNK_CMD_CAL_ACTIVE:
		if (f->len == 1) g_zcl_thermostatAttrs.localTemperatureCalibration = (int8_t)f->payload[0];
		break;
	case LNK_CMD_CAL_EXTERNAL:
		if (f->len == 1) g_zcl_thermostatAttrs.extTemperatureCalibration = (int8_t)f->payload[0];
		break;
	case LNK_CMD_HYSTERESIS:
		if (f->len == 1) g_zcl_thermostatAttrs.dead_band = (int8_t)f->payload[0];
		break;
	case LNK_CMD_PROG_MODE:
		if (f->len == 1) g_zcl_thermostatAttrs.manual_progMode = f->payload[0];
		break;
	case LNK_CMD_SYSTEM_MODE:
		if (f->len == 1) g_zcl_thermostatAttrs.systemMode = f->payload[0];
		break;
	case LNK_CMD_RUNNING:
		if (f->len == 2)
			g_zcl_thermostatAttrs.runningState =
				(uint16_t)(((uint16_t)f->payload[0] << 8) | f->payload[1]);
		break;
	case LNK_CMD_LIMIT_MIN:
		if (f->len == 2) g_zcl_thermostatAttrs.minHeatSetpointLimit = app_link_i16(f->payload);
		break;
	case LNK_CMD_LIMIT_MAX:
		if (f->len == 2) g_zcl_thermostatAttrs.maxHeatSetpointLimit = app_link_i16(f->payload);
		break;
	case LNK_CMD_SETPOINT_HEAT:
		if (f->len == 2) g_zcl_thermostatAttrs.occupiedHeatingSetpoint = app_link_i16(f->payload);
		break;
	case LNK_CMD_KEY_LOCK:
		/* Tri-state 0=FREE / 1=EX_OFF / 2=FULL.                               */
		if (f->len == 1 && f->payload[0] <= 2)
			g_zcl_thermostatAttrs.keypadLockout = f->payload[0];
		else
			return LNK_ST_RANGE;
		break;
	case LNK_CMD_BRIGHT_DAY:
		if (f->len == 1) g_zcl_levelAttrs.currentLevelA = f->payload[0];
		break;
	case LNK_CMD_BRIGHT_NIGHT:
		if (f->len == 1) g_zcl_levelAttrs.currentLevelB = f->payload[0];
		break;
	case LNK_CMD_SCHED_MON: case LNK_CMD_SCHED_TUE: case LNK_CMD_SCHED_WED:
	case LNK_CMD_SCHED_THU: case LNK_CMD_SCHED_FRI: case LNK_CMD_SCHED_SAT:
	case LNK_CMD_SCHED_SUN:
		if (f->len == LNK_SCHED_DAY_LEN) {
			schedule_t *row = (schedule_t *)&g_zcl_scheduleData;
			day = (uint8_t)(f->cmd - LNK_CMD_SCHED_MON);
			row += (uint16_t)day * LNK_SCHED_N;
			for (i = 0; i < LNK_SCHED_N; i++) {
				row[i].minute      = (uint16_t)(((uint16_t)f->payload[i * 4] << 8) | f->payload[i * 4 + 1]);
				row[i].temperature = (int16_t)(((uint16_t)f->payload[i * 4 + 2] << 8) | f->payload[i * 4 + 3]);
			}
		}
		break;
	default:
		return LNK_ST_UNSUPPORT;
	}
    return LNK_ST_OK;
}

static uint8_t app_link_dedup_replay(const uart_frame_t *f) {
    if(!link_dedup.valid || link_dedup.cmd != f->cmd ||
       link_dedup.seq != f->seq) return 0;
    App_Uart_SendFrame(f->seq, UART_F_RSP, link_dedup.status, f->cmd,
                       link_dedup.type, link_dedup.len, link_dedup.payload);
    return 1;
}

static void app_link_reply(const uart_frame_t *f, uint8_t status,
                           uint8_t type, uint8_t len, const uint8_t *payload) {
    uint8_t i;

    if(f->flags & UART_F_ACK) {
        link_dedup.valid = 1;
        link_dedup.seq = f->seq;
        link_dedup.cmd = f->cmd;
        link_dedup.status = status;
        link_dedup.type = type;
        link_dedup.len = len;
        for(i = 0; i < len; i++) link_dedup.payload[i] = payload[i];
    }
    if(f->flags & UART_F_ACK)
        App_Uart_SendFrame(f->seq, UART_F_RSP, status, f->cmd,
                           type, len, payload);
}

static uint8_t app_link_shape(const uart_frame_t *f, uint8_t flags,
                              uint8_t len, uint8_t type) {
    if(f->flags != flags) return LNK_ST_BAD_FLAGS;
    if(f->len != len) return LNK_ST_BAD_LEN;
    if(len && f->type != type) return LNK_ST_BAD_TYPE;
    return LNK_ST_OK;
}

static uint8_t app_link_validate(const uart_frame_t *f) {
    if(f->flags & (uint8_t)~(UART_F_RSP | UART_F_ACK | UART_F_CMD))
        return LNK_ST_BAD_FLAGS;
    if((f->flags & (UART_F_RSP | UART_F_ACK)) ==
       (UART_F_RSP | UART_F_ACK)) return LNK_ST_BAD_FLAGS;
    if((f->flags & UART_F_CMD) && !(f->flags & UART_F_ACK))
        return LNK_ST_BAD_FLAGS;
    if(f->len > UART_FRAME_PAYLOAD) return LNK_ST_BAD_LEN;

    if(f->flags & UART_F_RSP) {
        /* An error reply may carry a detail payload: the DLC must NOT reject   */
        /* it - a dropped RSP never matches the txq slot, so the command would  */
        /* retransmit forever while the real status is swallowed. The payload   */
        /* itself is ignored (only OK replies are parsed below).                */
        if(f->status != LNK_ST_OK) return LNK_ST_OK;
        if(f->cmd == LNK_CMD_INFO) {
            if(f->len < LNK_INFO_MODEL || f->len > LNK_INFO_MAX_LEN)
                return LNK_ST_BAD_LEN;
            return f->type == LNK_T_RAW ? LNK_ST_OK : LNK_ST_BAD_TYPE;
        }
        if(f->cmd == LNK_CMD_TIME) {
            if(f->len == 0) return LNK_ST_OK;
            return app_link_shape(f, UART_F_RSP, 8, LNK_T_RAW);
        }
        if(f->cmd == LNK_CMD_NET_STATUS)
            return app_link_shape(f, UART_F_RSP, 1, LNK_T_ENUM);
        return f->len ? LNK_ST_BAD_LEN : LNK_ST_OK;
    }

    switch(f->cmd) {
    case LNK_CMD_TIME:
        if(f->flags == UART_F_ACK) return app_link_shape(f, UART_F_ACK, 0, 0);
        return app_link_shape(f, 0, 8, LNK_T_RAW);
    case LNK_CMD_INFO:
        if(f->flags == UART_F_ACK) return app_link_shape(f, UART_F_ACK, 0, 0);
        if(f->flags != 0) return LNK_ST_BAD_FLAGS;
        if(f->len < LNK_INFO_MODEL || f->len > LNK_INFO_MAX_LEN)
            return LNK_ST_BAD_LEN;
        return f->type == LNK_T_RAW ? LNK_ST_OK : LNK_ST_BAD_TYPE;
    case LNK_CMD_TEMP_LOCAL:
    case LNK_CMD_TEMP_OUTDOOR:
        if(f->flags == UART_F_ACK) return app_link_shape(f, UART_F_ACK, 2, LNK_T_I16);
        return app_link_shape(f, 0, 2, LNK_T_I16);
    case LNK_CMD_NET_STATUS:
        if(f->flags == UART_F_ACK) return app_link_shape(f, UART_F_ACK, 0, 0);
        return app_link_shape(f, 0, 1, LNK_T_ENUM);
    case LNK_CMD_SYSTEM_MODE:
        return app_link_shape(f, UART_F_ACK | UART_F_CMD, 1, LNK_T_ENUM);
    case LNK_CMD_RUNNING:
        if(f->flags == 0) return app_link_shape(f, 0, 2, LNK_T_BITMAP16);
        return app_link_shape(f, UART_F_ACK | UART_F_CMD, 2, LNK_T_BITMAP16);
    case LNK_CMD_CAL_ACTIVE:
    case LNK_CMD_CAL_EXTERNAL:
        if(f->flags == 0) return app_link_shape(f, 0, 1, LNK_T_I8);
        return app_link_shape(f, UART_F_ACK | UART_F_CMD, 1, LNK_T_I8);
    case LNK_CMD_SETPOINT_HEAT:
    case LNK_CMD_LIMIT_MIN:
    case LNK_CMD_LIMIT_MAX:
        if(f->flags == 0) return app_link_shape(f, 0, 2, LNK_T_I16);
        return app_link_shape(f, UART_F_ACK | UART_F_CMD, 2, LNK_T_I16);
    case LNK_CMD_PROG_MODE:
        if(f->flags == 0) return app_link_shape(f, 0, 1, LNK_T_BITMAP8);
        return app_link_shape(f, UART_F_ACK | UART_F_CMD, 1, LNK_T_BITMAP8);
    case LNK_CMD_SENSOR_SRC:
        if(f->flags == 0) return app_link_shape(f, 0, 1, LNK_T_ENUM);
        return app_link_shape(f, UART_F_ACK | UART_F_CMD, 1, LNK_T_ENUM);
    case LNK_CMD_HYSTERESIS:
        if(f->flags == 0) return app_link_shape(f, 0, 1, LNK_T_I8);
        return app_link_shape(f, UART_F_ACK | UART_F_CMD, 1, LNK_T_I8);
    case LNK_CMD_KEY_LOCK:
        if(f->flags == 0) return app_link_shape(f, 0, 1, LNK_T_ENUM);
        return app_link_shape(f, UART_F_ACK | UART_F_CMD, 1, LNK_T_ENUM);
    case LNK_CMD_BRIGHT_DAY:
    case LNK_CMD_BRIGHT_NIGHT:
        if(f->flags == 0) return app_link_shape(f, 0, 1, LNK_T_U8);
        return app_link_shape(f, UART_F_ACK | UART_F_CMD, 1, LNK_T_U8);
    case LNK_CMD_SCHED_MON:
    case LNK_CMD_SCHED_TUE:
    case LNK_CMD_SCHED_WED:
    case LNK_CMD_SCHED_THU:
    case LNK_CMD_SCHED_FRI:
    case LNK_CMD_SCHED_SAT:
    case LNK_CMD_SCHED_SUN:
        if(f->flags == UART_F_ACK) return app_link_shape(f, UART_F_ACK, 0, 0);
        if(f->flags == 0)
            return app_link_shape(f, 0, LNK_SCHED_DAY_LEN, LNK_T_RAW);
        return app_link_shape(f, UART_F_ACK | UART_F_CMD,
                              LNK_SCHED_DAY_LEN, LNK_T_RAW);
    case LNK_CMD_FACTORY_RESET:
    case LNK_CMD_STATE_ALL:
    case LNK_CMD_CA51F2_BOOT:
        return app_link_shape(f, UART_F_ACK | UART_F_CMD, 0, 0);
    case LNK_CMD_OTA_START:
        return app_link_shape(f, UART_F_ACK | UART_F_CMD, 6, LNK_T_RAW);
    default:
        return LNK_ST_OK;
    }
}

static uint8_t app_link_handshake_ok(const uart_frame_t *f) {
    if(link_handshake_done) return 1;
    switch(f->cmd) {
    case LNK_CMD_INFO:
    case LNK_CMD_TEMP_LOCAL:
    case LNK_CMD_TEMP_OUTDOOR:
    case LNK_CMD_NET_STATUS:
    case LNK_CMD_CA51F2_BOOT:
    case LNK_CMD_OTA_START:
        return 1;
    case LNK_CMD_TIME:
    case LNK_CMD_SCHED_MON:
    case LNK_CMD_SCHED_TUE:
    case LNK_CMD_SCHED_WED:
    case LNK_CMD_SCHED_THU:
    case LNK_CMD_SCHED_FRI:
    case LNK_CMD_SCHED_SAT:
    case LNK_CMD_SCHED_SUN:
        return f->flags == UART_F_ACK;
    default:
        return 0;
    }
}

static void app_link_send_error(const uart_frame_t *f, uint8_t status) {
    if(!(f->flags & UART_F_RSP) && (f->flags & UART_F_ACK))
        App_Uart_SendFrame(f->seq, UART_F_RSP, status, f->cmd, 0, 0, 0);
}

void App_Link_OnFrame(const uart_frame_t *f) {
	uint8_t st = app_link_validate(f);

	if(st != LNK_ST_OK) {
		app_link_send_error(f, st);
		return;
	}
	if (App_Ota_IsActive() && f->cmd != LNK_CMD_OTA_START &&
	    f->cmd != LNK_CMD_INFO && f->cmd != LNK_CMD_CA51F2_BOOT) {
		/* OTA window: answer BUSY instead of dropping silently - the CA      */
		/* otherwise keeps burning its timer retries (3x100 ms + 30 s cycles) */
		/* against a wall. app_link_send_error stays mute for RSP frames and  */
		/* for reports (no reply is owed to them); INFO / 0x2A / 0xC0 keep    */
		/* flowing - they are how the OTA itself completes. During the raw    */
		/* transfer this code is unreachable (frame parser off).              */
		app_link_send_error(f, LNK_ST_BUSY);
		return;
	}
	if (f->flags & UART_F_RSP) {

		uint8_t i;

		/* Reply to our Info request: handshake complete, stop retrying.       */
		if (f->cmd == LNK_CMD_INFO && link_info_state == LNK_REQ_WAIT_RSP &&
		    f->seq == link_info_seq) {
			if (f->status != LNK_ST_OK || f->len < (LNK_INFO_VER_APP + 2)) {
				return;
			}
			link_info_state     = LNK_REQ_IDLE;
			link_handshake_done = 1;
			link_state_req_done = 0;
			link_ca51f2_ver = ((uint16_t)f->payload[LNK_INFO_VER_APP] << 8) |
			                   (uint16_t)f->payload[LNK_INFO_VER_APP + 1];
			if (!link_ca51f2_alive) {
				char model[LNK_INFO_MODEL_MAX + 1];
				uint8_t n = (f->len > LNK_INFO_MODEL) ?
				            (uint8_t)(f->len - LNK_INFO_MODEL) : 0;
				if (n > LNK_INFO_MODEL_MAX) n = LNK_INFO_MODEL_MAX;
				for (i = 0; i < n; i++)
					model[i] = (char)f->payload[LNK_INFO_MODEL + i];
				model[n] = 0;
				APP_DEBUG(DEBUG_ZB_CB_EN,
				          "CA51F2: ver 0x%x model %s\r\n",
				          link_ca51f2_ver, model);
			}
			link_ca51f2_alive = 1;
		}
		/* Reply to a reliable message: free its slot.                         */
		for (i = 0; i < LINK_TXQ_N; i++) {
			if (link_txq[i].state == LINK_TXQ_WAIT_RSP &&
			    link_txq[i].session == link_session &&
			    link_txq[i].seq == f->seq && link_txq[i].cmd == f->cmd) {
				link_pending_response(link_txq[i].cmd, link_txq[i].seq);
				link_txq[i].state = LINK_TXQ_FREE;
				/* Telemetry (WS sensor 0x04/0x06) is exempt: a dump cannot   */
				/* teach an old CA51F2 a new command, and every sensor report */
				/* would otherwise cost a full 20-frame STATE_ALL storm.      */
				if (f->status != LNK_ST_OK && f->cmd != LNK_CMD_TIME &&
				    f->cmd != LNK_CMD_TEMP_NET &&
				    f->cmd != LNK_CMD_HUMID_NET &&
				    !link_pending_any()) {
					link_state_request();
				}
				break;
			}
		}
		if (f->cmd == LNK_CMD_OTA_START && f->seq == link_ota_rsp_seq) {
			link_ota_rsp_status = f->status;
		}
		return;
	}
	if(!app_link_handshake_ok(f)) {
		app_link_send_error(f, LNK_ST_NOT_READY);
		return;
	}
	if((f->flags & UART_F_ACK) && app_link_dedup_replay(f)) return;
	if (!(f->flags & UART_F_ACK)) {
		link_dedup.valid = 0;
		(void)app_link_apply(f);
		return;
	}

	/* Request/command from the peer -> apply + reply.                      */
	switch (f->cmd) {
	case LNK_CMD_NET_STATUS:
		link_tx[0] = link_read_net_status();
		app_link_reply(f, LNK_ST_OK, LNK_T_ENUM, 1, link_tx);
		/* After the net status ask the CA51F2 for its whole configuration.   */
		/* Gated on OUR Info handshake: a dump started earlier would be       */
		/* rejected NOT_READY by our own gate and make the CA51F2 retry the   */
		/* same frame every 100 ms until the late Info aborts it (storm       */
		/* seen on the bench 2026-09-27). The dump is not lost: the Info RSP  */
		/* re-arms link_state_req_done and CA51F2's h_info answers with a     */
		/* fresh Net-status request, which re-enters this case with the gate  */
		/* open.                                                              */
		if (!link_state_req_done && link_handshake_done) {
			if(link_pending_any()) {
				link_state_req_done = LINK_STATE_REQ_WAIT_PENDING;
			}
			else {
				link_state_request();
			}
		}
		break;

	case LNK_CMD_CA51F2_BOOT:
		/* CA51F2 (re)booted: confirm, then run a fresh Info handshake now      */
		app_link_reply(f, LNK_ST_OK, 0, 0, 0);
		APP_DEBUG(DEBUG_ZB_CB_EN, "CA51F2 reboot -> Info\r\n");
		link_net_reported = 0xFF;          /* force a fresh Net-status push    */
		link_handshake_restart(0);
		break;

	case LNK_CMD_SYSTEM_MODE:
		/* values match ZCL SystemMode; the CA51F2 is authoritative for power */
		g_zcl_thermostatAttrs.systemMode = f->payload[0];
		APP_DEBUG(DEBUG_CMD_EN, "sysmode %d\r\n", f->payload[0]);
		app_link_reply(f, LNK_ST_OK, 0, 0, 0);
		break;

	case LNK_CMD_RUNNING:
		/* BITMAP16 big-endian (wire alignment 2026-09-26): payload[0] is    */
		/* the HIGH byte - the old 1-byte read stored 0 for Heat (Idle).     */
		if (f->len == 2)
			g_zcl_thermostatAttrs.runningState =
				(uint16_t)(((uint16_t)f->payload[0] << 8) | f->payload[1]);
		APP_DEBUG(DEBUG_CMD_EN, "running %d\r\n",
		          g_zcl_thermostatAttrs.runningState);
		app_link_reply(f, LNK_ST_OK, 0, 0, 0);
		break;

	case LNK_CMD_TIME:
		if (!zb_isDeviceJoinedNwk()) {
			app_link_reply(f, LNK_ST_NOT_READY, 0, 0, 0);
			break;
		}
		app_link_reply(f, LNK_ST_OK, 0, 0, 0);
		app_time_request();
		break;

	case LNK_CMD_FACTORY_RESET:
		/* confirm first, then leave the network + factory-new (device reset)  */
		app_link_reply(f, LNK_ST_OK, 0, 0, 0);
		APP_DEBUG(DEBUG_CMD_EN, "factory reset\r\n");
		TL_ZB_TIMER_SCHEDULE(delayedFactoryResetCb, NULL, TIMEOUT_500MS);
		break;

	default:
		/* state-dump frames: apply the value and confirm receipt             */
		app_link_reply(f, app_link_apply(f), 0, 0, 0);
		break;
	}
}
