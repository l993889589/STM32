cmake_minimum_required(VERSION 3.20)
project(app_msg_bus_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(test_msg_bus
    test_msg_bus.c
    "${CMAKE_CURRENT_LIST_DIR}/../../shared/comm/msg_bus/app_msg_bus.c"
)

target_include_directories(test_msg_bus PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../shared/comm/msg_bus"
)

if(MSVC)
    target_compile_options(test_msg_bus PRIVATE /W4 /WX)
else()
    target_compile_options(test_msg_bus PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()

enable_testing()
add_test(NAME app_msg_bus_all COMMAND test_msg_bus)
