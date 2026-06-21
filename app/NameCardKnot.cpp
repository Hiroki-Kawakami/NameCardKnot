#include "NameCardKnot.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bsp.h"

static const char *TAG = "NameCardKnot";

static void loop(void *args) {
    auto display_size = bsp_display_get_size();
    size_t buf_size = display_size.width * display_size.height * bsp_pixel_format_bytes(bsp_display_get_pixel_format());
    uint8_t *buf = (uint8_t*)malloc(buf_size);
    for (int y = 0; y < display_size.height; y++) {
        for (int x = 0; x < display_size.width; x++) {
            int band = x * 16 / display_size.width; // 0..15
            buf[y * display_size.width + x] = (uint8_t)(band * 0xFF / 15);
            // buf[y * display_size.width + x] = band % 2 ? 0xff : 0;
        }
    }
    bsp_display_draw_bitmap({0, 0, display_size.width, display_size.height}, buf);
    bsp_display_refresh({0, 0, display_size.width, display_size.height}, BSP_EPD_MODE_QUALITY);
    free(buf);

    while (true) {
        bsp_touch_point_t point;
        if (bsp_touch_read(&point, 1)) {
            ESP_LOGI(TAG, "touch: %d,%d", point.x, point.y);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_entry() {
    bsp_init(nullptr);
    xTaskCreate(loop, "loop", 4096, nullptr, 3, nullptr);
}
