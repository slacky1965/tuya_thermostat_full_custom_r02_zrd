#include "include/temp_pipeline.h"

static temp_sample_t xdata samples[TEMP_PIPELINE_N];

void Temp_Pipeline_Init(void) {
    uint8_t i;

    for (i = 0; i < TEMP_PIPELINE_N; i++) {
        samples[i].live_c100 = 0;
        samples[i].accepted_c100 = 0;
        samples[i].live_valid = 0;
        samples[i].accepted_valid = 0;
        samples[i].armed = 0;
    }
}

void Temp_Pipeline_Update(uint8_t sensor, int16_t measured_c100,
                          uint8_t valid, int8_t calibration_x10) {
    temp_sample_t xdata *sample;
    int16_t value;
    int16_t delta;

    if (sensor >= TEMP_PIPELINE_N) {
        return;
    }

    sample = &samples[sensor];
    if (!valid) {
        sample->live_c100 = 0;
        sample->live_valid = 0;
        sample->accepted_valid = 0;
        sample->armed = 0;
        return;
    }

    value = (int16_t)(measured_c100 +
                      (int16_t)calibration_x10 * 8 +
                      (int16_t)calibration_x10 * 2);
    sample->live_c100 = value;
    sample->live_valid = 1;

    if (!sample->armed) {
        sample->accepted_c100 = value;
        sample->accepted_valid = 1;
        sample->armed = 1;
        return;
    }

    if ((value < 0) != (sample->accepted_c100 < 0)) {
        sample->accepted_c100 = value;
        sample->accepted_valid = 1;
        sample->armed = 1;
        return;
    }

    delta = (value > sample->accepted_c100)
          ? (int16_t)(value - sample->accepted_c100)
          : (int16_t)(sample->accepted_c100 - value);
    if (delta >= TEMP_PIPELINE_CHANGE_C100) {
        sample->accepted_c100 = value;
        sample->accepted_valid = 1;
        sample->armed = 1;
    }
}

const temp_sample_t xdata *Temp_Pipeline_Get(uint8_t sensor) {
    if (sensor >= TEMP_PIPELINE_N) {
        return 0;
    }
    return &samples[sensor];
}
