cmake_minimum_required(VERSION 3.16)
project(chpm_host_tests LANGUAGES C)

enable_testing()

add_executable(chpm_param_store_tests
    test_param_store.c
    mock_flash.c
    ../../user/app/param.c
)
target_include_directories(chpm_param_store_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../user/app
)
target_compile_features(chpm_param_store_tests PRIVATE c_std_99)
if(MSVC)
    string(REPLACE "/DNDEBUG" "" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
    target_compile_options(chpm_param_store_tests PRIVATE /W4 /WX)
else()
    target_compile_options(chpm_param_store_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror -UNDEBUG
    )
endif()
add_test(NAME chpm_param_store_all COMMAND chpm_param_store_tests)
