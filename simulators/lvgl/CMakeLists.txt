cmake_minimum_required(VERSION 3.12)

project(lvgl_simulator LANGUAGES C CXX)

# Point to the existing LVGL v9.2.2 source tree in the STM32 project
set(LVGL_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../STM32H563_App/user/ui/lvgl/lvgl)
set(H5_UI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../STM32H563_App/user/ui/lvgl)

# Use our PC-specific lv_conf.h
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE STRING "lv_conf path" FORCE)
set(LV_LVGL_H_INCLUDE_SIMPLE ON CACHE BOOL "simple include" FORCE)
set(LV_CONF_INCLUDE_SIMPLE ON CACHE BOOL "simple conf include" FORCE)

# Disable examples and demos to speed up build
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "disable examples" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "disable demos" FORCE)

# Add LVGL as subdirectory
add_subdirectory(${LVGL_SRC_DIR} ${CMAKE_BINARY_DIR}/lvgl_build)

# Fix: LVGL CMakeLists sets dllimport on INTERFACE for MSVC,
# but we are building a static lib. Override it to empty.
get_target_property(_lvgl_defs lvgl INTERFACE_COMPILE_DEFINITIONS)
if(_lvgl_defs)
    list(FIND _lvgl_defs "LV_ATTRIBUTE_EXTERN_DATA=__declspec(dllimport)" _idx)
    if(NOT _idx EQUAL -1)
        list(REMOVE_AT _lvgl_defs ${_idx})
    endif()
    set_target_properties(lvgl PROPERTIES INTERFACE_COMPILE_DEFINITIONS "${_lvgl_defs}")
endif()

# Simulator executable
add_executable(lvgl_simulator WIN32
    src/main.c
    src/ui_asset_store_sim.c
    ${H5_UI_DIR}/sim_ui.c
    ${H5_UI_DIR}/sim_data.c
    ${H5_UI_DIR}/boot_eye_img.c
    ${H5_UI_DIR}/ref_page_img.c
    ${H5_UI_DIR}/font_cjk_12.c
    ${H5_UI_DIR}/font_cjk_14.c
    ${H5_UI_DIR}/font_cjk_18.c
    ${H5_UI_DIR}/font_cjk_20.c
    ${H5_UI_DIR}/font_cjk_28.c
)

target_include_directories(lvgl_simulator PRIVATE
    ${H5_UI_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${LVGL_SRC_DIR}
    ${LVGL_SRC_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(lvgl_simulator PRIVATE
    _CRT_SECURE_NO_WARNINGS
    UNICODE _UNICODE
)

target_link_libraries(lvgl_simulator PRIVATE
    lvgl
    gdi32
    user32
)

# MSVC settings
if(MSVC)
    target_compile_options(lvgl_simulator PRIVATE /W3 /utf-8)
endif()

# Output location
set_target_properties(lvgl_simulator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
