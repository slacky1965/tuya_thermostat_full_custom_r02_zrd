#ifndef _TEMP_PIPELINE_H_
#define _TEMP_PIPELINE_H_
#include "include/stdint.h"
#include "include/sensor_ntc.h"

#define TEMP_PIPELINE_N 2
#define TEMP_PIPELINE_CHANGE_C100 6

typedef struct {
    int16_t live_c100;
    int16_t accepted_c100;
    uint8_t live_valid;
    uint8_t accepted_valid;
    uint8_t armed;
} temp_sample_t;

void Temp_Pipeline_Init(void);
void Temp_Pipeline_Update(uint8_t sensor, int16_t measured_c100, uint8_t valid, int8_t calibration_x10);
const temp_sample_t xdata *Temp_Pipeline_Get(uint8_t sensor);
#endif
