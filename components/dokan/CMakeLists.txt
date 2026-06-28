# dokan — establish a P2P link from a connection descriptor exchanged over a
# short-range channel. Spec: docs/dokan.md.
#
# Same sources build on device (../components container) and host (the simulator's
# idf_component_register shim). Device-only code is fenced behind ESP_PLATFORM so
# the pure descriptor codec also builds in test/run.sh (gcc, no ESP-IDF).
#
# SRCS is globbed; CONFIGURE_DEPENDS is unusable here (see app/CMakeLists.txt),
# so re-run cmake after adding/removing files. test/ is host-only, outside src/.
file(GLOB_RECURSE DOKAN_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

idf_component_register(
    SRCS ${DOKAN_SRCS}
    INCLUDE_DIRS "inc"
    PRIV_INCLUDE_DIRS "src"
    PRIV_REQUIRES esp_hw_support freertos)  # esp_fill_random (seed RNG) + session lock
