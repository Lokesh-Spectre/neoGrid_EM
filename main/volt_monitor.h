#pragma once
#include "esp_err.h"

typedef struct {
    double min;
    double max;
    double avg;
    double sd;
} adc_stats_t;

typedef void (*data_cb_t)(adc_stats_t data);

void init_volt_monitor(data_cb_t volt_data_cb);
// esp_err_t adc_sampler_get_stats(adc_stats_t *stats);
