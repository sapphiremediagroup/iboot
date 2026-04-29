#pragma once

#include <UEFI/Uefi.h>

enum class MemoryType : unsigned int {
    Unusable        = 0,
    Free            = 1,
    KernelCode      = 2,
    KernelData      = 3,
    BootloaderData  = 4,
    AcpiReclaimable = 5,
    AcpiNvs         = 6,
    MmioRegion      = 7,
    Reserved        = 8
};

struct MemoryRegion {
    unsigned long long   base;
    unsigned long long   pages;
    MemoryType type;
    unsigned int   _pad;

    constexpr unsigned long long bytes()  const { return pages * 4096ULL; }
    constexpr unsigned long long end()    const { return base + bytes();   }
};

struct MemoryMap {
    MemoryRegion* regions;
    unsigned long long      count;
    unsigned long long      capacity;

    unsigned long long totalBytes;
    unsigned long long freeBytes;
    unsigned long long kernelBytes;
    unsigned long long reservedBytes;
};

enum class PixelFormat : unsigned int {
    RGBReserved  = 0,
    BGRReserved  = 1,
    BitMask      = 2,
    BltOnly      = 3
};

struct Framebuffer {
    UINT64      base;
    UINT64      size;
    UINT32      width;
    UINT32      height;
    UINT32      pixelsPerScanLine;
    PixelFormat format;
    UINT32      redMask;
    UINT32      greenMask;
    UINT32      blueMask;
};

struct BootInfo {
    MemoryMap   memoryMap;
    Framebuffer framebuffer;
    UINT64      kernelBase;
    UINT64      kernelSize;
    UINT64      rsdp;
    UINT64      initrdBase;
    UINT64      initrdSize;
    UINT64      imageBase;
};

class MemoryManager {
public:
    static EFI_STATUS Initialize(EFI_SYSTEM_TABLE* st);

    static EFI_STATUS PrepareForKernel(EFI_SYSTEM_TABLE* st);

    static const MemoryMap& GetMap();
    static unsigned long long TotalBytes();
    static unsigned long long FreeBytes();

    MemoryManager()                              = delete;
    MemoryManager(const MemoryManager&)          = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;

private:
    static MemoryMap   s_map;
    static bool        s_initialized;

    static MemoryType  TranslateEfiType(EFI_MEMORY_TYPE efiType);
    static const char* TypeName(MemoryType t);
    static void        FormatSize(unsigned long long bytes, char* buf, unsigned int len);
};