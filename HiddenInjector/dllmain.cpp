#include <iostream>
#include <vector>
#include <thread>
#include <windows.h>
#include <tlhelp32.h>

// this dll can be turned into a dll hijack or not, this method will not be documented on discord server
/*
* you can inject this dll into a process and this dll will act as the injector, making it harder to find the injector
* for the actual hook, you can manual map it
*/

#define HOOK_DLL_NAME "ExampleHook.dll"
#define DISCORD_PROCESS "Discord.exe"

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
            printf("failed to query process information\n");
            return;
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

                // comment for beginners: allocate memory into the process for LoadLibrary() to use as a path name
                void* ExternalPath = VirtualAllocEx(Process, nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                if (!ExternalPath) continue;

                // comment for beginners: writes the external memory with the local file path
                if (!WriteProcessMemory(Process, ExternalPath, LocalModulePath, sizeof(LocalModulePath), NULL)) continue;

                HANDLE Thread = CreateRemoteThread(Process, NULL, NULL, (PTHREAD_START_ROUTINE)LoadLibraryA, ExternalPath, NULL, NULL);
                if (Thread == INVALID_HANDLE_VALUE) continue;
                Injected.push_back(ProcessEntry.th32ProcessID);
            }
        }
        Sleep(10);
    }
}

int __stdcall DllMain(HMODULE hModule, DWORD CallReason, LPVOID)
{
    if (CallReason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        std::thread(MainFunction).detach();
    }
    return 1;
}

