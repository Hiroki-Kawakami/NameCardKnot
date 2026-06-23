#include "resources.h"

// MARK: icons
extern const lv_image_dsc_t card_sd_80px;
extern const lv_image_dsc_t cog_80px;
extern const lv_image_dsc_t images_80px;
extern const lv_image_dsc_t square_arrow_right_enter_80px;

// MARK: fonts
extern const lv_font_t lucide_40;

const struct Resources R = {
    .icon = {
        .card_sd_80px = &card_sd_80px,
        .cog_80px = &cog_80px,
        .images_80px = &images_80px,
        .square_arrow_right_enter_80px = &square_arrow_right_enter_80px,
    },
    .font = {
        .lucide_40 = &lucide_40,
    },
};
