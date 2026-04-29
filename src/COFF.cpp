#include "ProcessorBind.h"
#include <iboot/COFF.hpp>

static unsigned coff_strlen(const char* s) {
    unsigned n = 0;
    while (s[n]) ++n;
    return n;
}

static void ConPrint(EFI_SYSTEM_TABLE* st, const wchar_t* s) {
    if (st && st->ConOut) st->ConOut->OutputString(st->ConOut, (CHAR16*)s);
}

static CHAR16* U64ToStr(UINT64 v, CHAR16* buf) {
    if (v == 0) { *buf++ = L'0'; *buf = L'\0'; return buf; }
    CHAR16 tmp[20]; int i = 0;
    while (v) { tmp[i++] = L'0' + (CHAR16)(v % 10); v /= 10; }
    for (int j = i - 1; j >= 0; --j) *buf++ = tmp[j];
    *buf = L'\0';
    return buf;
}

static CHAR16* U64ToHex(UINT64 v, CHAR16* buf) {
    const wchar_t hex[] = L"0123456789ABCDEF";
    *buf++ = L'0'; *buf++ = L'x';
    bool leading = true;
    for (int i = 60; i >= 0; i -= 4) {
        CHAR16 d = hex[(v >> i) & 0xF];
        if (d != L'0') leading = false;
        if (!leading) *buf++ = d;
    }
    if (leading) *buf++ = L'0';
    *buf = L'\0';
    return buf;
}

bool CoffSection::NameIs(const char* name) const {
    if (!hdr || !name) return false;
    unsigned nameLen = coff_strlen(name);
    if (nameLen > EFI_IMAGE_SIZEOF_SHORT_NAME) return false;
    for (unsigned i = 0; i < EFI_IMAGE_SIZEOF_SHORT_NAME; ++i) {
        char sc = (char)hdr->Name[i];
        char nc = (i < nameLen) ? name[i] : '\0';
        if (sc != nc) return false;
    }
    return true;
}

CoffResult<CoffImage> CoffImage::From(const void* base, UINTN size) {
    if (!base) return { {}, CoffError::NullImage };

    CoffImage img;
    img.m_base = static_cast<const UINT8*>(base);
    img.m_size = size;

    if (size < sizeof(EFI_IMAGE_DOS_HEADER))
        return { {}, CoffError::BadDosMagic };

    const auto* dos = reinterpret_cast<const EFI_IMAGE_DOS_HEADER*>(base);
    UINT32 ntOffset = 0;

    if (dos->e_magic == EFI_IMAGE_DOS_SIGNATURE) {
        img.m_hasDos = true;
        ntOffset = dos->e_lfanew;
    }
    if (ntOffset + 4 > size) return { {}, CoffError::BadNtSignature };
    const UINT32* sig = reinterpret_cast<const UINT32*>(img.m_base + ntOffset);
    if (*sig != EFI_IMAGE_NT_SIGNATURE) return { {}, CoffError::BadNtSignature };

    UINT32 fileHdrOffset = ntOffset + 4;
    if (fileHdrOffset + sizeof(EFI_IMAGE_FILE_HEADER) > size)
        return { {}, CoffError::BadNtSignature };

    img.m_fileHdr = reinterpret_cast<const EFI_IMAGE_FILE_HEADER*>(
        img.m_base + fileHdrOffset);

    UINT32 optOffset = fileHdrOffset + sizeof(EFI_IMAGE_FILE_HEADER);
    if (optOffset + 2 > size) return { {}, CoffError::BadOptionalHeader };

    const UINT16 magic = *reinterpret_cast<const UINT16*>(img.m_base + optOffset);
    if (magic == EFI_IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        img.m_pe32plus = true;
    } else if (magic == EFI_IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        img.m_pe32plus = false;
    } else {
        return { {}, CoffError::BadOptionalHeader };
    }
    img.m_optHdr = img.m_base + optOffset;

    UINT32 secOffset = optOffset + img.m_fileHdr->SizeOfOptionalHeader;
    UINT32 secEnd    = secOffset +
        (UINT32)img.m_fileHdr->NumberOfSections * sizeof(EFI_IMAGE_SECTION_HEADER);
    if (secEnd > size) return { {}, CoffError::OutOfBounds };

    img.m_sections = reinterpret_cast<const EFI_IMAGE_SECTION_HEADER*>(
        img.m_base + secOffset);

    return { img, CoffError::None };
}

CoffResult<CoffImage> CoffImage::FromUnchecked(const void* base) {
    return From(base, ~(UINTN)0);
}

CoffMachine CoffImage::Machine() const {
    return static_cast<CoffMachine>(m_fileHdr->Machine);
}

CoffSubsystem CoffImage::Subsystem() const {
    if (m_pe32plus)
        return static_cast<CoffSubsystem>(OptHdr64()->Subsystem);
    return static_cast<CoffSubsystem>(OptHdr32()->Subsystem);
}

UINT64 CoffImage::ImageBase() const {
    return m_pe32plus ? OptHdr64()->ImageBase : (UINT64)OptHdr32()->ImageBase;
}

UINT32 CoffImage::SizeOfImage() const {
    return m_pe32plus ? OptHdr64()->SizeOfImage : OptHdr32()->SizeOfImage;
}

UINT32 CoffImage::SizeOfHeaders() const {
    return m_pe32plus ? OptHdr64()->SizeOfHeaders : OptHdr32()->SizeOfHeaders;
}

UINT32 CoffImage::EntryPointRva() const {
    return m_pe32plus
        ? OptHdr64()->AddressOfEntryPoint
        : OptHdr32()->AddressOfEntryPoint;
}

void* CoffImage::EntryPoint() const {
    auto r = RvaToPtr(EntryPointRva());
    return r.ok() ? r.value : nullptr;
}

const EFI_IMAGE_OPTIONAL_HEADER32* CoffImage::OptHdr32() const {
    return static_cast<const EFI_IMAGE_OPTIONAL_HEADER32*>(m_optHdr);
}

const EFI_IMAGE_OPTIONAL_HEADER64* CoffImage::OptHdr64() const {
    return static_cast<const EFI_IMAGE_OPTIONAL_HEADER64*>(m_optHdr);
}

UINT16 CoffImage::SectionCount() const {
    return m_fileHdr->NumberOfSections;
}

CoffSection CoffImage::SectionAt(UINT16 index) const {
    if (index >= m_fileHdr->NumberOfSections) return {};
    return { &m_sections[index] };
}

CoffSection CoffImage::FindSection(const char* name) const {
    for (UINT16 i = 0; i < m_fileHdr->NumberOfSections; ++i) {
        CoffSection s{ &m_sections[i] };
        if (s.NameIs(name)) return s;
    }
    return {};
}

CoffSection CoffImage::SectionForRva(UINT32 rva) const {
    for (UINT16 i = 0; i < m_fileHdr->NumberOfSections; ++i) {
        const auto& s = m_sections[i];
        if (rva >= s.VirtualAddress && rva < s.VirtualAddress + s.Misc.VirtualSize)
            return { &s };
    }
    return {};
}

bool CoffImage::InBounds(const void* ptr, UINTN len) const {
    const UINT8* p = static_cast<const UINT8*>(ptr);
    return p >= m_base && (p + len) <= (m_base + m_size);
}

CoffResult<void*> CoffImage::RvaToPtr(UINT32 rva) const {
    const UINT8* p = m_base + rva;
    if (!InBounds(p)) return { nullptr, CoffError::OutOfBounds };
    return { const_cast<UINT8*>(p), CoffError::None };
}

CoffResult<void*> CoffImage::OffsetToPtr(UINT32 off) const {
    const UINT8* p = m_base + off;
    if (!InBounds(p)) return { nullptr, CoffError::OutOfBounds };
    return { const_cast<UINT8*>(p), CoffError::None };
}

CoffResult<UINT32> CoffImage::PtrToRva(const void* ptr) const {
    const UINT8* p = static_cast<const UINT8*>(ptr);
    if (p < m_base || p >= m_base + m_size)
        return { 0, CoffError::OutOfBounds };
    return { (UINT32)(p - m_base), CoffError::None };
}

const EFI_IMAGE_DATA_DIRECTORY* CoffImage::DataDirectory(UINT32 index) const {
    if (index >= EFI_IMAGE_NUMBER_OF_DIRECTORY_ENTRIES) return nullptr;

    const EFI_IMAGE_DATA_DIRECTORY* dirs = m_pe32plus
        ? OptHdr64()->DataDirectory
        : OptHdr32()->DataDirectory;

    UINT32 numDirs = m_pe32plus
        ? OptHdr64()->NumberOfRvaAndSizes
        : OptHdr32()->NumberOfRvaAndSizes;

    if (index >= numDirs) return nullptr;
    if (dirs[index].VirtualAddress == 0 && dirs[index].Size == 0) return nullptr;
    return &dirs[index];
}

CoffError CoffImage::ApplyRelocations(UINT64 newBase) const {
    const EFI_IMAGE_DATA_DIRECTORY* dir =
        DataDirectory(EFI_IMAGE_DIRECTORY_ENTRY_BASERELOC);
    if (!dir) return CoffError::NoRelocations;

    auto ptrResult = RvaToPtr(dir->VirtualAddress);
    if (!ptrResult) return CoffError::OutOfBounds;

    UINT64 delta = newBase - ImageBase();
    if (delta == 0) return CoffError::None; 

    const UINT8* block    = static_cast<const UINT8*>(ptrResult.value);
    const UINT8* blockEnd = block + dir->Size;

    while (block < blockEnd) {
        const auto* reloc = reinterpret_cast<const EFI_IMAGE_BASE_RELOCATION*>(block);
        if (reloc->SizeOfBlock < sizeof(EFI_IMAGE_BASE_RELOCATION)) break;

        UINT32 count = (reloc->SizeOfBlock - sizeof(EFI_IMAGE_BASE_RELOCATION))
                       / sizeof(UINT16);
        const UINT16* entries = reinterpret_cast<const UINT16*>(reloc + 1);

        for (UINT32 i = 0; i < count; ++i) {
            UINT16 entry  = entries[i];
            UINT8  type   = (UINT8)(entry >> 12);
            UINT16 offset = entry & 0x0FFF;
            UINT32 rva    = reloc->VirtualAddress + offset;

            auto target = RvaToPtr(rva);
            if (!target) continue;

            switch (type) {
            case EFI_IMAGE_REL_BASED_ABSOLUTE:
                break; 
            case 0x1: 
                break; 
            case 0x2: 
                break; 
            case EFI_IMAGE_REL_BASED_HIGHLOW: {
                UINT32* p = static_cast<UINT32*>(target.value);
                *p = (UINT32)(*p + delta);
                break;
            }
            case 0x4: 
                break; 
            case 0x5: 
                break; 
            case EFI_IMAGE_REL_BASED_DIR64: {
                UINT64* p = static_cast<UINT64*>(target.value);
                *p += delta;
                break;
            }
            default:
                break; 
            }
        }

        block += reloc->SizeOfBlock;
    }

    return CoffError::None;
}

const EFI_IMAGE_DEBUG_DIRECTORY_ENTRY* CoffImage::DebugDirectory(UINT32* outCount) const {
    const EFI_IMAGE_DATA_DIRECTORY* dir =
        DataDirectory(EFI_IMAGE_DIRECTORY_ENTRY_DEBUG);
    if (!dir) return nullptr;

    auto ptr = RvaToPtr(dir->VirtualAddress);
    if (!ptr) return nullptr;

    if (outCount)
        *outCount = dir->Size / sizeof(EFI_IMAGE_DEBUG_DIRECTORY_ENTRY);

    return static_cast<const EFI_IMAGE_DEBUG_DIRECTORY_ENTRY*>(ptr.value);
}

const char* CoffImage::PdbPath() const {
    UINT32 count = 0;
    const EFI_IMAGE_DEBUG_DIRECTORY_ENTRY* dbg = DebugDirectory(&count);
    if (!dbg) return nullptr;

    for (UINT32 i = 0; i < count; ++i) {
        if (dbg[i].Type != EFI_IMAGE_DEBUG_TYPE_CODEVIEW) continue;
        if (dbg[i].FileOffset == 0) continue;

        auto ptr = OffsetToPtr(dbg[i].FileOffset);
        if (!ptr) continue;

        const UINT32* sig = static_cast<const UINT32*>(ptr.value);

        if (*sig == CODEVIEW_SIGNATURE_RSDS) {
            const auto* rsds =
                static_cast<const EFI_IMAGE_DEBUG_CODEVIEW_RSDS_ENTRY*>(ptr.value);
            return reinterpret_cast<const char*>(rsds + 1);
        }
        if (*sig == CODEVIEW_SIGNATURE_NB10) {
            const auto* nb10 =
                static_cast<const EFI_IMAGE_DEBUG_CODEVIEW_NB10_ENTRY*>(ptr.value);
            return reinterpret_cast<const char*>(nb10 + 1);
        }
    }
    return nullptr;
}

void CoffPrintSections(EFI_SYSTEM_TABLE* st, const CoffImage& img) {
    if (!st || !st->ConOut) return;

    wchar_t buf[128];

    ConPrint(st, L"PE/COFF sections:\r\n");

    for (UINT16 i = 0; i < img.SectionCount(); ++i) {
        CoffSection sec = img.SectionAt(i);

        
        wchar_t* p = buf;
        *p++ = L' '; *p++ = L' ';
        for (int c = 0; c < EFI_IMAGE_SIZEOF_SHORT_NAME; ++c) {
            UINT8 ch = sec.hdr->Name[c];
            *p++ = ch ? (wchar_t)ch : L' ';
        }
        *p++ = L' ';

        
        p = (wchar_t*)U64ToHex(sec.VirtualAddress(), reinterpret_cast<CHAR16*>(p));
        *p++ = L' ';

        
        p = (wchar_t*)U64ToHex(sec.VirtualSize(), (CHAR16*)p);
        *p++ = L' ';

        
        if (sec.IsCode())       { *p++ = L'X'; }
        if (sec.IsReadable())   { *p++ = L'R'; }
        if (sec.IsWritable())   { *p++ = L'W'; }
        if (sec.IsDiscardable()){ *p++ = L'D'; }

        *p++ = L'\r'; *p++ = L'\n'; *p = L'\0';
        ConPrint(st, buf);
    }

    
    wchar_t* p = buf;
    p = (wchar_t*)U64ToStr(img.SectionCount(), (CHAR16*)p);
    
    const wchar_t suffix[] = L" section(s), entry=";
    for (int i = 0; suffix[i]; ++i) *p++ = suffix[i];
    p = (wchar_t*)U64ToHex(img.EntryPointRva(), (CHAR16*)p);
    *p++ = L'\r'; *p++ = L'\n'; *p = L'\0';
    ConPrint(st, buf);
}

const char* CoffMachineName(CoffMachine m) {
    switch (m) {
    case CoffMachine::I386:    return "i386";
    case CoffMachine::X64:     return "x86-64";
    case CoffMachine::Arm64:   return "AArch64";
    case CoffMachine::RiscV64: return "RISC-V 64";
    case CoffMachine::Ebc:     return "EBC";
    default:                   return "unknown";
    }
}

const char* CoffSubsystemName(CoffSubsystem s) {
    switch (s) {
    case CoffSubsystem::EfiApplication:   return "EFI Application";
    case CoffSubsystem::EfiBootDriver:    return "EFI Boot Service Driver";
    case CoffSubsystem::EfiRuntimeDriver: return "EFI Runtime Driver";
    case CoffSubsystem::Native:           return "Native";
    case CoffSubsystem::WindowsGui:       return "Windows GUI";
    case CoffSubsystem::WindowsCui:       return "Windows CUI";
    default:                              return "Unknown";
    }
}
