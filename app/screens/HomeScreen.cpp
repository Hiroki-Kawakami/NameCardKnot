#include "HomeScreen.hpp"

void HomeScreen::build() {
    lv_obj_t *label = lv_label_create(root_);
    lv_label_set_text(label, "HomeScreen");
    lv_obj_center(label);
}
