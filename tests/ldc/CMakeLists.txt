cmake_minimum_required(VERSION 3.16)

project(ldc_host_tests C)

enable_testing()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(LDC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../shared/ldc")

add_library(ldc_core STATIC
    "${LDC_ROOT}/ldc_core.c"
    "${LDC_ROOT}/ldc_ring.c"
    "${LDC_ROOT}/ldc_packet.c"
)
target_include_directories(ldc_core PUBLIC "${LDC_ROOT}")

if(MSVC)
    target_compile_options(ldc_core PRIVATE /W4 /WX)
else()
    target_compile_options(ldc_core PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()

add_executable(test_ldc test_ldc.c)
target_link_libraries(test_ldc PRIVATE ldc_core)
if(MSVC)
    target_compile_options(test_ldc PRIVATE /W4 /WX)
else()
    target_compile_options(test_ldc PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()

add_test(NAME ldc_all COMMAND test_ldc)
