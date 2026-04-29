#include "ProcessorBind.h"
#include <UEFI/Uefi.h>
#include <Library/UefiLib.h>
#include <iboot/boot.hpp>

extern "C" EFI_STATUS EFIAPI iboot(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    SystemTable->BootServices->SetWatchdogTimer(0, 0, 0, nullptr);
    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);

    EFI_STATUS status = MemoryManager::Initialize(SystemTable);
    if (EFI_ERROR(status)) {
        const wchar_t* err = L"[iBoot] MemoryManager::Initialize failed!\r\n";
        SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16*)err);
        return status;
    }

    SystemTable->ConOut->OutputString(SystemTable->ConOut, (CHAR16*)L"[iBoot] iBoot starting...\r\n");

    status = KernelLoader::LoadAndBoot(ImageHandle, SystemTable);

    SystemTable->ConOut->OutputString(SystemTable->ConOut,
        (CHAR16*)L"[iBoot] KernelLoader::LoadAndBoot failed!\r\n");
    
    CHAR16 errMsg[64];
    const wchar_t* hex = L"0123456789ABCDEF";
    errMsg[0] = L'['; errMsg[1] = L'i'; errMsg[2] = L'B'; errMsg[3] = L'o'; 
    errMsg[4] = L'o'; errMsg[5] = L't'; errMsg[6] = L']'; errMsg[7] = L' ';
    errMsg[8] = L'E'; errMsg[9] = L'r'; errMsg[10] = L'r'; errMsg[11] = L'o';
    errMsg[12] = L'r'; errMsg[13] = L' '; errMsg[14] = L'c'; errMsg[15] = L'o';
    errMsg[16] = L'd'; errMsg[17] = L'e'; errMsg[18] = L':'; errMsg[19] = L' ';
    errMsg[20] = L'0'; errMsg[21] = L'x';
    for (int i = 0; i < 16; i++) {
        errMsg[22 + i] = hex[(status >> (60 - i * 4)) & 0xF];
    }
    errMsg[38] = L'\r'; errMsg[39] = L'\n'; errMsg[40] = 0;
    SystemTable->ConOut->OutputString(SystemTable->ConOut, errMsg);

    while (true)
        SystemTable->BootServices->Stall(1000000);

    return status;
}

extern "C" void* memset(void* dest, int val, size_t count) {
    void* ret = dest;
    asm volatile(
        "cld\n"
        "rep stosb"
        : "+D"(dest), "+c"(count)
        : "a"((unsigned char)val)
        : "memory"
    );
    return ret;
}

extern "C" void* memcpy(void* dest, const void* src, size_t count) {
    void* ret = dest;
    asm volatile(
        "cld\n"
        "rep movsb"
        : "+D"(dest), "+S"(src), "+c"(count)
        : : "memory"
    );
    return ret;
}
