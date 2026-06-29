# image_processor — JPEG/PNG decode -> grayscale/dither pipeline for the EPD
# panel. Pure orchestration over esp-devkit/libs/image_framework (decode,
# resize, recolor, dither, pack, async); LVGL-free — it outputs a plain buffer
# the app wraps into an lv_image_dsc_t.
#
# Same sources build for both targets: on device via EXTRA_COMPONENT_DIRS
# (../components is a component container), on the host via the
# idf_component_register shim in simulator/CMakeLists.txt. The pure pipeline +
# decoders are also exercised headless by test/run.sh (g++, no ESP-IDF), so
# anything device-only must be fenced behind ESP_PLATFORM.
#
# SRCS is globbed; CONFIGURE_DEPENDS is unusable here (see app/CMakeLists.txt),
# so re-run cmake after adding/removing files. test/ is host-only, outside src/.
file(GLOB_RECURSE IMAGE_PROCESSOR_SRCS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

idf_component_register(
    SRCS ${IMAGE_PROCESSOR_SRCS}
    INCLUDE_DIRS "inc"
    PRIV_INCLUDE_DIRS "src"
    # image_framework (decode/resize/recolor/dither/pack/async), esp_timer
    # (IMGPROC_PROFILE), freertos (reads core/prio to configure imgf_async).
    PRIV_REQUIRES image_framework esp_timer freertos)
