#ifndef _FLASH_H_
#define _FLASH_H_

#include "include/stdint.h"

/* On-chip data-flash access (byte oriented, XRAM registers 0xFC01..0xFC06).    */
/* Data area: last 512 bytes of flash = 4 sectors x 128 bytes, erased only by   */
/* sector, addressed by logical data-area offsets. For 16/32-bit values store/  */
/* load the raw byte image (Keil C51 is little-endian, no struct padding).      */

#define FLASH_PAGE  128

enum {
	CMD_DATA_AREA_READ        = 1,   /* read one data-area byte  */
	CMD_DATA_AREA_WIRTE       = 2,   /* write one data-area byte */
	CMD_DATA_AREA_ERASE_SECTOR = 3,  /* erase one data sector    */
	CMD_CODE_AREA_READ        = 5,   /* read one code-area byte  */
	CMD_CODE_AREA_WRITE       = 6,   /* write one code-area byte */
	CMD_CODE_AREA_ERASE_SECTOR = 7,  /* erase one code sector    */
};

enum {
	CMD_CODE_AREA_UNLOCK = 0x29,     /* code-area access unlock  */
	CMD_DATA_AREA_UNLOCK = 0x2A,     /* data-area access unlock  */
	CMD_FLASH_LOCK       = 0xAA,     /* access locks again       */
};

#define IFEN        (1 << 7)          /* FSCMD interrupt flag bit */

uint8_t Data_Area_Sector_Erase(unsigned char SectorNumber);
void Data_Area_Write_Byte(unsigned int Address, unsigned char Data);
void Data_Area_Mass_Write(unsigned int Address, unsigned char xdata *pData, unsigned int Length);
void Data_Area_Mass_Read(unsigned int Address, unsigned char xdata *pData, unsigned int Length);

#endif
