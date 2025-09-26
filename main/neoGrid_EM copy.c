
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "driver/adc.h" 
#include "hal/adc_types.h"
#include "esp_adc/adc_oneshot.h"

void app_main(){
// In your task or main loop:

// In your task or main loop:
adc_oneshot_unit_handle_t adc1_handle;

// Initialize ADC unit
adc_oneshot_unit_init_cfg_t init_cfg1 = {
    .unit_id = ADC_UNIT_1,               // Use ADC1
    .ulp_mode = ADC_ULP_MODE_DISABLE,    // Disable ULP mode
};
ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg1, &adc1_handle));

// Configure channel
adc_oneshot_chan_cfg_t config = {
    .atten = ADC_ATTEN_DB_11,            // Up to ~3.3V on ADC1 (not 8.1V on ESP32!)
    .bitwidth = ADC_BITWIDTH_DEFAULT,    // Default resolution for this chip
};

adc_channel_t channel = ADC_CHANNEL_8;   // GPIO36 on ESP32
ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, channel, &config));
while (1) {
    // Get ADC raw value
    int raw_value;
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, channel, &raw_value));
    printf("RAW: %d\n",raw_value);
    // Optional: Get calibrated voltage
    // float voltage;
    // ESP_ERROR_CHECK(adc_oneshot_get_calibrated_voltage(adc1_handle, channel, &voltage));

    // Process your raw_value or voltage here

    vTaskDelay(pdMS_TO_TICKS(1)); // Delay for 1ms to achieve 1000Hz
}
}
