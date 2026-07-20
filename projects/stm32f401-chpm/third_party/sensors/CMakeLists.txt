cmake_minimum_required(VERSION 3.16)
project(sensors LANGUAGES C)

option(SENSORS_BUILD_TESTS "Build portable sensor driver tests" OFF)

add_library(sensors STATIC
    src/aht20.c
    src/ds18b20.c
)
target_include_directories(sensors PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(sensors PUBLIC c_std_99)

if(MSVC)
    target_compile_options(sensors PRIVATE /W4 /WX)
else()
    target_compile_options(sensors PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(SENSORS_BUILD_TESTS)
    enable_testing()

    add_executable(aht20_tests tests/test_aht20.c)
    target_link_libraries(aht20_tests PRIVATE sensors)
    target_compile_features(aht20_tests PRIVATE c_std_99)

    add_executable(ds18b20_tests tests/test_ds18b20.c)
    target_link_libraries(ds18b20_tests PRIVATE sensors)
    target_compile_features(ds18b20_tests PRIVATE c_std_99)

    if(MSVC)
        string(REPLACE "/DNDEBUG" "" CMAKE_C_FLAGS_RELEASE
               "${CMAKE_C_FLAGS_RELEASE}")
        target_compile_options(aht20_tests PRIVATE /W4 /WX)
        target_compile_options(ds18b20_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(aht20_tests PRIVATE
            -UNDEBUG -Wall -Wextra -Wpedantic -Werror
        )
        target_compile_options(ds18b20_tests PRIVATE
            -UNDEBUG -Wall -Wextra -Wpedantic -Werror
        )
    endif()

    add_test(NAME aht20_all COMMAND aht20_tests)
    add_test(NAME ds18b20_all COMMAND ds18b20_tests)
endif()
