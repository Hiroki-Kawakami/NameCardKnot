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
    for (int i = 0; i < buf_size; i++) buf[i] = 0xff;
    bsp_display_draw_bitmap({0, 0, display_size.width, display_size.height}, buf);
    bsp_display_refresh({0, 0, display_size.width, display_size.height}, BSP_EPD_MODE_QUALITY);
    free(buf);

    while (true) {
        bsp_touch_point_t point;
        if (bsp_touch_read(&point, 1)) {
            ESP_LOGI(TAG, "touch: %d,%d", point.x, point.y);
            uint8_t pixel = 0;
            bsp_display_draw_bitmap({point.x, point.y, 1, 1}, &pixel);
            bsp_display_refresh({point.x, point.y, 1, 1}, BSP_EPD_MODE_FAST);
        }
    }
}

void app_entry() {
    bsp_init(nullptr);
    xTaskCreate(loop, "loop", 2048, nullptr, 3, nullptr);
}
