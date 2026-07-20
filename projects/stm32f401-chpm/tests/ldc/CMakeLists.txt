cmake_minimum_required(VERSION 3.16)

project(chpm_ldc_tests C)

enable_testing()
set(LDC_BUILD_TESTS ON CACHE BOOL "Build the canonical LDC tests" FORCE)
add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../../third_party/ldc"
    "${CMAKE_CURRENT_BINARY_DIR}/ldc"
)
