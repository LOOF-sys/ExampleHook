#include <iostream>
#include <vector>
#include <thread>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

// this dll can be turned into a dll hijack or not, this method will not be documented on discord server
/*
* you can inject this dll into a process and this dll will act as the injector, making it harder to find the injector
* for the actual hook, you can manual map it
*/

#define DEBUG_MODE 0
#if (DEBUG_MODE)
#define _printf(...) printf(__VA_ARGS__)
#else
#define _printf(...)
#endif

#define ANTI_SCAN_DLL "AntiScan.dll"
#define DISCORD_PROCESS "Discord.exe"
#define HOOK_DLL_NAME "ExampleHookMm.dll"

#define USE_OVERRIDE_PATH 0
#if (USE_OVERRIDE_PATH)
#define OVERRIDE_PATH "E:\\source\\repos\\ExampleHook\\x64\\Release\\"
#endif
#define MANUAL_MAP_INJECT_HOOK 1

// possible risk of processes using these names to bypass the anti-scan, if this occurs then this will be removed and swapped for something better
#define IsBlacklistedProcess(ProcessName) (!strcmp(ProcessName, "Discord.exe") || !strcmp(ProcessName, "DiscordPTB.exe") || !strcmp(ProcessName, "DiscordDevelopment.exe") || !strcmp(ProcessName, "dllhost.exe"))

bool HasModuleLoaded(HANDLE Process, LPCSTR ModuleName)
{
    HMODULE Modules[1024] = {};
    DWORD UsedBytes = 0;
    if (!K32EnumProcessModules(Process, Modules, sizeof(Modules), &UsedBytes)) return false;

    for (uint16_t i = 0; i < UsedBytes / sizeof(Modules[0]); i++)
    {
        char ThisModuleName[MAX_PATH] = {};
        if (!K32GetModuleBaseNameA(Process, Modules[i], ThisModuleName, sizeof(ThisModuleName))) continue;
        if (!stricmp(ThisModuleName, ModuleName)) return true;
    }
    return false;
}

bool ManualMap(HANDLE process_handle, const char* binary_path);
void AntiScan()
{
#if (DEBUG_MODE)
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
#endif

    std::vector<DWORD> Injected = {};
    PROCESSENTRY32 ProcessEntry = {};
    ProcessEntry.dwSize = sizeof(ProcessEntry);

    HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (Snapshot == INVALID_HANDLE_VALUE)
    {
        _printf("failed to query process information\n");
        return;
    }

    // enter all current process ids into a no injection list assuming the scanners are not running yet (this needs to be loaded before scanners)
    while (Process32Next(Snapshot, &ProcessEntry)) Injected.push_back(ProcessEntry.th32ProcessID);
    CloseHandle(Snapshot);

    _printf("loaded antiscan\n");

    while (true)
    {
        ProcessEntry = {};
        ProcessEntry.dwSize = sizeof(ProcessEntry);

        Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (Snapshot == INVALID_HANDLE_VALUE)
        {
            _printf("failed to query process information\n");
            continue;
        }

        // inject all new processes to protect against renaming and file resizing
        while (Process32Next(Snapshot, &ProcessEntry))
        {
            bool IsInjected = false;
            for (uint32_t i = 0; i < Injected.size(); i++)
            {
                if (Injected[i] == ProcessEntry.th32ProcessID)
                {
                    IsInjected = true;
                    break;
                }
            }
            if (IsInjected) continue;

            // comment for beginners: access the process, check if its accessed
            HANDLE Process = OpenProcess(PROCESS_ALL_ACCESS, false, ProcessEntry.th32ProcessID);
            if (Process == INVALID_HANDLE_VALUE) continue;
            if (IsBlacklistedProcess(ProcessEntry.szExeFile) || !HasModuleLoaded(Process, "kernel32.dll"))
            {
                _printf("process is blacklisted, %s, %i\n", ProcessEntry.szExeFile, HasModuleLoaded(Process, "kernel32.dll"));
                continue;
            }

            // comment for beginners: get the path for the dll
#if (USE_OVERRIDE_PATH)
            std::string LocalModulePath = OVERRIDE_PATH;
            LocalModulePath = LocalModulePath + ANTI_SCAN_DLL;
            if (!ManualMap(Process, LocalModulePath.c_str()))
            {
                _printf("antiscan injection failed %s, %i, %s\n", ProcessEntry.szExeFile, ProcessEntry.th32ProcessID, LocalModulePath.c_str());
                continue;
            }
#else
            char LocalModulePath[MAX_PATH] = {};
            if (!GetFullPathNameA(ANTI_SCAN_DLL, MAX_PATH, LocalModulePath, NULL)) continue;
            if (!ManualMap(Process, LocalModulePath)) continue;
#endif
            _printf("antiscan injected %s, %i, %s", ProcessEntry.szExeFile, ProcessEntry.th32ProcessID, LocalModulePath.c_str());
            CloseHandle(Process);
            Injected.push_back(ProcessEntry.th32ProcessID);
        }
        CloseHandle(Snapshot);
        Sleep(10);
    }
}

void MainFunction()
{
    std::vector<DWORD> Injected = {};
    while (true)
    {
        PROCESSENTRY32 ProcessEntry = {};
        ProcessEntry.dwSize = sizeof(ProcessEntry);

        HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (Snapshot == INVALID_HANDLE_VALUE)
        {
            _printf("failed to query process information\n");
            continue;
        }

        while (Process32Next(Snapshot, &ProcessEntry))
        {
            if (!memcmp(ProcessEntry.szExeFile, DISCORD_PROCESS, sizeof(DISCORD_PROCESS) - 1))
            {
                bool IsInjected = false;
                for (uint16_t i = 0; i < Injected.size(); i++)
                {
                    if (Injected[i] == ProcessEntry.th32ProcessID)
                    {
                        IsInjected = true;
                        break;
                    }
                }
                if (IsInjected) continue;

                // comment for beginners: access the process, check if its accessed
                HANDLE Process = OpenProcess(PROCESS_ALL_ACCESS, false, ProcessEntry.th32ProcessID);
                if (Process == INVALID_HANDLE_VALUE) continue;

                // comment for beginners: get the path for the dll
                char LocalModulePath[MAX_PATH] = {};
                if (!GetFullPathNameA(HOOK_DLL_NAME, MAX_PATH, LocalModulePath, NULL)) continue;

#if (!MANUAL_MAP_INJECT_HOOK)
                // comment for beginners: allocate memory into the process for LoadLibrary() to use as a path name
                void* ExternalPath = VirtualAllocEx(Process, nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                if (!ExternalPath) continue;

                // comment for beginners: writes the external memory with the local file path
                if (!WriteProcessMemory(Process, ExternalPath, LocalModulePath, sizeof(LocalModulePath), NULL)) continue;

                HANDLE Thread = CreateRemoteThread(Process, NULL, NULL, (PTHREAD_START_ROUTINE)LoadLibraryA, ExternalPath, NULL, NULL);
                if (Thread == INVALID_HANDLE_VALUE) continue;
#else
#if (USE_OVERRIDE_PATH)
                std::string _LocalModulePath = OVERRIDE_PATH;
                _LocalModulePath = _LocalModulePath + HOOK_DLL_NAME;
                if (!ManualMap(Process, _LocalModulePath.c_str())) continue;
#else
                if (!ManualMap(Process, LocalModulePath)) continue;
#endif
#endif
                CloseHandle(Process);
                Injected.push_back(ProcessEntry.th32ProcessID);
            }
        }
        Sleep(10);
    }
}

// will add customizable dll hijack later with "Setup.exe"
int __stdcall DllMain(HMODULE hModule, DWORD CallReason, LPVOID)
{
    if (CallReason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        std::thread(MainFunction).detach();
        std::thread(AntiScan).detach();
    }
    return 1;
}

