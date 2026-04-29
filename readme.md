# iBoot

iBoot is a small x86_64 UEFI bootloader for InstantOS. 

## Features
- [x] Kernel loading
- [x] Initial RamDisk support
- [x] Framebuffer & memory map
- [x] Advanced Configuration and Power Interface

## Build

This project expects Clang and CMake 3.20 or newer. The top-level or parent
CMake project should define the platform flags used by this target:

```cmake
set(BASE_CXX_FLAGS ...)
set(UEFI_CXX_FLAGS ...)
set(EFI_LINK_OPTIONS ...)
set(ISO_EFI_DIR "${CMAKE_BINARY_DIR}/iso/EFI/BOOT")
```

Standalone build:

```sh
cmake -S . -B build
cmake --build build
```

## CMake Integration

```cmake
target_link_libraries(my_target PRIVATE iboot::headers)
```