#include <iostream>
#include <vector>
#include <thread>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include "ntdll.h"
#include "offsets.hpp"
#include "opus.h"
#include "minhook/MinHook.h"
#include "exceptions.hpp"

// set this to 0 if you are manual mapping
#define STANDARD_INJECTION 1

struct PREVIOUS_PROT
{
    void* BaseAddress;
    SIZE_T RegionSize;
    DWORD Protection;
};

int stdmemcmp(const void* Memory1, const void* Memory2, unsigned long long length);
char* stdstrstr(const void* Memory1, const void* Memory2, unsigned long long length, unsigned long long segment_length);
wchar_t* wstdstrstr(const void* Memory1, const void* Memory2, unsigned long long length, unsigned long long segment_length);
unsigned long wstrlen(const wchar_t* Memory1);
void RenderWindow();
bool InitializeNodeApiHooks(HMODULE Discord);

extern "C" __declspec(noinline) void __cdecl _alloc(unsigned long amount, void** dest)
{
    if (!amount) amount = 0x1000;
    PVOID Address = VirtualAlloc(nullptr, amount, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!Address)
    {
        printf("FATAL ERROR - ALLOCATION FAILED WITH CODE: %i\n", GetLastError());
        return;
    }
    *dest = Address;
}

extern "C" __declspec(noinline) int __cdecl _dealloc(void* address)
{
    if (!VirtualFree(address, 0, MEM_RELEASE))
    {
        printf("DEALLOCATION OF %p FAILE WITH CODE: %i\n", address, GetLastError());
        return false;
    }
    return true;
}

// keeping it simple for users
using LoadLibraryExW_t = decltype(LoadLibraryExW)*;

extern "C" void ReportError(LPCSTR Error)
{
    CloseHandle(CreateFileA(Error, GENERIC_READ | GENERIC_WRITE, NULL, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
}

bool EnableUI = false;
LoadLibraryExW_t oLoadLibraryExW;
HMODULE __stdcall LoadLibraryExWHook(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    if (!wstdstrstr(lpLibFileName, L"discord_voice", wstrlen(lpLibFileName), sizeof(L"discord_voice") - 2)) return oLoadLibraryExW(lpLibFileName, hFile, dwFlags);
    HMODULE VoiceModule = oLoadLibraryExW(lpLibFileName, hFile, dwFlags);
    if (!VoiceModule)
    {
        ReportError("discord voice node could not be loaded");
        return {};
    }

    std::vector<PREVIOUS_PROT> PreviousProtections = {};
    MEMORY_BASIC_INFORMATION MemoryInfo = {};
    if (!VirtualQuery((void*)((uintptr_t)VoiceModule + offsets::opus_encode()), &MemoryInfo, sizeof(MemoryInfo)))
    {
        ReportError("discord voice node is not mapped");
        return {};
    }

    PREVIOUS_PROT Entry = { MemoryInfo.BaseAddress, MemoryInfo.RegionSize, 0 };
    VirtualProtect(MemoryInfo.BaseAddress, MemoryInfo.RegionSize, PAGE_EXECUTE_READWRITE, &Entry.Protection);
    PreviousProtections.push_back(Entry);
    if (MH_CreateHook((void*)((uintptr_t)VoiceModule + offsets::opus_encode()), opus_encode, NULL) != MH_OK)
    {
        ReportError("opus_encode could not be detoured");
        return VoiceModule;
    }

    if (!VirtualQuery((void*)((uintptr_t)VoiceModule + offsets::opus_decode()), &MemoryInfo, sizeof(MemoryInfo)))
    {
        ReportError("discord voice node is not mapped");
        return {};
    }
    Entry = { MemoryInfo.BaseAddress, MemoryInfo.RegionSize, 0 };
    VirtualProtect(MemoryInfo.BaseAddress, MemoryInfo.RegionSize, PAGE_EXECUTE_READWRITE, &Entry.Protection);
    PreviousProtections.push_back(Entry);
    if (MH_CreateHook((void*)((uintptr_t)VoiceModule + offsets::opus_decode()), opus_decode, NULL) != MH_OK)
    {
        ReportError("opus_encode could not be detoured");
        return {};
    }

    if (!VirtualQuery((void*)((uintptr_t)VoiceModule + offsets::HighPassFilter()), &MemoryInfo, sizeof(MemoryInfo)))
    {
        ReportError("discord voice node is not mapped");
        return {};
    }
    Entry = { MemoryInfo.BaseAddress, MemoryInfo.RegionSize, 0 };
    VirtualProtect(MemoryInfo.BaseAddress, MemoryInfo.RegionSize, PAGE_EXECUTE_READWRITE, &Entry.Protection);
    PreviousProtections.push_back(Entry);
    memcpy((void*)((uintptr_t)VoiceModule + offsets::HighPassFilter()), "\xC3", 1);

    HMODULE Discord = GetModuleHandleA("Discord.exe");
    if (!Discord)
    {
        ReportError("this is not a discord process");
        return {};
    }

    if (!InitializeNodeApiHooks(Discord))
    {
        ReportError("failed to enable properties of voice node");
        return {};
    }

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
    {
        ReportError("opus_encode could not be written to");
        return VoiceModule;
    }
    EnableUI = true;

    for (uint16_t i = 0; i < PreviousProtections.size(); i++)
    {
        DWORD unused = 0;
        VirtualProtect(PreviousProtections[i].BaseAddress, PreviousProtections[i].RegionSize, PreviousProtections[i].Protection, &unused);
    }
    return VoiceModule;
}

// dll notifications will not be used for this example
void RenderWindow();
void MainFunction(HMODULE ExampleHook)
{
    MH_Initialize();

    HMODULE KernelBase = GetModuleHandleA("kernelbase.dll");
    if (!KernelBase)
    {
        ReportError("kernelbase.dll could not be found");
        return;
    }

    void* LoadLibraryExW = GetProcAddress(KernelBase, "LoadLibraryExW");
    if (!LoadLibraryExW)
    {
        ReportError("LoadLibraryExA could not be retrieved");
        return;
    }

    if (MH_CreateHook(LoadLibraryExW, LoadLibraryExWHook, (void**)&oLoadLibraryExW) != MH_OK)
    {
        ReportError("Failed to create LoadLibraryExW hook");
        return;
    }

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
    {
        ReportError("Failed to write LoadLibraryExW hook");
        return;
    }

    while (!EnableUI) Sleep(1000);
    DWORD ProcessId = GetCurrentProcessId();
    if (!AttachConsole(ProcessId) && GetLastError() != ERROR_ACCESS_DENIED)
    {
        if (!AllocConsole())
        {
            //MessageBoxA(NULL, xorstr("Failed to allocate console"), xorstr("Discord"), MB_ICONERROR);
            return;
        }
    }
    freopen(("conin$"), ("r"), stdin);
    freopen(("conout$"), ("w"), stdout);
    freopen(("conout$"), ("w"), stderr);

#ifdef STANDARD_INJECTION
    MODULEINFO ExampleHookInfo = {};
    if (!K32GetModuleInformation(NtCurrentProcess, ExampleHook, &ExampleHookInfo, sizeof(ExampleHookInfo)))
    {
        ReportError("Failed to get module info somehow");
        return;
    }
    SetupExceptionHandler(ExampleHook, ExampleHookInfo.SizeOfImage);
#endif

    // if you dont want your window to appear immediately just remove this code
    if (EnableUI) RenderWindow();

    while (true)
    {
        if (EnableUI && GetKeyState(VK_F2) & 0x8000) RenderWindow();
        Sleep(10);
    }
}

int __stdcall DllMain(HMODULE hModule, DWORD CallReason, PVOID)
{
    if (CallReason == DLL_PROCESS_ATTACH)
    {
        // manual map case
        if (hModule) DisableThreadLibraryCalls(hModule);
        std::thread(MainFunction, hModule).detach();
    }
}

