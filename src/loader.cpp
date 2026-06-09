#include "ProcessorBind.h"
#include <iboot/loader.hpp>
#include <iboot/COFF.hpp>
#include <iboot/memory.hpp>

#include <UEFI/Uefi.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/GraphicsOutput.h>
#include <Guid/FileInfo.h>

static EFI_GUID gEfiLoadedImageProtocolGuid      = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID gEfiSimpleFileSystemProtocolGuid  = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID gEfiFileInfoGuid                  = EFI_FILE_INFO_ID;
static EFI_GUID gEfiGraphicsOutputProtocolGuid    = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

namespace {
constexpr UINT32 kPreferredFramebufferWidth = 1280;
constexpr UINT32 kPreferredFramebufferHeight = 720;

bool Is16By9(UINT32 width, UINT32 height) {
    return height != 0 && width * 9 == height * 16;
}

void Prefer16By9Framebuffer(EFI_SYSTEM_TABLE* st, EFI_GRAPHICS_OUTPUT_PROTOCOL* gop) {
    if (!gop || !gop->Mode) {
        return;
    }

    UINT32 bestMode = gop->Mode->Mode;
    UINT32 bestWidth = gop->Mode->Info ? gop->Mode->Info->HorizontalResolution : 0;
    UINT32 bestHeight = gop->Mode->Info ? gop->Mode->Info->VerticalResolution : 0;
    bool foundPreferred = false;
    bool found16By9 = Is16By9(bestWidth, bestHeight);

    for (UINT32 modeIndex = 0; modeIndex < gop->Mode->MaxMode; ++modeIndex) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info = nullptr;
        UINTN infoSize = 0;
        EFI_STATUS status = gop->QueryMode(gop, modeIndex, &infoSize, &info);
        if (EFI_ERROR(status) || !info) {
            continue;
        }

        const UINT32 width = info->HorizontalResolution;
        const UINT32 height = info->VerticalResolution;
        if (width == kPreferredFramebufferWidth && height == kPreferredFramebufferHeight) {
            bestMode = modeIndex;
            bestWidth = width;
            bestHeight = height;
            foundPreferred = true;
        } else if (!foundPreferred && Is16By9(width, height)) {
            const UINT64 pixels = static_cast<UINT64>(width) * height;
            const UINT64 bestPixels = static_cast<UINT64>(bestWidth) * bestHeight;
            if (!found16By9 || pixels > bestPixels) {
                bestMode = modeIndex;
                bestWidth = width;
                bestHeight = height;
                found16By9 = true;
            }
        }

        if (st && st->BootServices) {
            st->BootServices->FreePool(info);
        }
    }

    if ((foundPreferred || found16By9) && bestMode != gop->Mode->Mode) {
        if (EFI_ERROR(gop->SetMode(gop, bestMode))) {
            if (st && st->ConOut) {
                st->ConOut->OutputString(st->ConOut, (CHAR16*)L"[iBoot] Warning: failed to switch GOP framebuffer mode\r\n");
            }
        }
    }
}
}

void KernelLoader::Print(EFI_SYSTEM_TABLE* st, const wchar_t* msg) {
    if (st && st->ConOut)
        st->ConOut->OutputString(st->ConOut, (CHAR16*)msg);
}

EFI_STATUS KernelLoader::OpenBootFile(EFI_HANDLE ImageHandle,
                                         EFI_SYSTEM_TABLE* st,
                                         const wchar_t* path,
                                         EFI_FILE_PROTOCOL** outFile,
                                         UINT64* outSize) {
    EFI_BOOT_SERVICES* bs = st->BootServices;

    EFI_LOADED_IMAGE_PROTOCOL* loadedImage = nullptr;
    EFI_STATUS status = bs->HandleProtocol(ImageHandle,
                                           &gEfiLoadedImageProtocolGuid,
                                           (void**)&loadedImage);
    if (EFI_ERROR(status)) {
        Print(st, L"[iBoot] HandleProtocol(LoadedImage) failed\r\n");
        return status;
    }

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs = nullptr;
    status = bs->HandleProtocol(loadedImage->DeviceHandle,
                                &gEfiSimpleFileSystemProtocolGuid,
                                (void**)&fs);
    if (EFI_ERROR(status)) {
        Print(st, L"[iBoot] HandleProtocol(SimpleFileSystem) failed\r\n");
        return status;
    }

    EFI_FILE_PROTOCOL* root = nullptr;
    status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(status)) {
        Print(st, L"[iBoot] OpenVolume failed\r\n");
        return status;
    }

    EFI_FILE_PROTOCOL* kernelFile = nullptr;
    status = root->Open(root, &kernelFile,
                        (CHAR16*)path,
                        EFI_FILE_MODE_READ,
                        0);
    if (EFI_ERROR(status)) {
        root->Close(root);
        return status;
    }

    UINTN infoSize = sizeof(EFI_FILE_INFO) + 256;
    EFI_FILE_INFO* info = nullptr;
    status = bs->AllocatePool(EfiLoaderData, infoSize, (void**)&info);
    if (EFI_ERROR(status)) {
        kernelFile->Close(kernelFile);
        root->Close(root);
        return status;
    }

    status = kernelFile->GetInfo(kernelFile, &gEfiFileInfoGuid, &infoSize, info);
    if (EFI_ERROR(status)) {
        Print(st, L"[iBoot] GetInfo on kernel file failed\r\n");
        bs->FreePool(info);
        kernelFile->Close(kernelFile);
        root->Close(root);
        return status;
    }

    *outSize = info->FileSize;
    bs->FreePool(info);
    root->Close(root);

    *outFile = kernelFile;
    return EFI_SUCCESS;
}

EFI_STATUS KernelLoader::ReadFile(EFI_SYSTEM_TABLE* st,
                                   EFI_FILE_PROTOCOL* file,
                                   UINT64 size,
                                   void** outBuffer) {
    EFI_BOOT_SERVICES* bs = st->BootServices;

    void* buf = nullptr;
    EFI_STATUS status = bs->AllocatePool(EfiLoaderData, (UINTN)size, &buf);
    if (EFI_ERROR(status)) return status;

    UINTN readSize = (UINTN)size;
    status = file->Read(file, &readSize, buf);
    if (EFI_ERROR(status)) {
        bs->FreePool(buf);
        return status;
    }

    *outBuffer = buf;
    return EFI_SUCCESS;
}

EFI_STATUS KernelLoader::LoadAndBoot(EFI_HANDLE ImageHandle,
                                      EFI_SYSTEM_TABLE* st) {

    EFI_LOADED_IMAGE_PROTOCOL* loadedImage;

    EFI_GUID loadedImageProtocol = EFI_LOADED_IMAGE_PROTOCOL_GUID;

    st->BootServices->HandleProtocol(
        ImageHandle,
        &loadedImageProtocol,
        (void**)&loadedImage
    );
    UINT64 runtime_base = (UINT64)loadedImage->ImageBase;

    EFI_BOOT_SERVICES* bs = st->BootServices;

    Print(st, L"[iBoot] Loading INSTANTOS.EFI...\r\n");

    EFI_FILE_PROTOCOL* kernelFile = nullptr;
    UINT64 fileSize = 0;
    EFI_STATUS status = OpenBootFile(ImageHandle, st, L"EFI\\BOOT\\INSTANTOS.EFI", &kernelFile, &fileSize);
    if (EFI_ERROR(status)) {
        Print(st, L"[iBoot] Cannot open EFI\\BOOT\\INSTANTOS.EFI\r\n");
        return status;
    }

    void* fileBuffer = nullptr;
    status = ReadFile(st, kernelFile, fileSize, &fileBuffer);
    kernelFile->Close(kernelFile);
    if (EFI_ERROR(status)) {
        Print(st, L"[iBoot] Failed to read kernel file\r\n");
        return status;
    }

    auto imgResult = CoffImage::From(fileBuffer, (UINTN)fileSize);
    if (!imgResult) {
        Print(st, L"[iBoot] INSTANTOS.EFI is not a valid PE/COFF image\r\n");
        bs->FreePool(fileBuffer);
        return EFI_LOAD_ERROR;
    }
    const CoffImage& img = imgResult.value;

    UINT32 imageSize = img.SizeOfImage();
    UINTN pageCount  = (imageSize + 0xFFF) / 0x1000;

    UINT64 preferredBase = img.ImageBase();
    EFI_PHYSICAL_ADDRESS loadBase = (EFI_PHYSICAL_ADDRESS)preferredBase;
    status = bs->AllocatePages(AllocateAddress, EfiLoaderData,
                               pageCount, &loadBase);
    if (EFI_ERROR(status)) {
        loadBase = 0;
        status = bs->AllocatePages(AllocateAnyPages, EfiLoaderData,
                                   pageCount, &loadBase);
    }
    if (EFI_ERROR(status)) {
        Print(st, L"[iBoot] AllocatePages for kernel failed\r\n");
        bs->FreePool(fileBuffer);
        return status;
    }

    UINT8* dest = (UINT8*)(UINTN)loadBase;
    for (UINTN i = 0; i < (UINTN)imageSize; i++) dest[i] = 0;

    UINT32 hdrSize = img.SizeOfHeaders();
    const UINT8* src = (const UINT8*)fileBuffer;
    for (UINT32 i = 0; i < hdrSize; i++) dest[i] = src[i];

    for (UINT16 s = 0; s < img.SectionCount(); s++) {
        CoffSection sec = img.SectionAt(s);
        UINT32 rawOff  = sec.RawOffset();
        UINT32 rawSize = sec.RawSize();
        UINT32 va      = sec.VirtualAddress();
        if (rawSize == 0) continue;
        const UINT8* secSrc = src + rawOff;
        UINT8*       secDst = dest + va;
        for (UINT32 i = 0; i < rawSize; i++) secDst[i] = secSrc[i];
    }

    bs->FreePool(fileBuffer);

    auto mappedResult = CoffImage::From((void*)(UINTN)loadBase, imageSize);
    if (!mappedResult) {
        Print(st, L"[iBoot] Failed to re-parse mapped kernel image\r\n");
        bs->FreePages(loadBase, pageCount);
        return EFI_LOAD_ERROR;
    }
    const CoffImage& mapped = mappedResult.value;

    CoffError relocErr = mapped.ApplyRelocations(loadBase);
    if (relocErr != CoffError::None && relocErr != CoffError::NoRelocations) {
        Print(st, L"[iBoot] Relocation failed\r\n");
        bs->FreePages(loadBase, pageCount);
        return EFI_LOAD_ERROR;
    }

    KernelEntry kernelEntry = (KernelEntry)mapped.EntryPoint();
    if (!kernelEntry) {
        Print(st, L"[iBoot] Kernel entry point is null\r\n");
        bs->FreePages(loadBase, pageCount);
        return EFI_LOAD_ERROR;
    }

    Print(st, L"[iBoot] Kernel loaded. Preparing initrd and memory map...\r\n");

    UINT64 initrdBase = 0;
    UINT64 initrdSize = 0;
    EFI_FILE_PROTOCOL* initrdFile = nullptr;
    
    if (!EFI_ERROR(OpenBootFile(ImageHandle, st, L"EFI\\BOOT\\INITRD", &initrdFile, &initrdSize))) {
        void* initrdBuf = nullptr;
        if (!EFI_ERROR(ReadFile(st, initrdFile, initrdSize, &initrdBuf))) {
            initrdBase = (UINT64)initrdBuf;
            Print(st, L"[iBoot] INITRD loaded.\r\n");
        } else {
            Print(st, L"[iBoot] Failed to read INITRD.\r\n");
        }
        initrdFile->Close(initrdFile);
    }

    Framebuffer fb = {};
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = nullptr;
    if (!EFI_ERROR(bs->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, nullptr, (void**)&gop))) {
        Prefer16By9Framebuffer(st, gop);
        EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* mode = gop->Mode;
        fb.base              = (UINT64)mode->FrameBufferBase;
        fb.size              = (UINT64)mode->FrameBufferSize;
        fb.width             = mode->Info->HorizontalResolution;
        fb.height            = mode->Info->VerticalResolution;
        fb.pixelsPerScanLine = mode->Info->PixelsPerScanLine;
        switch (mode->Info->PixelFormat) {
        case PixelRedGreenBlueReserved8BitPerColor: fb.format = PixelFormat::RGBReserved; break;
        case PixelBlueGreenRedReserved8BitPerColor: fb.format = PixelFormat::BGRReserved; break;
        case PixelBitMask:
            fb.format    = PixelFormat::BitMask;
            fb.redMask   = mode->Info->PixelInformation.RedMask;
            fb.greenMask = mode->Info->PixelInformation.GreenMask;
            fb.blueMask  = mode->Info->PixelInformation.BlueMask;
            break;
        default: fb.format = PixelFormat::BltOnly; break;
        }
        Print(st, L"[iBoot] Framebuffer acquired.\r\n");
    } else {
        Print(st, L"[iBoot] Warning: GOP not found, no framebuffer.\r\n");
    }

    UINTN mapSize  = 0;
    UINTN mapKey   = 0;
    UINTN descSize = 0;
    UINT32 descVer = 0;

    bs->GetMemoryMap(&mapSize, nullptr, &mapKey, &descSize, &descVer);
    UINT64 estimatedRegionCount = (mapSize / descSize) + 16;
    mapSize += 16 * descSize;

    EFI_MEMORY_DESCRIPTOR* efiMap = nullptr;
    status = bs->AllocatePool(EfiLoaderData, mapSize, (void**)&efiMap);
    if (EFI_ERROR(status)) return status;

    BootInfo* bootInfo = nullptr;
    status = bs->AllocatePool(EfiLoaderData, sizeof(BootInfo), (void**)&bootInfo);
    if (EFI_ERROR(status)) {
        bs->FreePool(efiMap);
        return status;
    }

    bootInfo->imageBase = runtime_base;

    MemoryRegion* regions = nullptr;
    status = bs->AllocatePool(EfiLoaderData,
                              estimatedRegionCount * sizeof(MemoryRegion),
                              (void**)&regions);
    if (EFI_ERROR(status)) {
        bs->FreePool(bootInfo);
        bs->FreePool(efiMap);
        return status;
    }

    UINTN finalMapSize = mapSize;
    status = bs->GetMemoryMap(&finalMapSize, efiMap, &mapKey, &descSize, &descVer);
    if (EFI_ERROR(status)) {
        bs->FreePool(regions);
        bs->FreePool(bootInfo);
        bs->FreePool(efiMap);
        return status;
    }

    UINT64 count = finalMapSize / descSize;
    UINT64 totalBytes = 0, freeBytes = 0, reservedBytes = 0;
    UINT64 n = 0;

    for (UINT64 i = 0; i < count; i++) {
        auto* d = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)efiMap + i * descSize);
        MemoryRegion& r = regions[n++];
        r.base  = d->PhysicalStart;
        r.pages = d->NumberOfPages;

        switch ((EFI_MEMORY_TYPE)d->Type) {
        case EfiConventionalMemory:
        case EfiBootServicesCode:
        case EfiBootServicesData:   r.type = MemoryType::Free;            break;
        case EfiLoaderCode:
        case EfiLoaderData:
        case EfiRuntimeServicesCode:
        case EfiRuntimeServicesData:
        case EfiACPIReclaimMemory:
        case EfiACPIMemoryNVS:
        case EfiPalCode:            r.type = MemoryType::Reserved;        break;
        case EfiMemoryMappedIO:
        case EfiMemoryMappedIOPortSpace: r.type = MemoryType::MmioRegion; break;
        case EfiUnusableMemory:     r.type = MemoryType::Reserved;        break;
        default:                    r.type = MemoryType::Reserved;        break;
        }

        UINT64 bytes = r.bytes();
        totalBytes += bytes;
        if (r.type == MemoryType::Free)        freeBytes     += bytes;
        if (r.type == MemoryType::Reserved ||
            r.type == MemoryType::MmioRegion)  reservedBytes += bytes;
    }

    bootInfo->memoryMap.regions      = regions;
    bootInfo->memoryMap.count        = n;
    bootInfo->memoryMap.capacity     = count;
    bootInfo->memoryMap.totalBytes   = totalBytes;
    bootInfo->memoryMap.freeBytes    = freeBytes;
    bootInfo->memoryMap.kernelBytes  = 0;
    bootInfo->memoryMap.reservedBytes= reservedBytes;
    bootInfo->framebuffer            = fb;
    bootInfo->kernelBase             = (UINT64)loadBase;
    bootInfo->kernelSize             = (UINT64)imageSize;
    bootInfo->rsdp                   = 0;
    bootInfo->initrdBase             = initrdBase;
    bootInfo->initrdSize             = initrdSize;

    for (UINTN i = 0; i < st->NumberOfTableEntries; i++) {
        EFI_GUID* guid = &st->ConfigurationTable[i].VendorGuid;
        // ACPI 2.0 GUID
        if (guid->Data1 == 0x8868e871 && guid->Data2 == 0xe4f1 && guid->Data3 == 0x11d3 &&
            guid->Data4[0] == 0xbc && guid->Data4[1] == 0x22 && guid->Data4[2] == 0x00 && guid->Data4[3] == 0x80 &&
            guid->Data4[4] == 0xc7 && guid->Data4[5] == 0x3c && guid->Data4[6] == 0x88 && guid->Data4[7] == 0x81) {
            bootInfo->rsdp = (UINT64)st->ConfigurationTable[i].VendorTable;
            break;
        }
        // ACPI 1.0 GUID
        if (guid->Data1 == 0xeb9d2d30 && guid->Data2 == 0x2d88 && guid->Data3 == 0x11d3 &&
            guid->Data4[0] == 0x9a && guid->Data4[1] == 0x16 && guid->Data4[2] == 0x00 && guid->Data4[3] == 0x90 &&
            guid->Data4[4] == 0x27 && guid->Data4[5] == 0x3f && guid->Data4[6] == 0xc1 && guid->Data4[7] == 0x4d) {
            bootInfo->rsdp = (UINT64)st->ConfigurationTable[i].VendorTable;
        }
    }

    for (int retry = 0; retry < 3; retry++) {
        finalMapSize = mapSize;
        status = bs->GetMemoryMap(&finalMapSize, efiMap, &mapKey, &descSize, &descVer);
        if (EFI_ERROR(status)) break;
        status = bs->ExitBootServices(ImageHandle, mapKey);
        if (!EFI_ERROR(status)) break;
    }
    if (EFI_ERROR(status)) return status;

    kernelEntry(bootInfo);

    while (true) {}
}
