cmake_minimum_required(VERSION 3.16)

project(shell_host_tests C)

enable_testing()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(SHELL_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

add_executable(test_shell
    test_shell.c
    "${SHELL_ROOT}/shell.c"
)
target_include_directories(test_shell PRIVATE "${SHELL_ROOT}")

if(MSVC)
    target_compile_options(test_shell PRIVATE /W4 /WX)
else()
    target_compile_options(test_shell PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()

add_test(NAME shell_all COMMAND test_shell)
