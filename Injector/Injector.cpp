#include <iostream>
#include <windows.h>
#include <tlhelp32.h>
#include <vector>

#define HOOK_DLL_NAME "ExampleHookMm.dll"

// standard injection, use hidden injector for manual map
int main()
{
	// comment for beginners: this is used to check if the dll is located in the current directory
	OFSTRUCT unused = {};
	auto File = (HANDLE)OpenFile(HOOK_DLL_NAME, &unused, OF_READ);
	if (File == INVALID_HANDLE_VALUE)
	{
		MessageBoxA(NULL, "ExampleHook.dll is missing", "Injector", MB_ICONERROR);
		system("pause");
		return 0;
	}
	CloseHandle(File);

	// comment for beginners: this is a list of all already injected discord processes
	std::vector<DWORD> Injected = {};
	while (true)
	{
		// comment for beginners: this is just how you access a list to all processes
		HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
		if (Snapshot == INVALID_HANDLE_VALUE)
		{
			std::cout << "could not query process information" << std::endl;
			continue;
		}

		// comment for beginners: this is just a structure used for storing basic process details, set size so windows knows the version of the structure
		PROCESSENTRY32 ProcessEntry = {};
		ProcessEntry.dwSize = sizeof(ProcessEntry);

		while (Process32Next(Snapshot, &ProcessEntry))
		{
			// comment for beginners: its not because memcmp returns 0 if its ==, sizeof("") - 1 is because sizeof includes null terminator
			if (!memcmp(ProcessEntry.szExeFile, "Discord.exe", sizeof("Discord.exe") - 1))
			{
				// comment for beginners: checks if the process has already been injected
				bool skip = false;
				for (uint16_t i = 0; i < Injected.size(); i++)
				{
					if (Injected[i] == ProcessEntry.th32ProcessID)
					{
						skip = true;
						break;
					}
				}
				if (skip) continue;

				// comment for beginners: access the process, check if its accessed
				HANDLE Process = OpenProcess(PROCESS_ALL_ACCESS, false, ProcessEntry.th32ProcessID);
				if (Process == INVALID_HANDLE_VALUE)
				{
					std::cout << "Access to process denied, is this process launched as admin?" << std::endl;
					continue;
				}

				// comment for beginners: get the path for the dll
				char LocalModulePath[MAX_PATH] = {};
				if (!GetFullPathNameA(HOOK_DLL_NAME, MAX_PATH, LocalModulePath, NULL))
				{
					std::cout << "Somehow failed" << std::endl;
					continue;
				}

				// comment for beginners: allocate memory into the process for LoadLibrary() to use as a path name
				void* ExternalPath = VirtualAllocEx(Process, nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
				if (!ExternalPath)
				{
					std::cout << "Pagefile is full or memory access is denied" << std::endl;
					continue;
				}

				// comment for beginners: writes the external memory with the local file path
				if (!WriteProcessMemory(Process, ExternalPath, LocalModulePath, sizeof(LocalModulePath), NULL))
				{
					std::cout << "Write access denied with code " << GetLastError() << std::endl;
					continue;
				}

				HANDLE Thread = CreateRemoteThread(Process, NULL, NULL, (PTHREAD_START_ROUTINE)LoadLibraryA, ExternalPath, NULL, NULL);
				if (Thread == INVALID_HANDLE_VALUE)
				{
					std::cout << "Failed to create a thread under the specified process" << std::endl;
					continue;
				}
				Injected.push_back(ProcessEntry.th32ProcessID);
				CloseHandle(Process);

				std::cout << "Injected " << ProcessEntry.th32ProcessID << std::endl;
			}
		}
		CloseHandle(Snapshot);
		Sleep(10);
	}
}