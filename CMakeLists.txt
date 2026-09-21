cmake_minimum_required(VERSION 3.16)
project(gm86x_cmd_i2c_tools_cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(gm86x_core
    core/src/commands/basic_commands.cpp
    core/src/commands/gige_commands.cpp
    core/src/commands/register_commands.cpp
    core/src/commands/partition_commands.cpp
    core/src/commands.cpp
    core/src/bytes.cpp
    core/src/protocol.cpp
)
target_include_directories(gm86x_core PUBLIC core/include)
if(NOT MSVC)
    target_compile_options(gm86x_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    add_subdirectory(windows_uvc/libusb_win)

	add_executable(gm86x_cmd_uvc_tools
    windows_uvc/windows_usb_xu.cpp
        windows_uvc/main.cpp)
	target_include_directories(gm86x_cmd_uvc_tools PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/windows_uvc/libusb_win)
	target_link_libraries(gm86x_cmd_uvc_tools PRIVATE
    libusb gm86x_core setupapi advapi32)
	target_compile_definitions(gm86x_cmd_uvc_tools PRIVATE NOMINMAX)
	if(NOT MSVC)
	    target_compile_options(gm86x_cmd_uvc_tools PRIVATE -Wall -Wextra -Wpedantic)
	endif()
else()
    add_executable(gm86x_cmd_i2c_tools 
        linux_i2c/linux_i2c.cpp
        linux_i2c/media_discovery.cpp
        linux_i2c/main.cpp)
    target_link_libraries(gm86x_cmd_i2c_tools PRIVATE gm86x_core)
    if(NOT MSVC)
        target_compile_options(gm86x_cmd_i2c_tools PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        # libusb-1.0 is only available for the host toolchain; the aarch64 cross
        # toolchain sysroot doesn't bundle it. build.sh cross-compiles by swapping in
        # the aarch64 compiler binary without setting CMAKE_SYSTEM_NAME, so
        # CMAKE_CROSSCOMPILING stays OFF and pkg-config would otherwise still resolve
        # the (wrong-ABI) host libusb-1.0 -- detect the compiler's real target triple
        # instead and skip libusb whenever it doesn't match the host architecture.
        execute_process(
            COMMAND ${CMAKE_CXX_COMPILER} -dumpmachine
            OUTPUT_VARIABLE GM86X_CXX_TARGET_TRIPLE
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        set(GM86X_IS_CROSS_TOOLCHAIN FALSE)
        if(GM86X_CXX_TARGET_TRIPLE AND NOT GM86X_CXX_TARGET_TRIPLE MATCHES "^${CMAKE_HOST_SYSTEM_PROCESSOR}")
            set(GM86X_IS_CROSS_TOOLCHAIN TRUE)
        endif()

        if(NOT GM86X_IS_CROSS_TOOLCHAIN)
            pkg_check_modules(LIBUSB QUIET IMPORTED_TARGET libusb-1.0)
        endif()

        if(LIBUSB_FOUND)
            add_executable(gm86x_cmd_uvc_tools 
                linux_uvc/linux_usb_xu.cpp
                linux_uvc/usb_discovery.cpp
                linux_uvc/main.cpp)
            target_link_libraries(gm86x_cmd_uvc_tools PRIVATE gm86x_core PkgConfig::LIBUSB)
            if(NOT MSVC)
                target_compile_options(gm86x_cmd_uvc_tools PRIVATE -Wall -Wextra -Wpedantic)
            endif()
        else()
            message(STATUS "libusb-1.0 not available for '${GM86X_CXX_TARGET_TRIPLE}', skipping gm86x_cmd_uvc_tools (host-only tool)")
        endif()
    else()
        message(STATUS "PkgConfig not found, skipping gm86x_cmd_uvc_tools (host-only tool)")
    endif()
endif()
