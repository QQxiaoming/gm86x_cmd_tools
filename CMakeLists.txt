cmake_minimum_required(VERSION 3.16)
project(gm86x_cmd_i2c_tools_cpp LANGUAGES CXX)

execute_process(
    COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" describe --always --long --dirty --abbrev=8 --tags
    OUTPUT_VARIABLE GM86X_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GM86X_GIT_COMMIT)
    set(GM86X_GIT_COMMIT "unknown")
endif()
execute_process(
    COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" describe --tags --abbrev=0
    OUTPUT_VARIABLE PROJECT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT PROJECT_VERSION)
    set(PROJECT_VERSION "unknown")
endif()
string(TIMESTAMP GM86X_BUILD_TIME "%Y-%m-%d %H:%M:%S UTC" UTC)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/build_info.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/gm86x/build_info.hpp"
    @ONLY
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(gm86x_configure_build_info target)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(${target} PRIVATE "GM86X_BUILD_TYPE=\"$<IF:$<BOOL:$<CONFIG>>,$<CONFIG>,Unspecified>\"")
endfunction()

function(gm86x_enable_warnings target)
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

set(PERCIPIO_REGADDR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/percipio_regaddr_convert")

add_library(percipio_regaddr STATIC
    "${PERCIPIO_REGADDR_DIR}/src/regaddr_convert.cpp"
)
target_include_directories(percipio_regaddr
    PUBLIC core/include
    PRIVATE "${PERCIPIO_REGADDR_DIR}/src"
)
gm86x_enable_warnings(percipio_regaddr)

add_executable(regaddr_convert
    "${PERCIPIO_REGADDR_DIR}/test/regaddr_convert_main.cpp"
)
target_include_directories(regaddr_convert PRIVATE "${PERCIPIO_REGADDR_DIR}/src")
target_link_libraries(regaddr_convert PRIVATE percipio_regaddr)
gm86x_enable_warnings(regaddr_convert)

# The checked-in header keeps normal and standalone builds Python/CSV independent.
# Invoke this target explicitly after changing TYGenICamReg register CSV files.
set(PERCIPIO_REGADDR_CSV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../TYGenICamReg/csv")
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND AND EXISTS "${PERCIPIO_REGADDR_CSV_DIR}")
    add_custom_target(regenerate_regaddr_csv
        COMMAND
            "${Python3_EXECUTABLE}"
            "${PERCIPIO_REGADDR_DIR}/tools/gen_regaddr_csv_data.py"
            --csv-dir "${PERCIPIO_REGADDR_CSV_DIR}"
            --output "${PERCIPIO_REGADDR_DIR}/src/regaddr_csv_data.h"
        WORKING_DIRECTORY "${PERCIPIO_REGADDR_DIR}"
        COMMENT "Regenerating embedded GenICam register CSV data"
        VERBATIM
    )
endif()

add_library(gm86x_core
    core/src/commands/basic_commands.cpp
    core/src/commands/post_processor_commands.cpp
    core/src/commands/gige_commands.cpp
    core/src/commands/register_commands.cpp
    core/src/commands/partition_commands.cpp
    core/src/commands.cpp
    core/src/bytes.cpp
    core/src/protocol.cpp
)
target_include_directories(gm86x_core PUBLIC core/include)
target_link_libraries(gm86x_core PRIVATE percipio_regaddr)
gm86x_enable_warnings(gm86x_core)

if(WIN32)
    add_subdirectory(windows_uvc/libusb_win)

	add_executable(gm86x_cmd_uvc_tools
    windows_uvc/windows_usb_xu.cpp
        windows_uvc/main.cpp)
    gm86x_configure_build_info(gm86x_cmd_uvc_tools)
	target_include_directories(gm86x_cmd_uvc_tools PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/windows_uvc/libusb_win)
	target_link_libraries(gm86x_cmd_uvc_tools PRIVATE
    libusb gm86x_core setupapi advapi32)
	target_compile_definitions(gm86x_cmd_uvc_tools PRIVATE NOMINMAX)
	gm86x_enable_warnings(gm86x_cmd_uvc_tools)
else()
    add_executable(gm86x_cmd_i2c_tools 
        linux_i2c/linux_i2c.cpp
        linux_i2c/media_discovery.cpp
        linux_i2c/main.cpp)
    gm86x_configure_build_info(gm86x_cmd_i2c_tools)
    target_link_libraries(gm86x_cmd_i2c_tools PRIVATE gm86x_core)
    gm86x_enable_warnings(gm86x_cmd_i2c_tools)

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
            gm86x_configure_build_info(gm86x_cmd_uvc_tools)
            target_link_libraries(gm86x_cmd_uvc_tools PRIVATE gm86x_core PkgConfig::LIBUSB)
            gm86x_enable_warnings(gm86x_cmd_uvc_tools)
        else()
            message(STATUS "libusb-1.0 not available for '${GM86X_CXX_TARGET_TRIPLE}', skipping gm86x_cmd_uvc_tools (host-only tool)")
        endif()
    else()
        message(STATUS "PkgConfig not found, skipping gm86x_cmd_uvc_tools (host-only tool)")
    endif()
endif()
