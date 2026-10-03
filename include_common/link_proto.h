#ifndef _SRC_COMMON_LINK_PROTO_H_
#define _SRC_COMMON_LINK_PROTO_H_
/*********************************************************************************/
/* Shared CA51F2 <-> ZT3L <-> TLSR8258 protocol maps (identical for every build  */
/* that uses the wire protocol). This header is self-contained: pure #define     */
/* constants, no chip-specific types, no includes. The frame layer lives in the  */
/* peer (CA51F2: uart.c, ZT3L/TLSR8258: its own UART/BLE transport).             */
/*                                                                               */
/* Numbers on the wire are big-endian. Temperatures and setpoints are i16 in     */
/* hundredths of deg C (x100, matching ZCL); calibration and dead band are in    */
/* tenths (x10, ZCL 0.1 C); humidity is u16 x100 (0..10000). ID ranges:          */
/* 0x00-0x1F data/state, 0x20-0x26 weekly schedule (one frame per day),          */
/* 0x27-0x28 actions, 0xC0-0xDF OTA (reserved), 0xE0-0xFF spare.                 */
/*                                                                               */
/* Frame CRC (CRC-8/MAXIM) is validated by the parser: frames with a bad CRC     */
/* are dropped there and never reach the dispatcher, so handlers do not re-      */
/* check CRC.                                                                    */
/*********************************************************************************/
/* Protocol scheme version carried in the Info payload (LNK_CMD_INFO).        */
#define LNK_PROTO_VER            0x01

#define LNK_T_BOOL               0
#define LNK_T_ENUM               1
#define LNK_T_U8                 2
#define LNK_T_I8                 3
#define LNK_T_U16                4
#define LNK_T_I16                5
#define LNK_T_U32                6
#define LNK_T_I32                7
#define LNK_T_FLOAT              8
#define LNK_T_STRING             10
#define LNK_T_RAW                11
#define LNK_T_BITMAP8            12   /* 8-bit bitfield, 1 byte payload          */
#define LNK_T_BITMAP16           13   /* 16-bit bitfield, 2 bytes BE             */

/*** Entity IDs (Cmd ID field, serial.md map) ********************************/
#define LNK_CMD_TIME             0x00   /* RAW(8): utc(4 BE) + local(4 BE)       */
#define LNK_CMD_INFO             0x01   /* RAW(5..21): identity/versions         */
#define LNK_CMD_TEMP_LOCAL       0x02   /* i16 x100 internal (local) temperature */
#define LNK_CMD_TEMP_OUTDOOR     0x03   /* i16 x100 outdoor temperature          */
#define LNK_CMD_TEMP_NET         0x04   /* i16 x100 wireless sensor temperature  */
#define LNK_CMD_HUMID_LOCAL      0x05   /* u16 x100 local humidity 0..10000      */
#define LNK_CMD_HUMID_NET        0x06   /* u16 x100 wireless sensor humidity     */
#define LNK_CMD_SETPOINT_HEAT    0x07   /* i16 x100 occupied heating setpoint    */
#define LNK_CMD_SETPOINT_COOL    0x08   /* i16 x100 occupied cooling setpoint    */
#define LNK_CMD_RUNNING          0x09   /* bitmap16 (2 B BE), LNK_RUN_*          */
#define LNK_CMD_SYSTEM_MODE      0x0A   /* enum u8, LNK_SYSMODE_*                */
#define LNK_CMD_PROG_MODE        0x0B   /* bitmap8 (1 B), LNK_PROGMODE_*         */
#define LNK_CMD_SENSOR_SRC       0x0C   /* enum u8, LNK_SENSOR_*                 */
#define LNK_CMD_CAL_ACTIVE       0x0D   /* i8 x10 (ZCL 0.1 C)                    */
#define LNK_CMD_CAL_EXTERNAL     0x0E   /* i8 x10 (ZCL 0.1 C)                    */
#define LNK_CMD_HYSTERESIS       0x0F   /* i8 x10 (10..50 = 1.0..5.0 C)          */
#define LNK_CMD_LIMIT_MIN        0x10   /* i16 x100                              */
#define LNK_CMD_LIMIT_MAX        0x11   /* i16 x100                              */
#define LNK_CMD_FROST_PROTECT    0x12   /* i16 x100                              */
#define LNK_CMD_HEAT_PROTECT     0x13   /* i16 x100                              */
#define LNK_CMD_ECO_MODE         0x14   /* bool                                  */
#define LNK_CMD_ECO_HEAT         0x15   /* i16 x100                              */
#define LNK_CMD_ECO_COOL         0x16   /* i16 x100                              */
#define LNK_CMD_KEY_LOCK         0x17   /* 0 free, 1 ex-off, 2 full              */
#define LNK_CMD_SOUND            0x18   /* bool                                  */
#define LNK_CMD_OUTPUT_INV       0x19   /* bool (relay NO/NC)                    */
#define LNK_CMD_BRIGHT_DAY       0x1A   /* u8 0..8                               */
#define LNK_CMD_BRIGHT_NIGHT     0x1B   /* u8 0..8                               */
#define LNK_CMD_SCREEN_TIMEOUT   0x1C   /* enum u8                               */
#define LNK_CMD_LED_IND          0x1D   /* bool                                  */
#define LNK_CMD_HUMID_OFFSET     0x1E   /* i16 x100                              */
#define LNK_CMD_NET_STATUS       0x1F   /* enum u8, LNK_NET_*                    */
#define LNK_CMD_SCHED_MON        0x20   /* RAW(24) weekly schedule, Monday       */
#define LNK_CMD_SCHED_TUE        0x21   /* RAW(24), Tuesday                      */
#define LNK_CMD_SCHED_WED        0x22   /* RAW(24), Wednesday                    */
#define LNK_CMD_SCHED_THU        0x23   /* RAW(24), Thursday                     */
#define LNK_CMD_SCHED_FRI        0x24   /* RAW(24), Friday                       */
#define LNK_CMD_SCHED_SAT        0x25   /* RAW(24), Saturday                     */
#define LNK_CMD_SCHED_SUN        0x26   /* RAW(24), Sunday                       */
#define LNK_CMD_CA51F2_BOOT      0x2A   /* Cmd: display MCU (re)started, asks    */
                                        /*   the peer to run a fresh Info        */
#define LNK_CMD_FACTORY_RESET    0x27   /* action: restore defaults              */
#define LNK_CMD_STATE_ALL        0x28   /* action: report the whole state        */
#define LNK_CMD_OTA_START        0xC0   /* RAW(6): bin_size u32 BE + ver u16 BE  */

/*** Raw OTA block protocol (link framing bypassed after OTA_START) **********  */
/* Block: SYNC OFF_HI OFF_LO LEN DATA[LEN] CRC8; CRC-8/MAXIM (reflected 0x8C)   */
/* over OFF_HI..DATA. OFF is an absolute CA51F2 flash address, 128-aligned      */
/* except the last block.                                                       */
#define LNK_OTA_SYNC             0x5A
#define LNK_OTA_ACK              0x06
#define LNK_OTA_NAK              0x15
#define LNK_OTA_READY            0xA5     /* flasher -> TLSR: ready for blocks    */
#define LNK_OTA_DONE             0xA8     /* flasher -> TLSR: image written       */
#define LNK_OTA_RESTART          0xA6     /* TLSR -> flasher: reset offset to 0   */
#define LNK_OTA_ABORT            0xA7     /* reserved                             */
#define LNK_OTA_BLOCK_MAX        128      /* bytes per block (one flash sector)   */
#define LNK_OTA_PROGRAM_MAX      0x7E00UL /* CA51F2 program-area byte limit       */
#define LNK_OTA_RSP_LEN           3
#define LNK_OTA_FLASHER_BYTE_TIMEOUT_MS 100
#define LNK_OTA_TLSR_ACK_TIMEOUT_MS     1000
#if LNK_OTA_FLASHER_BYTE_TIMEOUT_MS >= LNK_OTA_TLSR_ACK_TIMEOUT_MS
#error OTA flasher byte timeout must be shorter than TLSR ACK timeout
#endif

/* Reserved: spare 0xE0-0xFF (serial.md).                                     */

/*** Status codes (ONLY in replies: 0x00 = OK, != 0 = error) ***************** */
/* High nibble = class: 0x1x frame/protocol, 0x2x value, 0x3x state, 0x4x      */
/* internal. Reports/commands (ACK=0) carry Status 0 and are never answered.   */
/* Check order: Flags -> DLC -> Type -> support -> range -> state.             */
#define LNK_ST_OK                0x00   /* done / data                          */

/* 0x1x frame / protocol                                                       */
#define LNK_ST_UNSUPPORT         0x10   /* unknown Cmd ID / entity not present  */
#define LNK_ST_BAD_LEN           0x11   /* DLC mismatch for the entity          */
#define LNK_ST_BAD_TYPE          0x12   /* Type mismatch for the entity         */
#define LNK_ST_BAD_FLAGS         0x13   /* invalid Flags combination            */
#define LNK_ST_BAD_VER           0x15   /* incompatible protocol version (Info) */

/* 0x2x value                                                                  */
#define LNK_ST_RANGE             0x20   /* value outside the valid range        */
#define LNK_ST_VALUE             0x21   /* invalid value (enum)                 */
#define LNK_ST_READONLY          0x22   /* entity is read-only                  */

/* 0x3x state                                                                  */
#define LNK_ST_NO_DATA           0x30   /* no value measured / received yet     */
#define LNK_ST_BUSY              0x31   /* busy (e.g. OTA in progress)          */
#define LNK_ST_NOT_READY         0x32   /* handshake/init not finished          */
#define LNK_ST_NOT_PERMITTED     0x33   /* forbidden (keypad lock)              */
#define LNK_ST_MODE              0x34   /* operation invalid in current mode    */

/* 0x4x internal                                                               */
#define LNK_ST_INTERNAL          0x40   /* internal error                       */
#define LNK_ST_FLASH             0x41   /* flash write failed                   */
#define LNK_ST_CORRUPT           0x42   /* stored settings image corrupt        */

/*** Running state bitmap (LNK_CMD_RUNNING, ZCL RunningState) *****************/
#define LNK_RUN_HEAT             0x01
#define LNK_RUN_COOL             0x02
#define LNK_RUN_FAN              0x04

/*** System mode (LNK_CMD_SYSTEM_MODE, ZCL HVAC SystemMode) *******************/
#define LNK_SYSMODE_OFF          0x00
#define LNK_SYSMODE_AUTO         0x01
#define LNK_SYSMODE_COOL         0x03
#define LNK_SYSMODE_HEAT         0x04
#define LNK_SYSMODE_EHEAT        0x05
#define LNK_SYSMODE_FANONLY      0x07
#define LNK_SYSMODE_DRY          0x08

/*** Programming operation mode (LNK_CMD_PROG_MODE, ZCL bit0 + bit2) ***********/
#define LNK_PROGMODE_MANUAL      0x00
#define LNK_PROGMODE_PROG        0x01
#define LNK_PROGMODE_TEMP        0x02

/*** Sensor source (LNK_CMD_SENSOR_SRC) *************************************** */
/* Order matches temp_src_t (thermostat.h) and the CA51F2 menu IN -> OU -> AL.  */
#define LNK_SENSOR_IN            0x00
#define LNK_SENSOR_OU            0x01
#define LNK_SENSOR_AL            0x02

/*** Network status (LNK_CMD_NET_STATUS) ***************************************/
#define LNK_NET_FREE             0x00
#define LNK_NET_CONNECTED        0x01
#define LNK_NET_ERROR            0x02

/*** Weekly schedule (LNK_CMD_SCHED_*, RAW, 24 B = 6 entries x 4 B BE) *********/
#define LNK_SCHED_N              6                               /* transitions per day                */
#define LNK_SCHED_ENTRY          4                               /* u16 transTime + i16 heatTemp       */
#define LNK_SCHED_DAY_LEN       (LNK_SCHED_N * LNK_SCHED_ENTRY)  /* 24 B                               */
#define LNK_SCHED_TIME_EMPTY     0xFFFF                          /* transTime: entry not used          */
#define LNK_SCHED_DAY_MAX_MIN    1439                            /* max minutes since midnight         */

/*** Info payload layout (LNK_CMD_INFO, RAW) ***********************************/
#define LNK_INFO_VER_PROTO       0                                     /* protocol scheme version u8         */
#define LNK_INFO_VER_APP         1                                     /* firmware version, u16 big-endian   */
#define LNK_INFO_CHIPID          3                                     /* hardware chip ID, u16 big-endian   */
#define LNK_INFO_MODEL           5                                     /* model string, no terminator        */
#define LNK_INFO_MODEL_MAX       16                                    /* max model string bytes             */
#define LNK_INFO_MAX_LEN        (LNK_INFO_MODEL + LNK_INFO_MODEL_MAX)  /* 21                                 */
/*************************************************************************************************************/
#endif