#include "NameCardKnot.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "NameCardKnot";

static void loop(void *args) {
    while (true) {
        ESP_LOGI(TAG, "HELLO!");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_entry() {
    xTaskCreate(loop, "loop", 2048, nullptr, 3, nullptr);
}
