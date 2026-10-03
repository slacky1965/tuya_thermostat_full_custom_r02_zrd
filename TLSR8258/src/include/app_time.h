#ifndef TLSR8258_SRC_INCLUDE_APP_TIME_H_
#define TLSR8258_SRC_INCLUDE_APP_TIME_H_

#define UNIX_TIME_CONST 946684800

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} ftime_t;

void app_time_request(void);
void app_time_apply(uint32_t utc, uint32_t local);

#endif /* TLSR8258_SRC_INCLUDE_APP_TIME_H_ */
