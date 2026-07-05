#include "resources.h"

// MARK: icons
extern const lv_image_dsc_t battery_32px;
extern const lv_image_dsc_t battery_low_32px;
extern const lv_image_dsc_t battery_medium_32px;
extern const lv_image_dsc_t battery_full_32px;
extern const lv_image_dsc_t card_sd_80px;
extern const lv_image_dsc_t cog_80px;
extern const lv_image_dsc_t images_80px;
extern const lv_image_dsc_t square_arrow_right_enter_80px;

// MARK: fonts
extern const lv_font_t lucide_40;
extern const lv_font_t noto_sans_jp_24;
extern const lv_font_t noto_sans_jp_32;
extern const lv_font_t noto_sans_jp_48;

const struct Resources R = {
    .icon = {
        .battery_32px = &battery_32px,
        .battery_low_32px = &battery_low_32px,
        .battery_medium_32px = &battery_medium_32px,
        .battery_full_32px = &battery_full_32px,
        .card_sd_80px = &card_sd_80px,
        .cog_80px = &cog_80px,
        .images_80px = &images_80px,
        .square_arrow_right_enter_80px = &square_arrow_right_enter_80px,
    },
    .font = {
        .lucide_40 = &lucide_40,
        .noto_sans_jp_24 = &noto_sans_jp_24,
        .noto_sans_jp_32 = &noto_sans_jp_32,
        .noto_sans_jp_48 = &noto_sans_jp_48,
    },
};
