cmake_minimum_required(VERSION 3.10)
project(libusb)

# Only build the Windows backend; the directory also contains other OS
# backends (darwin/linux/netbsd/openbsd/sunos/haiku/null) which must not be
# compiled together, as they define conflicting symbols (e.g. usbi_backend).
set(LIBUSB_SOURCES
	core.c
	descriptor.c
	hotplug.c
	io.c
	strerror.c
	sync.c
	events_windows.c
	threads_windows.c
	windows_common.c
	windows_usbdk.c
	windows_winusb.c
)

add_library(libusb STATIC ${LIBUSB_SOURCES})

target_include_directories(libusb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(libusb PRIVATE _WIN32_WINNT=0x0600)