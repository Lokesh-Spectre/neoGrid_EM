#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "volt_monitor.h"
#include "esp_http_client.h"
#include "nvs_flash.h"
#include "wifi_man.h"
#include "telemetry.h"
#include "driver/gpio.h"

static const char *TAG = "APP";
#define RELAY_PIN 8
void relay_init(){
        // Configure GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RELAY_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
}
void relay_state(bool state){
    if (state){
        gpio_set_level(RELAY_PIN, 1);
    }else{
        gpio_set_level(RELAY_PIN, 0);
    }
}
// Queue handle for passing ADC stats
static QueueHandle_t stats_queue;

// Callback: only pushes data to queue
void volt_data_cb(adc_stats_t data) {
    if (stats_queue) {
        // Non-blocking, if queue full we can decide to overwrite or drop
        if (xQueueSend(stats_queue, &data, 0) != pdPASS) {
            ESP_LOGW(TAG, "Stats queue full, dropping data");
        }
    }
}

void app_main(void) {
        // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    wifi_init();
    wait_for_wifi_connect();
    
    // Create queue for ADC stats
    stats_queue = xQueueCreate(10, sizeof(adc_stats_t));
    if (stats_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create stats queue");
        return;
    }

    telemetry_init(&stats_queue);
    // Init voltage monitor with callback
    init_volt_monitor(volt_data_cb);
    ESP_LOGI(TAG, "INIT DONE");
}
