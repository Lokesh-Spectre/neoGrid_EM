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

static const char *TAG = "APP";

// #define BACKEND_ENDPOINT "https://wj3dltzp-5000.inc1.devtunnels.ms/api/saveLog"
#define BACKEND_ENDPOINT "http://192.168.0.108:8000"
// Queue handle for passing ADC stats
static QueueHandle_t* stats_queue;

void http_post_json(const char *url, const char *json_data) {
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,        
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .skip_cert_common_name_check = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Set POST data
    esp_http_client_set_post_field(client, json_data, strlen(json_data));
    esp_http_client_set_header(client, "Content-Type", "application/json");

    // Perform the POST
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d",
                 esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    // Cleanup
    esp_http_client_cleanup(client);
}
// Task that handles HTTP posting
static void http_post_task(void *arg) {
    adc_stats_t stats;
    char json_buf[128];

    while (1) {
        // Block until new data arrives
        if (xQueueReceive(*stats_queue, &stats, portMAX_DELAY)) {
            ESP_LOGI(TAG, "HTTP POST: min=%lf, max=%lf, avg=%lf, sd=%lf",
                     stats.min, stats.max, stats.avg, stats.sd);

            snprintf(json_buf, sizeof(json_buf),
                     "{\"nodeId\":\"\",\"level\":\"info\",\"Voltage_min\":%lf,\"Voltage_max\":%lf,\"Voltage_avg\":%lf,\"Voltage_sd\":%lf}",
                     stats.min, stats.max, stats.avg, stats.sd);

            // HTTP POST
            http_post_json(BACKEND_ENDPOINT, json_buf);
        }
    }
}

void telemetry_init(QueueHandle_t*queue) {
    stats_queue = queue;
    xTaskCreate(http_post_task, "http_post_task", 4096, NULL, 5, NULL);
}
