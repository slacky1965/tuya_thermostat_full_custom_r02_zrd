#include "include/ca51f2sfr.h"
#include "include/ca51f2xsfr.h"
#include "include/flash.h"
#include "include/debug.h"
#include "include/intrins.h"

/* Erases one sector of the data area (128 bytes, sector address = n * 0x80).   */
uint8_t Data_Area_Sector_Erase(unsigned char SectorNumber) {
	unsigned int SectorAddress;
	uint16_t timeout = 0xFFFF;

	FSCMD = 0;
	SectorAddress = 0x80 * SectorNumber;
	LOCK = CMD_DATA_AREA_UNLOCK;                   // unlock flash data area
	PTSH = (unsigned char)(SectorAddress >> 8);    // target high address
	PTSL = (unsigned char)(SectorAddress);         // target low address
	FSCMD = CMD_DATA_AREA_ERASE_SECTOR;            // execute sector erase
	while((uint8_t)FSCMD && timeout) timeout--;
	LOCK = CMD_FLASH_LOCK;                         // lock flash again
	return (uint8_t)!FSCMD;
}

/* Writes one byte to the data area. Note: must be erased first.                */
void Data_Area_Write_Byte(unsigned int Address, unsigned char Data) {
	FSCMD  = 0;
	LOCK = CMD_DATA_AREA_UNLOCK;                   // unlock flash data area
	PTSH = (unsigned char)(Address >> 8);          // target high address
	PTSL = (unsigned char)Address;                 // target low address
	FSCMD = CMD_DATA_AREA_WIRTE;                   // execute write operation
	FSDAT = Data;                                  // load data byte
	FSCMD  = 0;
	LOCK = CMD_FLASH_LOCK;                         // lock flash again
}

/* Writes a byte block (e.g. a struct) into the data area.                      */
void Data_Area_Mass_Write(unsigned int Address, unsigned char xdata *pData, unsigned int Length) {
//    DEBUG(FLASH_EN, "Data_Area_Mass_Write() - Address: %d, Length: %d\r\n", Address, Length);
//    DEBUG_ARRAY(pData, Length);
    unsigned int i;
	FSCMD  = 0;
	LOCK = CMD_DATA_AREA_UNLOCK;                   // unlock flash data area
	PTSH = (unsigned char)(Address >> 8);          // target high address
	PTSL = (unsigned char)Address;                 // target low address
	FSCMD = CMD_DATA_AREA_WIRTE;                   // execute write operation
	for(i = 0; i < Length; i++) {
		FSDAT = *pData++;                          // load data byte
	}
	FSCMD  = 0;
	LOCK = CMD_FLASH_LOCK;                         // lock flash again

}

/* Reads a byte block (e.g. a struct) back from the data area.                  */
void Data_Area_Mass_Read(unsigned int Address, unsigned char xdata *pData, unsigned int Length) {
//#if UART_DEBUG
//    DEBUG(FLASH_EN, "Data_Area_Mass_Read() - Address: %d, Length: %d\r\n", Address, Length);
//    uint8_t *buf = (uint8_t*)pData;
//#endif
	unsigned int i;
	FSCMD  = 0;
	PTSH = (unsigned char)(Address >> 8);          // target high address
	PTSL = (unsigned char)Address;                 // target low address
	FSCMD = CMD_DATA_AREA_READ;                    // execute read operation
	for(i = 0; i < Length; i++) {
		*pData++ = FSDAT;
	}
	FSCMD  = 0;
	LOCK = CMD_FLASH_LOCK;                         // lock flash again
//#if UART_DEBUG
//	DEBUG_ARRAY(buf, Length);
//#endif
}
