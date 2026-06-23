#include "HomeScreen.hpp"
#include "resources.h"

void HomeScreen::build() {
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(root_, 20, 0);

    { // Title
        lv_obj_t *title = lv_label_create(root_);
        lv_label_set_text(title, "Name Card Knot");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);

        lv_obj_t *version = lv_label_create(root_);
        lv_label_set_text(version, "v0.1.0");
        lv_obj_set_style_text_font(version, &lv_font_montserrat_24, 0);

        lv_obj_set_style_pad_top(title, 40, 0);
        lv_obj_set_style_pad_bottom(version, 40, 0);
    }

    { // Buttons
        auto button = [](lv_obj_t *parent, const void *icon, const char *title) {
            auto button = lv_button_create(parent);
            lv_obj_set_size(button, 200, 140);
            lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_border_width(button, 2, 0);
            lv_obj_set_style_radius(button, 8, 0);
            lv_obj_set_style_border_color(button, lv_color_white(), 0);
            lv_obj_set_style_border_color(button, lv_color_black(), LV_STATE_PRESSED);

            auto image = lv_image_create(button);
            lv_image_set_src(image, icon);
            lv_obj_set_style_image_recolor(image, lv_color_black(), 0);
            lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);

            auto label = lv_label_create(button);
            lv_label_set_text(label, title);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        };

        auto row1 = lv_container_create(root_, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(row1, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_ver(row1, 10, 0);
        lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        button(row1, R.icon.card_sd_80px, "Open from SD");
        lv_ver_separator_create(row1);
        button(row1, R.icon.images_80px, "Gallery");

        lv_hor_separator_create(root_, 20);

        auto row2 = lv_container_create(root_, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(row2, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_ver(row2, 10, 0);
        lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        button(row2, R.icon.square_arrow_right_enter_80px, "Receive");
        lv_ver_separator_create(row2);
        button(row2, R.icon.cog_80px, "Settings");
    }
}
