cmake_minimum_required(VERSION 3.16)
include(${CMAKE_CURRENT_SOURCE_DIR}/../esp-devkit/devkit.cmake)
devkit_simulator_init()
project(simulator C CXX)
devkit_simulator(
    BOARD paper_s3
    LV_CONF_DIR main
    DEFAULT_ROTATION 90
    COMPONENT_DIRS
        ../components/image_processor
        ../components/namecard_pdf
        ../components/dokan
        ../app)
