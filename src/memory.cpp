#include <iboot/memory.hpp>

static void u64_to_dec(unsigned long long v, char* buf, int size) {
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[24]; int i = 0;
    while (v && i < 23) { tmp[i++] = '0' + (v % 10); v /= 10; }
    int j = 0;
    while (i-- > 0 && j < size - 1) buf[j++] = tmp[i];
    buf[j] = '\0';
}

static void u64_to_hex(unsigned long long v, char* buf) {
    const char* h = "0123456789ABCDEF";
    buf[0]='0'; buf[1]='x';
    for (int i = 0; i < 16; i++)
        buf[2 + i] = h[(v >> (60 - i * 4)) & 0xF];
    buf[18] = '\0';
}

static int str_len(const char* s) {
    int n = 0; while (s[n]) n++; return n;
}

static void efi_print(EFI_SYSTEM_TABLE* st, const char* s) {
    CHAR16 buf[2] = {0, 0};
    for (int i = 0; s[i]; i++) {
        buf[0] = (CHAR16)(unsigned char)s[i];
        st->ConOut->OutputString(st->ConOut, buf);
    }
}

MemoryMap MemoryManager::s_map        = {};
bool      MemoryManager::s_initialized = false;

EFI_STATUS MemoryManager::Initialize(EFI_SYSTEM_TABLE* st) {
    EFI_BOOT_SERVICES* bs = st->BootServices;

    UINTN mapSize     = 0;
    UINTN mapKey      = 0;
    UINTN descSize    = 0;
    UINT32 descVer    = 0;

    EFI_STATUS status = bs->GetMemoryMap(&mapSize, nullptr, &mapKey,
                                         &descSize, &descVer);
    if (status != EFI_BUFFER_TOO_SMALL) return status;

    mapSize += 4 * descSize;

    EFI_MEMORY_DESCRIPTOR* efiMap = nullptr;
    status = bs->AllocatePool(EfiLoaderData, mapSize, (void**)&efiMap);
    if (EFI_ERROR(status)) return status;

    status = bs->GetMemoryMap(&mapSize, efiMap, &mapKey,
                              &descSize, &descVer);
    if (EFI_ERROR(status)) { bs->FreePool(efiMap); return status; }

    unsigned long long count = mapSize / descSize;
    MemoryRegion* regions = nullptr;
    status = bs->AllocatePool(EfiLoaderData,
                              count * sizeof(MemoryRegion),
                              (void**)&regions);
    if (EFI_ERROR(status)) { bs->FreePool(efiMap); return status; }

    s_map.totalBytes    = 0;
    s_map.freeBytes     = 0;
    s_map.kernelBytes   = 0;
    s_map.reservedBytes = 0;
    s_map.count         = 0;

    for (unsigned long long i = 0; i < count; i++) {
        auto* d = (EFI_MEMORY_DESCRIPTOR*)((unsigned char*)efiMap + i * descSize);
        MemoryRegion& r = regions[s_map.count++];
        r.base  = d->PhysicalStart;
        r.pages = d->NumberOfPages;
        r.type  = TranslateEfiType((EFI_MEMORY_TYPE)d->Type);

        unsigned long long bytes = r.bytes();
        s_map.totalBytes += bytes;
        switch (r.type) {
        case MemoryType::Free:            s_map.freeBytes     += bytes; break;
        case MemoryType::KernelCode:
        case MemoryType::KernelData:      s_map.kernelBytes   += bytes; break;
        case MemoryType::Reserved:
        case MemoryType::MmioRegion:      s_map.reservedBytes += bytes;break;
        default: break;
        }
    }

    s_map.regions  = regions;
    s_map.capacity = count;
    bs->FreePool(efiMap);
    s_initialized = true;
    return EFI_SUCCESS;
}

EFI_STATUS MemoryManager::PrepareForKernel(EFI_SYSTEM_TABLE* st) {
    s_initialized = false;
    return Initialize(st);
}

const MemoryMap& MemoryManager::GetMap()    { return s_map; }
unsigned long long         MemoryManager::TotalBytes(){ return s_map.totalBytes; }
unsigned long long         MemoryManager::FreeBytes() { return s_map.freeBytes;  }

MemoryType MemoryManager::TranslateEfiType(EFI_MEMORY_TYPE t) {
    switch (t) {
    case EfiConventionalMemory:
    case EfiBootServicesCode:
    case EfiBootServicesData:    return MemoryType::Free;
    case EfiLoaderCode:
    case EfiLoaderData:
    case EfiRuntimeServicesCode:
    case EfiRuntimeServicesData:
    case EfiACPIReclaimMemory:
    case EfiACPIMemoryNVS:
    case EfiPalCode:             return MemoryType::Reserved;
    case EfiMemoryMappedIO:
    case EfiMemoryMappedIOPortSpace: return MemoryType::MmioRegion;
    case EfiUnusableMemory:      return MemoryType::Reserved;
    default:                     return MemoryType::Reserved;
    }
}

const char* MemoryManager::TypeName(MemoryType t) {
    switch (t) {
    case MemoryType::Free:            return "Free";
    case MemoryType::KernelCode:      return "KernelCode";
    case MemoryType::KernelData:      return "KernelData (iBoot)";
    case MemoryType::BootloaderData:  return "BootloaderData";
    case MemoryType::AcpiReclaimable: return "ACPI Reclaimable";
    case MemoryType::AcpiNvs:         return "ACPI NVS";
    case MemoryType::MmioRegion:      return "MMIO";
    case MemoryType::Reserved:        return "Reserved";
    case MemoryType::Unusable:        return "Unusable";
    default:                          return "Unknown";
    }
}

void MemoryManager::FormatSize(unsigned long long bytes, char* buf, unsigned int len) {
    const char* units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    int u = 0;
    unsigned long long val = bytes;
    while (val >= 1024 && u < 4) { val /= 1024; u++; }
    u64_to_dec(val, buf, len - 4);
    int n = str_len(buf);
    buf[n++] = ' ';
    for (int i = 0; units[u][i]; i++) buf[n++] = units[u][i];
    buf[n] = '\0';
}
