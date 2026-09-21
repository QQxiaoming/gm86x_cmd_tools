cmake_minimum_required(VERSION 3.10)
project(libusb)

file(GLOB LIBUSB_SOURCES "*.c")

add_library(libusb STATIC ${LIBUSB_SOURCES})

target_include_directories(libusb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(libusb PRIVATE _WIN32_WINNT=0x0600)