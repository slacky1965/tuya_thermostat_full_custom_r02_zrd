#include "zcl_include.h"
#include "zcl_app_thermostat.h"

zcl_scheduleData_t g_zcl_scheduleData = {
    .schedule_mon = {
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
    },
    .schedule_tue = {
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
    },
    .schedule_wed = {
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
    },
    .schedule_thu = {
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
    },
    .schedule_fri = {
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
    },
    .schedule_sat = {
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
    },
    .schedule_sun = {
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
        {0x0000, 1500},
    },
};

uint8_t zcl_convert_temperature_fromCtoF(int16_t tempC) {

    return (((tempC*18 + 250)/500 + 64)/2);
}

int16_t zcl_convert_temperature_fromFtoC(int16_t tempF) {

    return (((tempF*2 - 64)*5000 + 90)/180);
}

