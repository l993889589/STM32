cmake_minimum_required(VERSION 3.16)

project(dwin_protocol VERSION 1.0.0 LANGUAGES C)

option(DWIN_PROTOCOL_BUILD_TESTS "Build dwin_protocol host tests" ON)
if(MSVC)
    string(REPLACE "/DNDEBUG" "" CMAKE_C_FLAGS_RELEASE
           "${CMAKE_C_FLAGS_RELEASE}")
endif()

add_library(dwin_protocol STATIC
    dwin_protocol.c
    dwin_rx_parser.c
)
target_include_directories(dwin_protocol PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(dwin_protocol PUBLIC c_std_99)

if(MSVC)
    target_compile_options(dwin_protocol PRIVATE /W4 /WX)
else()
    target_compile_options(dwin_protocol PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

if(DWIN_PROTOCOL_BUILD_TESTS)
    enable_testing()
    add_executable(dwin_protocol_tests tests/test_dwin_protocol.c)
    target_link_libraries(dwin_protocol_tests PRIVATE dwin_protocol)
    if(MSVC)
        target_compile_options(dwin_protocol_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(dwin_protocol_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror -UNDEBUG
        )
    endif()
    add_test(NAME dwin_protocol_all COMMAND dwin_protocol_tests)
endif()
