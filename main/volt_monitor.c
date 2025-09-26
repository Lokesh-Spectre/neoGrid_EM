#include <string.h>
#include <stdio.h>
#include <math.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc_cal.h"
#include "volt_monitor.h"
#define EXAMPLE_ADC_UNIT            ADC_UNIT_1
#define EXAMPLE_ADC_CHANNEL         ADC_CHANNEL_6
#define EXAMPLE_ADC_ATTEN           ADC_ATTEN_DB_11
#define EXAMPLE_ADC_BIT_WIDTH       SOC_ADC_DIGI_MAX_BITWIDTH
#define EXAMPLE_ADC_CONV_MODE       ADC_CONV_SINGLE_UNIT_1
#define EXAMPLE_ADC_OUTPUT_TYPE     ADC_DIGI_OUTPUT_FORMAT_TYPE2
#define EXAMPLE_READ_LEN            256

#define EXAMPLE_ADC_GET_CHANNEL(p_data)     ((p_data)->type2.channel)
#define EXAMPLE_ADC_GET_DATA(p_data)        ((p_data)->type2.data)

static const char *TAG = "ADC_SINGLE";

static data_cb_t _volt_data_cb;
// ADC calibration handle
static esp_adc_cal_characteristics_t *adc_chars;

// ADC continuous driver handle
static adc_continuous_handle_t adc_handle = NULL;

// Task handle for ADC task
static TaskHandle_t adc_task_handle = NULL;

// Conversion done callback
static bool IRAM_ATTR s_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)
{
    BaseType_t mustYield = pdFALSE;
    vTaskNotifyGiveFromISR(adc_task_handle, &mustYield);
    return (mustYield == pdTRUE);
}

// ADC continuous init function
static void continuous_adc_init(void)
{
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 1024,
        .conv_frame_size = EXAMPLE_READ_LEN,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &adc_handle));

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = 1000,  // 1 kHz sampling
        .conv_mode = EXAMPLE_ADC_CONV_MODE,
        .format = EXAMPLE_ADC_OUTPUT_TYPE,
        .pattern_num = 1,
    };

    adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {0};
    adc_pattern[0].atten = EXAMPLE_ADC_ATTEN;
    adc_pattern[0].channel = EXAMPLE_ADC_CHANNEL & 0x7;
    adc_pattern[0].unit = EXAMPLE_ADC_UNIT;
    adc_pattern[0].bit_width = EXAMPLE_ADC_BIT_WIDTH;

    dig_cfg.adc_pattern = adc_pattern;
    ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &dig_cfg));

    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = s_conv_done_cb,
    };
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(adc_handle, &cbs, NULL));
    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));
}

// ADC task: reads values and calculates RMS, min, max, mean every second
// ADC task: continuous RMS + per-second stats on RMS values
static void adc_task(void *arg)
{
    uint8_t result[EXAMPLE_READ_LEN] = {0};
    memset(result, 0xcc, EXAMPLE_READ_LEN);

    double sum_sq = 0;
    uint32_t sample_count = 0;

    // Buffer for storing continuous RMS values within 1s
    #define MAX_RMS_SAMPLES 200   // ~200 batches per sec at 1kHz, adjust as needed
    double rms_buffer[MAX_RMS_SAMPLES];
    uint32_t rms_count = 0;

    TickType_t last_tick = xTaskGetTickCount();

    while (1) {
        // Wait for ADC conversion done notification
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint32_t ret_num = 0;
        while (1) {
            esp_err_t ret = adc_continuous_read(adc_handle, result, EXAMPLE_READ_LEN, &ret_num, 0);
            if (ret == ESP_OK) {
                for (int i = 0; i < ret_num; i += SOC_ADC_DIGI_RESULT_BYTES) {
                    adc_digi_output_data_t *p = (adc_digi_output_data_t*)&result[i];
                    uint32_t chan_num = EXAMPLE_ADC_GET_CHANNEL(p);
                    uint32_t raw = EXAMPLE_ADC_GET_DATA(p);

                    if (chan_num < SOC_ADC_CHANNEL_NUM(EXAMPLE_ADC_UNIT)) {
                        uint32_t voltage = esp_adc_cal_raw_to_voltage(raw, adc_chars);
                        sum_sq += ((double)voltage * voltage);
                        sample_count++;
                    }
                }
            } else if (ret == ESP_ERR_TIMEOUT) {
                break;
            }
        }

        // Compute instantaneous RMS for this batch
        if (sample_count > 0) {
            double rms = sqrt(sum_sq / sample_count);
            // ESP_LOGI(TAG, "Continuous RMS: %.2f mV", rms);

            // Save in buffer
            if (rms_count < MAX_RMS_SAMPLES) {
                rms_buffer[rms_count++] = rms;
            }

            // Reset accumulators for next batch
            sum_sq = 0;
            sample_count = 0;
        }

        // Every 1s, compute stats
        TickType_t now = xTaskGetTickCount();
        if ((now - last_tick) >= pdMS_TO_TICKS(1000)) {
            if (rms_count > 0) {
                double min_rms = rms_buffer[0];
                double max_rms = rms_buffer[0];
                double sum_rms = 0;

                for (uint32_t i = 0; i < rms_count; i++) {
                    if (rms_buffer[i] < min_rms) min_rms = rms_buffer[i];
                    if (rms_buffer[i] > max_rms) max_rms = rms_buffer[i];
                    sum_rms += rms_buffer[i];
                }

                double mean_rms = sum_rms / rms_count;

                // Compute standard deviation
                double var = 0;
                for (uint32_t i = 0; i < rms_count; i++) {
                    double diff = rms_buffer[i] - mean_rms;
                    var += diff * diff;
                }
                double stddev = sqrt(var / rms_count);
                adc_stats_t data= {
                    .min = min_rms,
                    .max = max_rms,
                    .avg = mean_rms,
                    .sd = stddev
                };
                _volt_data_cb(data);
                // ESP_LOGI(TAG,
                //          "Stats (1s) -> Min RMS: %.2f mV, Max RMS: %.2f mV, Mean RMS: %.2f mV, StdDev: %.2f mV",
                //          min_rms, max_rms, mean_rms, stddev);
            }

            // Reset for next interval
            rms_count = 0;
            last_tick = now;
        }
    }
}


void init_volt_monitor(data_cb_t volt_data_cb){
    _volt_data_cb = volt_data_cb;

    // ADC calibration init
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(EXAMPLE_ADC_UNIT, EXAMPLE_ADC_ATTEN, EXAMPLE_ADC_BIT_WIDTH, 0, adc_chars);

    // Initialize ADC
    continuous_adc_init();

    // Create FreeRTOS task for ADC processing
    xTaskCreate(adc_task, "adc_task", 4096, NULL, 10, &adc_task_handle);

}
