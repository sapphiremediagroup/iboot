#pragma once

#include <UEFI/Uefi.h>
#include <IndustryStandard/PeImage.h>

class CoffImage;

enum class CoffError : unsigned int {
    None            = 0,
    BadDosMagic,
    BadNtSignature,
    BadMachine,
    BadOptionalHeader,
    OutOfBounds,
    NoRelocations,
    NullImage,
};

template<typename T>
struct CoffResult {
    T          value{};
    CoffError  error{ CoffError::None };

    constexpr bool ok()  const { return error == CoffError::None; }
    constexpr explicit operator bool() const { return ok(); }
};

enum class CoffMachine : UINT16 {
    Unknown   = 0,
    I386      = IMAGE_FILE_MACHINE_I386,
    X64       = IMAGE_FILE_MACHINE_X64,
    Arm64     = IMAGE_FILE_MACHINE_ARM64,
    RiscV64   = IMAGE_FILE_MACHINE_RISCV64,
    Ebc       = IMAGE_FILE_MACHINE_EBC,
};

enum class CoffSubsystem : UINT16 {
    Unknown          = EFI_IMAGE_SUBSYSTEM_UNKNOWN,
    Native           = EFI_IMAGE_SUBSYSTEM_NATIVE,
    EfiApplication   = EFI_IMAGE_SUBSYSTEM_EFI_APPLICATION,
    EfiBootDriver    = EFI_IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER,
    EfiRuntimeDriver = EFI_IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER,
    WindowsGui       = EFI_IMAGE_SUBSYSTEM_WINDOWS_GUI,
    WindowsCui       = EFI_IMAGE_SUBSYSTEM_WINDOWS_CUI,
};

struct CoffSection {
    const EFI_IMAGE_SECTION_HEADER* hdr{ nullptr };

    constexpr bool valid() const { return hdr != nullptr; }

    bool NameIs(const char* name) const;

    constexpr UINT32 VirtualAddress()  const { return hdr->VirtualAddress; }
    constexpr UINT32 VirtualSize()     const { return hdr->Misc.VirtualSize; }
    constexpr UINT32 RawOffset()       const { return hdr->PointerToRawData; }
    constexpr UINT32 RawSize()         const { return hdr->SizeOfRawData; }
    constexpr UINT32 Characteristics() const { return hdr->Characteristics; }

    constexpr bool IsCode()       const { return (hdr->Characteristics & EFI_IMAGE_SCN_CNT_CODE) != 0; }
    constexpr bool IsReadable()   const { return (hdr->Characteristics & EFI_IMAGE_SCN_MEM_READ) != 0; }
    constexpr bool IsWritable()   const { return (hdr->Characteristics & EFI_IMAGE_SCN_MEM_WRITE) != 0; }
    constexpr bool IsExecutable() const { return (hdr->Characteristics & EFI_IMAGE_SCN_MEM_EXECUTE) != 0; }
    constexpr bool IsDiscardable()const { return (hdr->Characteristics & EFI_IMAGE_SCN_MEM_DISCARDABLE) != 0; }
};

class CoffImage {
public:
    static CoffResult<CoffImage> From(const void* base, UINTN size);

    static CoffResult<CoffImage> FromUnchecked(const void* base);

    bool     IsPe32Plus()  const { return m_pe32plus; }
    bool     HasDosStub()  const { return m_hasDos; }

    CoffMachine    Machine()    const;
    CoffSubsystem  Subsystem()  const;

    UINT64   ImageBase()        const;
    UINT32   SizeOfImage()      const;
    UINT32   SizeOfHeaders()    const;
    UINT32   EntryPointRva()    const;

    void*    EntryPoint()       const;

    UINT16       SectionCount()                    const;
    CoffSection  SectionAt(UINT16 index)           const;
    CoffSection  FindSection(const char* name)     const;

    CoffSection  SectionForRva(UINT32 rva)         const;

    CoffResult<void*>        RvaToPtr(UINT32 rva)  const;
    CoffResult<void*>        OffsetToPtr(UINT32 off) const;
    CoffResult<UINT32>       PtrToRva(const void* ptr) const;

    const EFI_IMAGE_DATA_DIRECTORY* DataDirectory(UINT32 index) const;

    CoffError ApplyRelocations(UINT64 newBase) const;

    const EFI_IMAGE_DEBUG_DIRECTORY_ENTRY* DebugDirectory(UINT32* outCount = nullptr) const;

    const char* PdbPath() const;

    const void*                    Base()       const { return m_base; }
    UINTN                          Size()       const { return m_size; }
    const EFI_IMAGE_FILE_HEADER*   FileHeader() const { return m_fileHdr; }

    const EFI_IMAGE_OPTIONAL_HEADER32* OptHdr32() const;
    const EFI_IMAGE_OPTIONAL_HEADER64* OptHdr64() const;

private:
    CoffImage() = default;

    const UINT8*                   m_base    { nullptr };
    UINTN                          m_size    { 0 };
    const EFI_IMAGE_FILE_HEADER*   m_fileHdr { nullptr };
    const void*                    m_optHdr  { nullptr };
    const EFI_IMAGE_SECTION_HEADER* m_sections{ nullptr };
    bool                           m_pe32plus{ false };
    bool                           m_hasDos  { false };

    bool InBounds(const void* ptr, UINTN len = 1) const;
};

void CoffPrintSections(EFI_SYSTEM_TABLE* st, const CoffImage& img);

const char* CoffMachineName(CoffMachine m);
const char* CoffSubsystemName(CoffSubsystem s);
