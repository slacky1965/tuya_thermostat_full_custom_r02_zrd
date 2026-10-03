#ifndef _CA51F2_OTA_H_
#define _CA51F2_OTA_H_
/*********************************************************************************/
/* CA51F2 OTA image header, shared by the CA51F2 firmware and the ZT3L/TLSR8258  */
/* updater. Self-contained: pure #define constants, no types, no includes.       */
/*                                                                               */
/* WHY THIS LAYOUT: the image travels through the Telink Zigbee-OTA receiver.    */
/* The Telink SDK validates a few FIXED offsets while receiving (zigbee/ota/     */
/* ota.c), so the CA51F2 header is shaped to keep those offsets valid while      */
/* still carrying the CA51F2 fields:                                             */
/*   off 6  == 0x5D 0x02  (OTA_MAGIC)   - hard check, ota.c:1091                 */
/*   off 8  == 0x4B       (start flag)  - hard check, ota.c:1073                 */
/*   last 4 bytes == CRC-32 of the whole image minus those 4 bytes, ota.c:1068   */
/* Everything else is ours. Multi-byte integers are little-endian.               */
/*                                                                               */
/* Layout (total CA51F2_OTA_HDR_SIZE bytes, then the raw CA51F2 bin):            */
/*   off 0  len 6  magic    6 ASCII bytes "CA51F2"                               */
/*   off 6  len 2  0x5D 0x02 (Telink OTA magic, required)                        */
/*   off 8  len 1  0x4B  (Telink start flag, required; SDK clears it to FF)      */
/*   off 9  len 2  version  u16 LE (BCD: hi byte = major, lo byte = minor)       */
/*   off 11 len 1  reserved                                                      */
/*   off 12 len 4  crc32    u32 LE, CRC-32 over exactly bin_size firmware bytes  */
/*   off 16 len 2  reserved                                                      */
/*   off 18 len 2  reserved (kept for the Telink make_ota.py fields)             */
/*   off 20 len 2  reserved                                                      */
/*   off 22 len 2  reserved                                                      */
/*   off 24 len 4  bin_size u32 LE, size of the firmware binary that follows     */
/*   off 28 len 4  reserved                                                      */
/*                                                                               */
/* CRC-32: reflected polynomial 0xEDB88320, init 0xFFFFFFFF, NO final xor.       */
/* This is exactly what TLSR xcrc32(buf, len, 0xFFFFFFFF) returns (utility.c     */
/* computes a bare reflected CRC and does not xor the result), so the Python     */
/* equivalent is binascii.crc32(data) ^ 0xFFFFFFFF. The image trailer CRC (the   */
/* last 4 bytes, required by the Telink receiver) is computed the same way over  */
/* the header + bin, and is produced by tools/make_ca51f2_ota.py, not here.      */
/*********************************************************************************/

/* Magic: 6 ASCII bytes "CA51F2" (not NUL terminated on disk).                  */
#define CA51F2_OTA_MAGIC_LEN   6
#define CA51F2_OTA_MAGIC       "CA51F2"
#define CA51F2_OTA_MAGIC_BYTES 'C', 'A', '5', '1', 'F', '2'

/* Field offsets (from the start of the header image) and field sizes.          */
#define CA51F2_OTA_OFF_MAGIC     0
#define CA51F2_OTA_LEN_MAGIC     6
#define CA51F2_OTA_OFF_TL_MAGIC  6    /* Telink OTA magic 0x5D02 (required)   */
#define CA51F2_OTA_OFF_TL_FLAG   8    /* Telink start flag 0x4B (required)    */
#define CA51F2_OTA_OFF_VERSION   9
#define CA51F2_OTA_LEN_VERSION   2
#define CA51F2_OTA_OFF_CRC32     12
#define CA51F2_OTA_LEN_CRC32     4
#define CA51F2_OTA_OFF_SIZE      24
#define CA51F2_OTA_LEN_SIZE      4
#define CA51F2_OTA_HDR_SIZE      32

/* Required Telink constants (see zigbee/ota/ota.c).                             */
#define CA51F2_OTA_TL_MAGIC0     0x5D
#define CA51F2_OTA_TL_MAGIC1     0x02
#define CA51F2_OTA_TL_FLAG       0x4B

/* The image is followed by the CRCs: the CA51F2 crc32 is INSIDE the header at   */
/* off 12 and covers only the bin; the Telink receiver CRC is the LAST 4 bytes   */
/* of the whole image (header + bin) and covers everything before it.            */

/* Version: same numeric value as config.h APP_VERSION (0x0100 = v1.00),         */
/* a BCD u16 where the high byte is the major and the low byte the minor.        */
#define CA51F2_OTA_VERSION_DEF APP_VERSION //0x0100
#define CA51F2_OTA_VMAJOR(v)   (((v) >> 8) & 0xFF)
#define CA51F2_OTA_VMINOR(v)   ((v) & 0xFF)
/*********************************************************************************/
#endif
