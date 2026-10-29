# gm86x command tools

The I2C and UVC tools share the same command implementation.

## Building

CMake 3.16+ and a C++20 compiler are required. zlib is optional.
Linux UVC support additionally requires libusb-1.0 development files.
CMake checks whether zlib can compile and link with the selected toolchain.
If it is missing or incompatible, tools still build with raw ZIP download support,
but `.xml` output and printing XML report an explicit error before downloading.
Cross builds should provide zlib for the target architecture, not the host.
Use `-DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=TRUE` to build without XML extraction explicitly.

```sh
cmake -S . -B build_pc -DCMAKE_BUILD_TYPE=Release
cmake --build build_pc
```

For Windows, install `zlib:x64-windows-static-md` using vcpkg and configure CMake
with its toolchain file and `-DVCPKG_TARGET_TRIPLET=x64-windows-static-md`,
as in [the CI workflow](.github/workflows/build.yml).
