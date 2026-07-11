cmake_minimum_required(VERSION 3.16)
project(ld_modbus VERSION 0.1.0 LANGUAGES C)

option(LD_MODBUS_BUILD_TESTS "Build host-side protocol tests" ON)

add_library(ld_modbus STATIC
    src/ld_modbus_core.c
    src/ld_modbus_client.c
    src/ld_modbus_server.c
)
add_library(ld_modbus::ld_modbus ALIAS ld_modbus)

target_include_directories(ld_modbus PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(ld_modbus PUBLIC c_std_99)

if(MSVC)
    target_compile_options(ld_modbus PRIVATE /W4 /WX)
else()
    target_compile_options(ld_modbus PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(LD_MODBUS_BUILD_TESTS)
    enable_testing()
    add_executable(ld_modbus_tests tests/test_ld_modbus.c)
    target_link_libraries(ld_modbus_tests PRIVATE ld_modbus::ld_modbus)
    if(MSVC)
        target_compile_options(ld_modbus_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(ld_modbus_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME ld_modbus_protocol COMMAND ld_modbus_tests)
endif()

include(GNUInstallDirs)
install(TARGETS ld_modbus EXPORT ld_modbusTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT ld_modbusTargets
    FILE ld_modbusTargets.cmake
    NAMESPACE ld_modbus::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ld_modbus
)
