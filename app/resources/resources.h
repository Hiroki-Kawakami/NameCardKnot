#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "lvgl.h"
#include "lucide_font.h"

struct Resources {
    struct {
        const lv_image_dsc_t *battery_32px;
        const lv_image_dsc_t *battery_low_32px;
        const lv_image_dsc_t *battery_medium_32px;
        const lv_image_dsc_t *battery_full_32px;
        const lv_image_dsc_t *card_sd_80px;
        const lv_image_dsc_t *cog_80px;
        const lv_image_dsc_t *images_80px;
        const lv_image_dsc_t *square_arrow_right_enter_80px;
    } icon;
    struct {
        const lv_font_t *lucide_40;
        const lv_font_t *noto_sans_jp_24;
        const lv_font_t *noto_sans_jp_32;
        const lv_font_t *noto_sans_jp_48;
    } font;
};
extern const struct Resources R;

#ifdef __cplusplus
} /*extern "C"*/
#endif
