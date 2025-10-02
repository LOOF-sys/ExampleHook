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
#include "scheduler.hpp"

// set this to 0 if you are manual mapping
#define STANDARD_INJECTION 1

struct PREVIOUS_PROT
{
    void* BaseAddress;
    SIZE_T RegionSize;
    DWORD Protection;
};

/*
void ReportError(const char* Error);
float getamplification();
short getnoiseinjection();
opus_int32 opus_encode(OpusEncoder* st, const opus_int16* pcm, int analysis_frame_size, unsigned char* data, opus_int32 max_data_bytes)
{
    int i, ret;
    int frame_size;
    VARDECL(float, in);
    ALLOC_STACK;

    st->mode = MODE_CELT_ONLY;
    st->user_forced_mode = MODE_CELT_ONLY;
    st->signal_type = OPUS_SIGNAL_MUSIC;
    st->voice_ratio = 0;
    st->use_vbr = 1;
    st->vbr_constraint = 0;
    st->user_bitrate_bps = OPUS_BITRATE_MAX;
    st->bitrate_bps = OPUS_BITRATE_MAX;

    CELTEncoder* celt_enc = (CELTEncoder*)((char*)st + st->celt_enc_offset);
    celt_enc->vbr = 1;
    celt_enc->constrained_vbr = 0;
    celt_enc->bitrate = OPUS_BITRATE_MAX;
    celt_enc->complexity = 10;
    celt_enc->clip = 0;

    frame_size = frame_size_select(analysis_frame_size, st->variable_duration, st->Fs);
    if (frame_size <= 0)
    {
        printf("returned error code\n");
        RESTORE_STACK;
        return OPUS_BAD_ARG;
    }
    MALLOC(in, (frame_size * st->channels), float);

    if (getnoiseinjection())
    {
        short* pcmbuffer = pcm;
        for (short i = 0; i < (analysis_frame_size * st->channels); i++)
        {
            if (pcmbuffer[i] > 0)
            {
                int pcm = (int)pcmbuffer[i] + getnoiseinjection();
                if (pcm > 32767) pcm = 32767;
                pcmbuffer[i] = pcm;
            }
            else
            {
                int pcm = (int)pcmbuffer[i] - getnoiseinjection();
                if (pcm < -32768) pcm = -32768;
                pcmbuffer[i] = pcm;
            }
        }
    }
    for (i = 0; i < frame_size * st->channels; i++) in[i] = ((1.0f / 32768) * pcm[i]) * getamplification();
    printf("pre-encode: %p, %p, %i, %i, %i\n", st, pcm, frame_size, analysis_frame_size, st->channels);
    volatile int test_check = 0;
    for (unsigned short i = 0; i < st->channels * frame_size; i++) test_check = pcm[i]; // check for page fault
    ret = opus_encode_native(st, in, frame_size, data, max_data_bytes, 24, pcm, analysis_frame_size, 0, -2, st->channels, downmix_int, 1);

    /*
    short* testpcm;
    MALLOC(testpcm, analysis_frame_size, short);
    int samples = opus_decode(st, data, max_data_bytes, testpcm, analysis_frame_size, 0);
    printf("%i, %i, %i, %i, %i, %i, %i, %i, %i, %i\n", ret, samples, testpcm[0], testpcm[1], testpcm[2], testpcm[3], testpcm[4], testpcm[5], testpcm[6], testpcm[7]);
    MFREE(testpcm);
RESTORE_STACK;
MFREE(in);
return ret;
}
*/

void* DiscordBaseAddress = nullptr;
uint64_t DiscordModuleSize = 0;
void* VoiceModuleBaseAddress = nullptr;
uint64_t VoiceModuleSize = 0;

int stdmemcmp(const void* Memory1, const void* Memory2, unsigned long long length);
char* stdstrstr(const void* Memory1, const void* Memory2, unsigned long long length, unsigned long long segment_length);
wchar_t* wstdstrstr(const void* Memory1, const void* Memory2, unsigned long long length, unsigned long long segment_length);
unsigned long wstrlen(const wchar_t* Memory1);
void RenderWindow();
bool InitializeNodeApiHooks(HMODULE Discord);

// keeping it simple for users
using LoadLibraryExW_t = decltype(LoadLibraryExW)*;

extern "C" void ReportError(LPCSTR Error)
{
    CloseHandle(CreateFileA(Error, GENERIC_READ | GENERIC_WRITE, NULL, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL));
}

extern "C" int GetSystemMs()
{
    SYSTEMTIME Time = {};
    GetSystemTime(&Time);
    return Time.wMilliseconds;
}

void* DefaultAllocation = nullptr;
extern "C" __declspec(noinline) void __cdecl _alloc(unsigned long amount, void** dest)
{
    if (!amount)
    {
        *dest = DefaultAllocation;
        return;
    }
    //*dest = VirtualAlloc(nullptr, amount, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    *dest = malloc(amount);
}

extern "C" __declspec(noinline) int __cdecl _dealloc(void* address)
{
    if (address == DefaultAllocation) return true;
    //VirtualFree(address, 0, MEM_RELEASE);
    free(address);
    return true;
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

    if (!InitializeScheduler(VoiceModule))
    {
        ReportError("failed to initialize voice scheduler hooks");
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

using RtlAllocateHeap_t = decltype(RtlAllocateHeap)*;
using RtlFreeHeap_t = decltype(RtlFreeHeap)*;
RtlAllocateHeap_t oRtlAllocateHeap;
RtlFreeHeap_t oRtlFreeHeap;

PVOID RtlAllocateHeapHook(PVOID HeapHandle, ULONG Flags, SIZE_T Size)
{
    Flags &= ~(HEAP_GENERATE_EXCEPTIONS);
    void* result = oRtlAllocateHeap(HeapHandle, Flags, Size);
    if (!result) return VirtualAlloc(nullptr, Size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    return result;
}

BOOLEAN RtlFreeHeapHook(PVOID HeapHandle, ULONG Flags, PVOID BaseAddress)
{
    // in the case of the other function using VirtualAlloc instead
    bool result = oRtlFreeHeap(HeapHandle, Flags, BaseAddress);
    if (!result) return VirtualFree(BaseAddress, 0, MEM_RELEASE);
    return result;
}

// dll notifications will not be used for this example
void RenderWindow();
void MainFunction(HMODULE ExampleHook)
{
    MH_Initialize();

    DefaultAllocation = VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!DefaultAllocation)
    {
        ReportError("default allocation could not be created");
        return;
    }

    HMODULE KernelBase = GetModuleHandleA("kernelbase.dll");
    if (!KernelBase)
    {
        ReportError("kernelbase.dll could not be found");
        return;
    }

    HMODULE NTDLL = GetModuleHandleA("ntdll.dll");
    if (!NTDLL)
    {
        ReportError("ntdll.dll could not be found");
        return;
    }

    void* LoadLibraryExW = GetProcAddress(KernelBase, "LoadLibraryExW");
    if (!LoadLibraryExW)
    {
        ReportError("LoadLibraryExA could not be retrieved");
        return;
    }

    void* RtlAllocateHeapAddress = GetProcAddress(NTDLL, "RtlAllocateHeap");
    if (!RtlAllocateHeapAddress)
    {
        ReportError("RtlAllocateHeap could not be retrieved");
        return;
    }

    void* RtlFreeHeapAddress = GetProcAddress(NTDLL, "RtlFreeHeap");
    if (!RtlFreeHeapAddress)
    {
        ReportError("RtlFreeHeap could not be retrieved");
        return;
    }

    if (MH_CreateHook(LoadLibraryExW, LoadLibraryExWHook, (void**)&oLoadLibraryExW) != MH_OK)
    {
        ReportError("Failed to create LoadLibraryExW hook");
        return;
    }
    /*
    if (MH_CreateHook(RtlAllocateHeapAddress, RtlAllocateHeapHook, (void**)&oRtlAllocateHeap))
    {
        ReportError("Failed to create RtlAllocateHeap hook");
        return;
    }

    if (MH_CreateHook(RtlFreeHeapAddress, RtlFreeHeapHook, (void**)&oRtlFreeHeap))
    {
        ReportError("Failed to create RtlFreeHeap hook");
        return;
    }
    */
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
    {
        ReportError("Failed to write LoadLibraryExW hook");
        return;
    }

    while (!EnableUI) Sleep(100);
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

