cmake_minimum_required(VERSION 3.16)

project(at_host_tests C)

enable_testing()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(AT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../STM32H563_App/user/at")

add_executable(test_at_command_plan
    test_at_command_plan.c
    "${AT_ROOT}/core/at_command_plan.c"
)
target_include_directories(test_at_command_plan PRIVATE
    "${AT_ROOT}/core"
    "${AT_ROOT}/modules"
)

if(MSVC)
    target_compile_options(test_at_command_plan PRIVATE /W4 /WX)
else()
    target_compile_options(test_at_command_plan PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()

add_test(NAME at_command_plan COMMAND test_at_command_plan)
