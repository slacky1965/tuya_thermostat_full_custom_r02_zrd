/********************************************************************************************************
 * @file    main.c
 *
 * @brief   This is the source file for main
 *
 * @author  Zigbee Group
 * @date    2021
 *
 * @par     Copyright (c) 2021, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *			All rights reserved.
 *
 *          Licensed under the Apache License, Version 2.0 (the "License");
 *          you may not use this file except in compliance with the License.
 *          You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 *          Unless required by applicable law or agreed to in writing, software
 *          distributed under the License is distributed on an "AS IS" BASIS,
 *          WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *          See the License for the specific language governing permissions and
 *          limitations under the License.
 *
 *******************************************************************************************************/

#include "zb_common.h"


#define ID_BOOTABLE             0x544C4E4b

#define FIRMWARE_FADDR          0x00008000
#define BOOTLOADER_FADDR        0x00000000
#define IMAGE_OTA_FADDR         0x00070000
#define IMAGE_OTA_FADDR_END     0x000E6000
#define ZIGBEE_MAC_FADDR        0x000FF000 // for 1 Mb
#define BUFF_SIZE               256

#ifndef BOOT_SIZE
#define BOOT_SIZE               22228
#endif


typedef struct __attribute__((packed)) {
    uint16_t reset;
    uint32_t file_version;
    uint16_t sig;
    uint32_t magic;
    uint32_t addr_bin_code;
    uint16_t irq;
    uint16_t manuf_code;
    uint16_t image_type;
    uint16_t dummy1;
    uint32_t bin_size;
    uint32_t dummy2;
} tl_header_t;

extern void user_init(bool isRetention);
extern void app_flash_write_status(flash_status_typedef_e type , unsigned short data);
extern void app_flash_read_page(unsigned long addr, unsigned long len, unsigned char *buf);
extern void app_flash_erase_sector(unsigned long addr);
extern void app_flash_write_page(unsigned long addr, unsigned long len, unsigned char *buf);

//uint8_t boot_yes = 255;

_attribute_ram_code_ void bootloader_copy(uint32_t bootloader_addr) {
    uint32_t size;
    uint32_t erase_size;
    uint8_t buff[BUFF_SIZE];
    app_flash_write_status(0, 0);
    app_flash_read_page(bootloader_addr, 32, (uint8_t*)&buff);
    tl_header_t *header = (tl_header_t*)buff;

    if (header->magic == ID_BOOTABLE && header->manuf_code == SLACKY_MANUF_CODE && header->bin_size == BOOT_SIZE) {

//        boot_yes = 1;
        size = header->bin_size;

        for (uint32_t i = 0; i < size; i += BUFF_SIZE) {
            if (i % FLASH_SECTOR_SIZE == 0) {
                app_flash_erase_sector(BOOTLOADER_FADDR + i);
            }
            app_flash_read_page(bootloader_addr+i, BUFF_SIZE, buff);
            app_flash_write_page(BOOTLOADER_FADDR+i, BUFF_SIZE, buff);
        }

//        /* erase last sector multiple of 0x1000 */
//        app_flash_erase_sector(bootloader_addr);

        app_flash_erase_sector(ZIGBEE_MAC_FADDR);

        erase_size = IMAGE_OTA_FADDR_END - IMAGE_OTA_FADDR;

        for (uint32_t i = 0; i < erase_size; i += FLASH_SECTOR_SIZE) {
            app_flash_erase_sector(IMAGE_OTA_FADDR + i);
        }

        while(1) reg_pwdn_ctrl = BIT(5);

    } //else boot_yes = 0;
}

int flash_main(void){
    startup_state_e state = drv_platform_init();

    u8 isRetention = (state == SYSTEM_DEEP_RETENTION) ? 1 : 0;

    os_init(isRetention);

#if 0
    extern void moduleTest_start(void);
    moduleTest_start();
#endif

    user_init(isRetention);

    drv_enable_irq();

#if (MODULE_WATCHDOG_ENABLE)
    drv_wd_setInterval(600);
    drv_wd_start();
#endif

#if VOLTAGE_DETECT_ENABLE
    u32 tick = clock_time();
#endif

    while(1){
#if VOLTAGE_DETECT_ENABLE
        if(clock_time_exceed(tick, 200 * 1000)){
            voltage_detect(0);
            tick = clock_time();
        }
#endif

        ev_main();

#if (MODULE_WATCHDOG_ENABLE)
        drv_wd_clear();
#endif

        tl_zbTaskProcedure();

#if (MODULE_WATCHDOG_ENABLE)
        drv_wd_clear();
#endif
    }

    return 0;
}

_attribute_ram_code_ int main(void) {

    uint32_t bootloader_faddr = IMAGE_OTA_FADDR + *(uint32_t*)(IMAGE_OTA_FADDR + 24);
    if (*(uint32_t*)(bootloader_faddr + 8) == ID_BOOTABLE) {
        bootloader_copy(bootloader_faddr);
    }

    return flash_main();
}
