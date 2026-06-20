#include "NameCardKnot.hpp"
#include "sdl_panel.h"
#include "sim_harness.h"
#include <cstdlib>
#include <unistd.h>

extern "C" int main(void) {
    app_entry();

    // Scripted UI verification: if SIMULATOR_SCRIPT names a script, the harness
    // interpreter runs on its own thread and steps in lockstep with this loop's
    // frames. NULL (env unset) is a no-op interactive run. sdl_panel registered
    // its input/capture callbacks during bsp_init.
    sim_harness_start(getenv("SIMULATOR_SCRIPT"));

    while (true) {
        sdl_panel_pump_input();
        sdl_panel_present();
        // No LVGL yet, so the UI is always "idle"; once LVGL is added pass e.g.
        // lv_anim_count_running() == 0 here for `settle`.
        if (!sim_harness_frame(true)) break;
        usleep(1000);
    }
    return sim_harness_exit_code();
}
