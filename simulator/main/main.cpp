#include "NameCardKnot.hpp"
#include "sim_harness.h"
#include "lvgl.hpp"
#include "nvs_flash.h"
#include <cstdio>
#include <cstdlib>

extern "C" int main(void) {
    // Headless verification must not inherit NVS state from interactive runs;
    // SIMULATOR_NVS_PATH overrides for scripts that test resume.
    if (const char *nvs_path = getenv("SIMULATOR_NVS_PATH")) {
        nvs_flash_sim_set_path(nvs_path);
    } else if (getenv("SIMULATOR_HEADLESS")) {
        remove("build/nvs_headless.json");
        nvs_flash_sim_set_path("build/nvs_headless.json");
    }
    app_entry();

    // Scripted UI verification: if SIMULATOR_SCRIPT names a script, the harness
    // interpreter runs on its own thread and steps in lockstep with this loop's
    // frames. NULL (env unset) is a no-op interactive run. sdl_panel registered
    // its input/capture callbacks during bsp_init.
    sim_harness_start(getenv("SIMULATOR_SCRIPT"));
    lvgl_sim_loop(sim_harness_frame);
    return sim_harness_exit_code();
}
