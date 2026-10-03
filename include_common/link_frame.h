#ifndef _LINK_FRAME_H_
#define _LINK_FRAME_H_
/**********************************************************************************/
/* Frame layer shared by CA51F2 and ZT3L/TLSR8258 (serial.md v2). Self-contained: */
/* pure #define constants, no types, no includes.                                 */
/*   66 BB | SEQ | FLAGS | STATUS | CMD | DLC | [TYPE] | PAYLOAD(<=64) | CRC      */
/*   TYPE is present only when DLC>0 (no payload -> CRC right after DLC).         */
/*   CRC-8/MAXIM: poly 0x31 (reflected 0x8C), init 0x00, xorout 0, over           */
/*   Header..Payload. Multi-byte numbers are big-endian.                          */
/**********************************************************************************/
#define UART_FRAME_H0        0x66                               /* header byte 0 (deliberately not Tuya 0x55) */
#define UART_FRAME_H1        0xBB                               /* header byte 1                              */
#define UART_FRAME_PAYLOAD   64                                 /* max Payload bytes per frame                */
#define UART_FRAME_MAX       (7 + 1 + UART_FRAME_PAYLOAD + 1)   /* 73 bytes                                   */

#define UART_F_RSP           0x01   /* reply (SEQ copied from the request)        */
#define UART_F_ACK           0x02   /* sender expects a reply                     */
#define UART_F_CMD           0x04   /* initiative is a command (action)           */

#define UART_CRC8_REFLECTED  0x8C   /* CRC-8/MAXIM reflected polynomial           */
/**********************************************************************************/
#endif
