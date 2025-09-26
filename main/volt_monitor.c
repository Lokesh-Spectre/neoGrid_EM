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
#define EXAMPLE_ADC_CHANNEL_1       ADC_CHANNEL_6  // GPIO34
#define EXAMPLE_ADC_CHANNEL_2       ADC_CHANNEL_5  // GPIO36
#define EXAMPLE_ADC_ATTEN           ADC_ATTEN_DB_11
#define EXAMPLE_ADC_BIT_WIDTH       SOC_ADC_DIGI_MAX_BITWIDTH
#define EXAMPLE_ADC_CONV_MODE       ADC_CONV_SINGLE_UNIT_1
#define EXAMPLE_ADC_OUTPUT_TYPE     ADC_DIGI_OUTPUT_FORMAT_TYPE2
#define EXAMPLE_READ_LEN            256

#define EXAMPLE_ADC_GET_CHANNEL(p_data) ((p_data)->type2.channel)
#define EXAMPLE_ADC_GET_DATA(p_data)    ((p_data)->type2.data)

static const char *TAG = "ADC_DUAL";

static data_cb_t _telemetry_data_cb;

static esp_adc_cal_characteristics_t *adc_chars;
static adc_continuous_handle_t adc_handle = NULL;
static TaskHandle_t adc_task_handle = NULL;

static bool IRAM_ATTR s_conv_done_cb(adc_continuous_handle_t handle,
                                     const adc_continuous_evt_data_t *edata,
                                     void *user_data)
{
    BaseType_t mustYield = pdFALSE;
    vTaskNotifyGiveFromISR(adc_task_handle, &mustYield);
    return (mustYield == pdTRUE);
}

static void continuous_adc_init(void)
{
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 2048,
        .conv_frame_size = EXAMPLE_READ_LEN,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &adc_handle));

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = 1000,
        .conv_mode = EXAMPLE_ADC_CONV_MODE,
        .format = EXAMPLE_ADC_OUTPUT_TYPE,
        .pattern_num = 2,  // two channels
    };

    adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {0};

    // Channel 1
    adc_pattern[0].atten = EXAMPLE_ADC_ATTEN;
    adc_pattern[0].channel = EXAMPLE_ADC_CHANNEL_1 & 0x7;
    adc_pattern[0].unit = EXAMPLE_ADC_UNIT;
    adc_pattern[0].bit_width = EXAMPLE_ADC_BIT_WIDTH;

    // Channel 2
    adc_pattern[1].atten = EXAMPLE_ADC_ATTEN;
    adc_pattern[1].channel = EXAMPLE_ADC_CHANNEL_2 & 0x7;
    adc_pattern[1].unit = EXAMPLE_ADC_UNIT;
    adc_pattern[1].bit_width = EXAMPLE_ADC_BIT_WIDTH;

    dig_cfg.adc_pattern = adc_pattern;

    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = s_conv_done_cb,
    };

    ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &dig_cfg));
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(adc_handle, &cbs, NULL));
    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));
}

#define MAX_RMS_SAMPLES 200
// Store AC RMS values per window
static double rms_buffer_ch1[MAX_RMS_SAMPLES];
static double rms_buffer_ch2[MAX_RMS_SAMPLES];

static void adc_task(void *arg)
{
    uint8_t result[EXAMPLE_READ_LEN] = {0};

    uint32_t rms_count_ch1 = 0;
    uint32_t rms_count_ch2 = 0;

    // Buffers for raw voltages per window
    static double sample_buf_ch1[EXAMPLE_READ_LEN];
    static double sample_buf_ch2[EXAMPLE_READ_LEN];
    uint32_t sample_count_ch1 = 0, sample_count_ch2 = 0;

    TickType_t last_tick = xTaskGetTickCount();

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint32_t ret_num = 0;
        while (1) {
            esp_err_t ret = adc_continuous_read(adc_handle, result, EXAMPLE_READ_LEN, &ret_num, 0);
            if (ret == ESP_OK && ret_num > 0) {
                for (uint32_t i = 0; i + SOC_ADC_DIGI_RESULT_BYTES <= ret_num; i += SOC_ADC_DIGI_RESULT_BYTES) {
                    adc_digi_output_data_t *p = (adc_digi_output_data_t*)&result[i];
                    uint32_t chan = EXAMPLE_ADC_GET_CHANNEL(p);
                    uint32_t raw = EXAMPLE_ADC_GET_DATA(p);

                    raw &= 0x0FFF; // 12-bit mask
                    uint32_t voltage = esp_adc_cal_raw_to_voltage(raw, adc_chars);

                    if (chan == (EXAMPLE_ADC_CHANNEL_1 & 0x7)) {
                        if (sample_count_ch1 < EXAMPLE_READ_LEN) {
                            sample_buf_ch1[sample_count_ch1++] = (double)voltage;
                        }
                    } else if (chan == (EXAMPLE_ADC_CHANNEL_2 & 0x7)) {
                        if (sample_count_ch2 < EXAMPLE_READ_LEN) {
                            sample_buf_ch2[sample_count_ch2++] = (double)voltage;
                        }
                    }
                }
            } else if (ret == ESP_ERR_TIMEOUT || ret == ESP_OK) {
                break;
            } else {
                ESP_LOGW(TAG, "adc_continuous_read err: 0x%x", ret);
                break;
            }
        }

        // --- Compute AC RMS per notification window ---
        if (sample_count_ch1 > 0) {
            double sum = 0;
            for (uint32_t i = 0; i < sample_count_ch1; i++) sum += sample_buf_ch1[i];
            double mean = sum / sample_count_ch1;

            double ac_sum_sq = 0;
            for (uint32_t i = 0; i < sample_count_ch1; i++) {
                double ac = sample_buf_ch1[i] - mean;
                ac_sum_sq += ac * ac;
            }
            double ac_rms = sqrt(ac_sum_sq / sample_count_ch1);

            if (rms_count_ch1 < MAX_RMS_SAMPLES) rms_buffer_ch1[rms_count_ch1++] = ac_rms;
            sample_count_ch1 = 0;
        }

        if (sample_count_ch2 > 0) {
            double sum = 0;
            for (uint32_t i = 0; i < sample_count_ch2; i++) sum += sample_buf_ch2[i];
            double mean = sum / sample_count_ch2;

            double ac_sum_sq = 0;
            for (uint32_t i = 0; i < sample_count_ch2; i++) {
                double ac = sample_buf_ch2[i] - mean;
                ac_sum_sq += ac * ac;
            }
            double ac_rms = sqrt(ac_sum_sq / sample_count_ch2);

            if (rms_count_ch2 < MAX_RMS_SAMPLES) rms_buffer_ch2[rms_count_ch2++] = ac_rms;
            sample_count_ch2 = 0;
        }

        // Every 1s, compute stats per channel
        TickType_t now = xTaskGetTickCount();
        if ((now - last_tick) >= pdMS_TO_TICKS(1000)) {
            if (rms_count_ch1 > 0 || rms_count_ch2 > 0) {
                adc_stats_tmp_t stats_ch1 = {0}, stats_ch2 = {0};

                if (rms_count_ch1 > 0) {
                    double sum = 0, var = 0;
                    stats_ch1.min = stats_ch1.max = rms_buffer_ch1[0];
                    for (uint32_t i = 0; i < rms_count_ch1; i++) {
                        double v = rms_buffer_ch1[i];
                        if (v < stats_ch1.min) stats_ch1.min = v;
                        if (v > stats_ch1.max) stats_ch1.max = v;
                        sum += v;
                    }
                    stats_ch1.avg = sum / rms_count_ch1;
                    for (uint32_t i = 0; i < rms_count_ch1; i++) {
                        double d = rms_buffer_ch1[i] - stats_ch1.avg;
                        var += d * d;
                    }
                    stats_ch1.sd = sqrt(var / rms_count_ch1);
                }

                if (rms_count_ch2 > 0) {
                    double sum = 0, var = 0;
                    stats_ch2.min = stats_ch2.max = rms_buffer_ch2[0];
                    for (uint32_t i = 0; i < rms_count_ch2; i++) {
                        double v = rms_buffer_ch2[i];
                        if (v < stats_ch2.min) stats_ch2.min = v;
                        if (v > stats_ch2.max) stats_ch2.max = v;
                        sum += v;
                    }
                    stats_ch2.avg = sum / rms_count_ch2;
                    for (uint32_t i = 0; i < rms_count_ch2; i++) {
                        double d = rms_buffer_ch2[i] - stats_ch2.avg;
                        var += d * d;
                    }
                    stats_ch2.sd = sqrt(var / rms_count_ch2);
                }

                adc_stats_t data = {
                    .V_min = (rms_count_ch1>0)?stats_ch1.min:0,
                    .V_max = (rms_count_ch1>0)?stats_ch1.max:0,
                    .V_avg = (rms_count_ch1>0)?stats_ch1.avg:0,
                    .V_sd  = (rms_count_ch1>0)?stats_ch1.sd:0,
                    .C_min = (rms_count_ch2>0)?stats_ch2.min:0,
                    .C_max = (rms_count_ch2>0)?stats_ch2.max:0,
                    .C_avg = (rms_count_ch2>0)?stats_ch2.avg:0,
                    .C_sd  = (rms_count_ch2>0)?stats_ch2.sd:0
                };

                if (_telemetry_data_cb) {
                    _telemetry_data_cb(data);
                } else {
                    ESP_LOGW(TAG, "telemetry callback is NULL");
                }
            }

            rms_count_ch1 = 0;
            rms_count_ch2 = 0;
            last_tick = now;
        }

        taskYIELD();
    }
}

void init_volt_monitor(data_cb_t volt_data_cb){
    _telemetry_data_cb = volt_data_cb;
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(EXAMPLE_ADC_UNIT, EXAMPLE_ADC_ATTEN, EXAMPLE_ADC_BIT_WIDTH, 0, adc_chars);

    continuous_adc_init();
    xTaskCreate(adc_task, "adc_task", 4096, NULL, 10, &adc_task_handle);
}
