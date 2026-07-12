cmake_minimum_required(VERSION 3.16)

project(usb_vendor_protocol_host_tests C)

enable_testing()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(USB_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

add_executable(test_usb_vendor_protocol
    test_vendor_protocol.c
    "${USB_ROOT}/usb_vendor_protocol.c"
)
target_include_directories(test_usb_vendor_protocol PRIVATE "${USB_ROOT}")

if(MSVC)
    target_compile_options(test_usb_vendor_protocol PRIVATE /W4 /WX)
else()
    target_compile_options(test_usb_vendor_protocol PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()

add_test(NAME usb_vendor_protocol_all COMMAND test_usb_vendor_protocol)
