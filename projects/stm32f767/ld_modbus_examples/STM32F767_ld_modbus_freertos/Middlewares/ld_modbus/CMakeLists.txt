cmake_minimum_required(VERSION 3.16)

project(ld_modbus VERSION 0.1.0 LANGUAGES C)

option(LD_MODBUS_BUILD_TESTS "Build ld_modbus host tests" ON)
set(LD_MODBUS_LDC_ROOT "" CACHE PATH "Optional directory containing ldc_easy.c and headers")

add_library(ld_modbus STATIC
    src/ld_modbus_client.c
    src/ld_modbus_core.c
    src/ld_modbus_rtu_framer.c
    src/ld_modbus_server.c
)

target_include_directories(ld_modbus PUBLIC include)
target_compile_features(ld_modbus PUBLIC c_std_99)

if(MSVC)
    target_compile_options(ld_modbus PRIVATE /W4 /WX)
else()
    target_compile_options(ld_modbus PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(LD_MODBUS_BUILD_TESTS)
    enable_testing()
    add_executable(ld_modbus_tests tests/test_ld_modbus.c)
    target_link_libraries(ld_modbus_tests PRIVATE ld_modbus)
    if(MSVC)
        target_compile_options(ld_modbus_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(ld_modbus_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME ld_modbus_all COMMAND ld_modbus_tests)

    add_executable(ld_modbus_rtu_framer_tests
        tests/test_ld_modbus_rtu_framer.c
    )
    target_link_libraries(ld_modbus_rtu_framer_tests PRIVATE ld_modbus)
    if(MSVC)
        target_compile_options(ld_modbus_rtu_framer_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(ld_modbus_rtu_framer_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror
        )
    endif()
    add_test(NAME ld_modbus_rtu_framer_all COMMAND ld_modbus_rtu_framer_tests)

    if(LD_MODBUS_LDC_ROOT)
        add_library(ld_modbus_test_ldc STATIC
            "${LD_MODBUS_LDC_ROOT}/ldc_core.c"
            "${LD_MODBUS_LDC_ROOT}/ldc_easy.c"
            "${LD_MODBUS_LDC_ROOT}/ldc_packet.c"
            "${LD_MODBUS_LDC_ROOT}/ldc_ring.c"
        )
        target_include_directories(ld_modbus_test_ldc PUBLIC "${LD_MODBUS_LDC_ROOT}")
        target_compile_features(ld_modbus_test_ldc PUBLIC c_std_99)

        add_library(ld_modbus_ldc STATIC integrations/ldc/ld_modbus_ldc.c)
        target_include_directories(ld_modbus_ldc PUBLIC integrations/ldc)
        target_link_libraries(ld_modbus_ldc PUBLIC ld_modbus ld_modbus_test_ldc)

        add_executable(ld_modbus_ldc_tests tests/test_ld_modbus_ldc.c)
        target_link_libraries(ld_modbus_ldc_tests PRIVATE ld_modbus_ldc)
        if(MSVC)
            target_compile_options(ld_modbus_ldc_tests PRIVATE /W4 /WX)
        else()
            target_compile_options(ld_modbus_test_ldc PRIVATE -Wall -Wextra -Wpedantic -Werror)
            target_compile_options(ld_modbus_ldc PRIVATE -Wall -Wextra -Wpedantic -Werror)
            target_compile_options(ld_modbus_ldc_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME ld_modbus_ldc_all COMMAND ld_modbus_ldc_tests)
    endif()
endif()
