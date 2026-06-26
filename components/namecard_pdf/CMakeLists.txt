# namecard_pdf — read the editor's NameCardKnot container PDF: extract the
# name/url/message + embedded JPEGs from the appended NCK footer, and write a
# share-only PDF by truncation. The PDF body is never parsed. No external
# dependencies and LVGL-free — the name_glyphs blob is returned as raw data
# (the app wraps it into an lv_font).
#
# Same sources build for both targets: on device via EXTRA_COMPONENT_DIRS
# (../components is a component container), on the host via the
# idf_component_register shim in simulator/CMakeLists.txt. The parser is also
# exercised headless by test/run.sh (g++, no ESP-IDF), so anything device-only
# must be fenced behind ESP_PLATFORM. test/ is host-only, outside src/.
file(GLOB_RECURSE NAMECARD_PDF_SRCS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

idf_component_register(
    SRCS ${NAMECARD_PDF_SRCS}
    INCLUDE_DIRS "inc"
    PRIV_INCLUDE_DIRS "src")
