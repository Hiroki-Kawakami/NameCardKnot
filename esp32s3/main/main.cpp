#include <cassert>
#include "esp_log.h"

static const char *TAG = "main";

extern "C" void app_main() {
    ESP_LOGI(TAG, "HELLO!");
}
