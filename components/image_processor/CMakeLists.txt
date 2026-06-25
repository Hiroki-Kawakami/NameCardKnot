# image_processor — self-contained JPEG/PNG decode + grayscale/dither pipeline
# for the EPD panel. No external dependencies: the baseline-JPEG and PNG
# decoders (inflate included) are in-tree, and the component is LVGL-free — it
# outputs a plain buffer the app wraps into an lv_image_dsc_t.
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
    PRIV_REQUIRES esp_timer freertos)  # esp_timer (IMGPROC_PROFILE) + freertos (IMGPROC_ASYNC)
