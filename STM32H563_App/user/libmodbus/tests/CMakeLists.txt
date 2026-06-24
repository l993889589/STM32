cmake_minimum_required(VERSION 3.16)

project(modbus_host_tests C)

enable_testing()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(MODBUS_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

add_executable(test_modbus
    test_modbus.c
    "${MODBUS_ROOT}/modbus.c"
    "${MODBUS_ROOT}/modbus-data.c"
)
target_include_directories(test_modbus PRIVATE "${MODBUS_ROOT}")

if(MSVC)
    target_compile_options(test_modbus PRIVATE /W4 /WX)
else()
    target_compile_options(test_modbus PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()

add_test(NAME modbus_all COMMAND test_modbus)
