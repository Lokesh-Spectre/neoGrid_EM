#pragma once
#include "esp_err.h"

typedef struct {
    double min;
    double max;
    double avg;
    double sd;
} adc_stats_tmp_t;

typedef struct {
    double V_min;
    double V_max;
    double V_avg;
    double V_sd;
    double C_min;
    double C_max;
    double C_avg;
    double C_sd;
} adc_stats_t;

typedef void (*data_cb_t)(adc_stats_t data);

void init_volt_monitor(data_cb_t telemetry_data_cb);
// esp_err_t adc_sampler_get_stats(adc_stats_t *stats);
