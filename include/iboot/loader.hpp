#pragma once

#include <UEFI/Uefi.h>
#include <UEFI/Protocol/SimpleFileSystem.h>
#include <iboot/memory.hpp>

using KernelEntry = void (*)(BootInfo*);

class KernelLoader {
public:
    static EFI_STATUS LoadAndBoot(EFI_HANDLE ImageHandle,
                                  EFI_SYSTEM_TABLE* SystemTable);

    KernelLoader()                               = delete;
    KernelLoader(const KernelLoader&)            = delete;
    KernelLoader& operator=(const KernelLoader&) = delete;

private:
    static EFI_STATUS OpenBootFile(EFI_HANDLE ImageHandle,
                                     EFI_SYSTEM_TABLE* st,
                                     const wchar_t* path,
                                     EFI_FILE_PROTOCOL** outFile,
                                     UINT64* outSize);

    static EFI_STATUS ReadFile(EFI_SYSTEM_TABLE* st,
                                EFI_FILE_PROTOCOL* file,
                                UINT64 size,
                                void** outBuffer);

    static void Print(EFI_SYSTEM_TABLE* st, const wchar_t* msg);
};