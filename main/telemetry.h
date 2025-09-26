
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

void telemetry_init(QueueHandle_t*queue);