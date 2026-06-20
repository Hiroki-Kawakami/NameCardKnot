#include "NameCardKnot.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "NameCardKnot";

void app_entry() {
    while (true) {
        ESP_LOGI(TAG, "HELLO!");
        vTaskDelay(pdTICKS_TO_MS(1000));
    }
}
