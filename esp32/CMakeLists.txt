cmake_minimum_required(VERSION 3.16.0)
# Shared components: ../components is a container (each subdir is a component),
# ../app is itself a single component (it has its own CMakeLists.txt).
# esp-devkit/bsp, ui_framework, and libs/image_framework are shared components from the submodule.
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/../components
    ${CMAKE_CURRENT_SOURCE_DIR}/../esp-devkit/bsp
    ${CMAKE_CURRENT_SOURCE_DIR}/../esp-devkit/ui_framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../esp-devkit/libs/image_framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../app)
# Select the BSP board for this target (no runtime detection; one target = one board).
set(BSP_BOARD paper CACHE STRING "esp-devkit BSP board")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(NameCardKnot)
