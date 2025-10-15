#include "ntdll.h"
#include <iostream>
#include <filesystem>
#include <thread>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <wintrust.h>
#include "minhook/minhook.h"

#define _printf(...) printf(__VA_ARGS__)
#define __debug__ 0
#define HIDDEN_MODULE_NAME "ExampleHook.dll"

using ReadProcessMemory_t = decltype(ReadProcessMemory)*;
using WriteProcessMemory_t = decltype(WriteProcessMemory)*;
using NtMapViewOfSection_t = decltype(NtMapViewOfSection)*;
using NtUnmapViewOfSection_t = decltype(NtUnmapViewOfSection)*;
using K32EnumProcessModules_t = decltype(K32EnumProcessModules)*;
using WinVerifyTrust_t = decltype(WinVerifyTrust)*;
using VirtualQueryEx_t = decltype(VirtualQueryEx)*;

ReadProcessMemory_t oReadProcessMemory;
WriteProcessMemory_t oWriteProcessMemory;
NtMapViewOfSection_t oNtMapViewOfSection;
NtUnmapViewOfSection_t oNtUnmapViewOfSection;
K32EnumProcessModules_t oK32EnumProcessModules;
WinVerifyTrust_t oWinVerifyTrust;
VirtualQueryEx_t oVirtualQueryEx;

struct THREAD_LOCAL_STORAGE
{
	DWORD ThreadId;
	bool RecursiveCall;
};

// windows provided TLS is not used because risk of writing to already occupied slots
std::vector<THREAD_LOCAL_STORAGE> LocalTls = {};
bool IsThreadRecursive()
{
	DWORD ThreadId = GetCurrentThreadId();
	for (uint16_t i = 0; i < LocalTls.size(); i++) return LocalTls[i].RecursiveCall;
	LocalTls.push_back({ThreadId, false});
	return false;
}
void SetThreadRecursive(bool state)
{
	DWORD ThreadId = GetCurrentThreadId();
	for (uint16_t i = 0; i < LocalTls.size(); i++) LocalTls[i].RecursiveCall = state;
}

bool IsAddressInModule(HANDLE Process, void* VirtualAddress)
{
	HMODULE Modules[1024] = {};
	DWORD UsedBytes = 0;
	if (!oK32EnumProcessModules(Process, Modules, sizeof(Modules), &UsedBytes)) return false;

	for (uint16_t i = 0; i < UsedBytes / sizeof(Modules[0]); i++)
	{
		MODULEINFO ModuleInfo = {};
		if (!K32GetModuleInformation(Process, Modules[i], &ModuleInfo, sizeof(ModuleInfo))) continue;
		if (VirtualAddress >= Modules[i] && VirtualAddress <= (void*)((uintptr_t)Modules[i] + ModuleInfo.SizeOfImage)) return true;
	}
	return false;
}

// used to get the new discord_voice.node base address every call
__declspec(noinline) void* FindDiscordVoiceBaseAddress(HANDLE Process, uint32_t* ModuleSize, char* path)
{
	HMODULE Modules[1024] = {};
	DWORD UsedBytes = 0;
	if (!oK32EnumProcessModules(Process, Modules, sizeof(Modules), &UsedBytes)) return nullptr;

	for (uint16_t i = 0; i < UsedBytes / sizeof(Modules[0]); i++)
	{
		char ModuleName[MAX_PATH] = {};
		if (!K32GetModuleBaseNameA(Process, Modules[i], ModuleName, sizeof(ModuleName))) continue;
		if (!memcmp(ModuleName, "discord_voice.node", sizeof("discord_voice.node") - 1))
		{
			MODULEINFO ModuleInfo = {};
			K32GetModuleInformation(Process, Modules[i], &ModuleInfo, sizeof(ModuleInfo));
			K32GetModuleFileNameExA(Process, Modules[i], path, MAX_PATH);
			*ModuleSize = ModuleInfo.SizeOfImage;
			return Modules[i];
		}
	}
	return nullptr;
}

void* GetFileDataInBuffer(PHANDLE FileHandle, const char* path)
{
	OFSTRUCT Junk = {};
	*FileHandle = (HANDLE)OpenFile(path, &Junk, OF_READ);
	if (*FileHandle == INVALID_HANDLE_VALUE) return nullptr;

	uint64_t FileSize = 0;
	if (!GetFileSizeEx(*FileHandle, (PLARGE_INTEGER)&FileSize)) return nullptr;

	void* FileData = VirtualAlloc(nullptr, FileSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!FileData) return nullptr;

	if (!ReadFile(*FileHandle, FileData, FileSize, NULL, NULL))
	{
		VirtualFree(FileData, 0, MEM_RELEASE);
		return nullptr;
	}

	return FileData;
}

void* DataMirrorAsLibrary = {};
void* GetDataMirrorAsLibrary(PBYTE FileData, uint32_t offset)
{
	if (!DataMirrorAsLibrary)
	{
		auto DosHeader = (PIMAGE_DOS_HEADER)FileData;
		auto NTHeaders = (PIMAGE_NT_HEADERS)((uintptr_t)FileData + DosHeader->e_lfanew);
		DataMirrorAsLibrary = VirtualAlloc(nullptr, NTHeaders->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!DataMirrorAsLibrary) return nullptr;
		auto Section = (PIMAGE_SECTION_HEADER)((uintptr_t)&NTHeaders->OptionalHeader + NTHeaders->FileHeader.SizeOfOptionalHeader);
		memcpy(DataMirrorAsLibrary, FileData, 0x400);
		memcpy((void*)((uintptr_t)DataMirrorAsLibrary + 0x1000), FileData + Section->PointerToRawData, Section->SizeOfRawData); // .text
		uint8_t i = 1;
		++Section;
		for (; Section++, i++; i < NTHeaders->FileHeader.NumberOfSections)
		{
			if (!Section->VirtualAddress || !Section->SizeOfRawData || Section->NumberOfLinenumbers || Section->NumberOfRelocations || Section->PointerToLinenumbers || (Section->VirtualAddress + Section->SizeOfRawData) > NTHeaders->OptionalHeader.SizeOfImage || (Section->PointerToRawData + Section->SizeOfRawData) > NTHeaders->OptionalHeader.SizeOfImage) continue;
			memcpy((void*)((uintptr_t)DataMirrorAsLibrary + Section->VirtualAddress), FileData + Section->PointerToRawData, Section->SizeOfRawData);
		}
	}
	return (void*)((uintptr_t)DataMirrorAsLibrary + offset);
}

SIZE_T __stdcall VirtualQueryExHook(HANDLE hProcess, LPCVOID lpAddress, PMEMORY_BASIC_INFORMATION lpBuffer, SIZE_T dwLength)
{
	if (IsThreadRecursive()) return oVirtualQueryEx(hProcess, lpAddress, lpBuffer, dwLength);
	SetThreadRecursive(true);
	uint32_t ModuleSize = 0;
	char DiscordModulePath[MAX_PATH] = {};
	void* BaseAddress = FindDiscordVoiceBaseAddress(hProcess, &ModuleSize, DiscordModulePath);
	if (BaseAddress && lpBuffer && !IsAddressInModule(hProcess, (void*)lpAddress))
	{
		SIZE_T result = oVirtualQueryEx(hProcess, lpAddress, lpBuffer, dwLength);
		if (lpBuffer->Protect == PAGE_EXECUTE || lpBuffer->Protect == PAGE_EXECUTE_READ || lpBuffer->Protect == PAGE_EXECUTE_READWRITE) lpBuffer->Protect = PAGE_NOACCESS | PAGE_GUARD;
		SetThreadRecursive(false);
		return result;
	}
	SetThreadRecursive(false);
	return oVirtualQueryEx(hProcess, lpAddress, lpBuffer, dwLength);
}

BOOL __stdcall ReadProcessMemoryHook(HANDLE hProcess, LPCVOID lpBaseAddress, LPVOID lpBuffer, SIZE_T nSize, SIZE_T* lpNumberOfBytesRead)
{
	if (IsThreadRecursive()) return oReadProcessMemory(hProcess, lpBaseAddress, lpBuffer, nSize, lpNumberOfBytesRead);
	SetThreadRecursive(true);
	uint32_t ModuleSize = 0;
	char DiscordModulePath[MAX_PATH] = {};
	void* BaseAddress = FindDiscordVoiceBaseAddress(hProcess, &ModuleSize, DiscordModulePath);
	if (BaseAddress && lpBaseAddress >= BaseAddress && lpBaseAddress <= (void*)((uintptr_t)BaseAddress + ModuleSize))
	{
		PROCESS_BASIC_INFORMATION ProcessBasicInfo = {};
		NTSTATUS Code = NtQueryInformationProcess(hProcess, ProcessBasicInformation, &ProcessBasicInfo, sizeof(ProcessBasicInfo), NULL);
		if (!NT_SUCCESS(Code))
		{
			_printf("warning: %p\n", (void*)Code);
			SetThreadRecursive(false);
			return oReadProcessMemory(hProcess, lpBaseAddress, lpBuffer, nSize, lpNumberOfBytesRead);
		}

		char* Path = DiscordModulePath;
		HANDLE File = {};
		void* FileData = GetFileDataInBuffer(&File, Path);
		if (!FileData)
		{
			_printf("warning: no file data\n");
			SetThreadRecursive(false);
			return oReadProcessMemory(hProcess, lpBaseAddress, lpBuffer, nSize, lpNumberOfBytesRead);
		}

		memcpy(lpBuffer, GetDataMirrorAsLibrary((PBYTE)FileData, (uintptr_t)lpBaseAddress - (uintptr_t)BaseAddress), nSize);
		if (lpNumberOfBytesRead) *lpNumberOfBytesRead = nSize;
		CloseHandle(File);
		VirtualFree(FileData, 0, MEM_RELEASE);
		SetThreadRecursive(false);
		return true;
	} else if (BaseAddress && !IsAddressInModule(hProcess, (void*)BaseAddress))
	{
		MEMORY_BASIC_INFORMATION MemoryInfo = {};
		if (!oVirtualQueryEx(hProcess, lpBaseAddress, &MemoryInfo, sizeof(MemoryInfo))) return oReadProcessMemory(hProcess, lpBaseAddress, lpBuffer, nSize, lpNumberOfBytesRead);
		if (MemoryInfo.Protect == PAGE_EXECUTE_READWRITE || MemoryInfo.Protect == PAGE_EXECUTE_READ || MemoryInfo.Protect == PAGE_EXECUTE)
		{
			SetThreadRecursive(false);
			RtlZeroMemory(lpBuffer, nSize);
			return true;
		}
	}
	SetThreadRecursive(false);
	return oReadProcessMemory(hProcess, lpBaseAddress, lpBuffer, nSize, lpNumberOfBytesRead);
}

bool WriteBlocker = false;
BOOL __stdcall WriteProcessMemoryHook(HANDLE hProcess, LPVOID lpBaseAddress, LPCVOID lpBuffer, SIZE_T nSize, SIZE_T* lpNumberOfBytesWritten)
{
	if (IsThreadRecursive()) return oWriteProcessMemory(hProcess, lpBaseAddress, lpBuffer, nSize, lpNumberOfBytesWritten);
	SetThreadRecursive(true);
	uint32_t ModuleSize = 0;
	char DiscordModulePath[MAX_PATH] = {};
	void* BaseAddress = FindDiscordVoiceBaseAddress(hProcess, &ModuleSize, DiscordModulePath);
	if (BaseAddress && lpBaseAddress >= BaseAddress && lpBaseAddress <= (void*)((uintptr_t)BaseAddress + ModuleSize))
	{
		// make it seem like all writes to discord_voice.node succeed
		// possible incorporation of mirror writes to fake it very well could be added
		SetThreadRecursive(false);
		if (WriteBlocker) return false;
		return true;
	}
	SetThreadRecursive(false);
	return oWriteProcessMemory(hProcess, lpBaseAddress, lpBuffer, nSize, lpNumberOfBytesWritten);
}

bool IsAddressValid(void* VirtualAddress)
{
	MEMORY_BASIC_INFORMATION MemoryInfo = {};
	if (!VirtualQuery(VirtualAddress, &MemoryInfo, sizeof(MemoryInfo))) return false;
	return (MemoryInfo.State != MEM_FREE && MemoryInfo.Protect != PAGE_NOACCESS);
}
 
NTSTATUS __stdcall NtMapViewOfSectionHook(HANDLE SectionHandle, HANDLE ProcessHandle, PVOID* BaseAddress, ULONG_PTR ZeroBits, SIZE_T CommitSize,PLARGE_INTEGER SectionOffset,PSIZE_T ViewSize, SECTION_INHERIT InheritDisposition, ULONG AllocationType, ULONG Win32Protect)
{
	if (IsThreadRecursive()) return oNtMapViewOfSection(SectionHandle, ProcessHandle, BaseAddress, ZeroBits, CommitSize, SectionOffset, ViewSize, InheritDisposition, AllocationType, Win32Protect);
	SetThreadRecursive(true);
	uint32_t ModuleSize = 0;
	char DiscordModulePath[MAX_PATH] = {};
	void* DiscordVoiceModuleBaseAddress = FindDiscordVoiceBaseAddress(ProcessHandle, &ModuleSize, DiscordModulePath);
	if (BaseAddress && *BaseAddress && *BaseAddress >= DiscordVoiceModuleBaseAddress && *BaseAddress <= (void*)((uintptr_t)DiscordVoiceModuleBaseAddress + ModuleSize))
	{
		SetThreadRecursive(false);
		WriteBlocker = true;
		return STATUS_SUCCESS; // successfully mapped (didnt)
	}
	SetThreadRecursive(false);
	return oNtMapViewOfSection(SectionHandle, ProcessHandle, BaseAddress, ZeroBits, CommitSize, SectionOffset, ViewSize, InheritDisposition, AllocationType, Win32Protect);
}

NTSTATUS __stdcall NtUnmapViewOfSectionHook(HANDLE ProcessHandle, PVOID BaseAddress)
{
	if (IsThreadRecursive()) return oNtUnmapViewOfSection(ProcessHandle, BaseAddress);
	SetThreadRecursive(true);
	uint32_t ModuleSize = 0;
	char DiscordModulePath[MAX_PATH] = {};
	void* DiscordVoiceModuleBaseAddress = FindDiscordVoiceBaseAddress(ProcessHandle, &ModuleSize, DiscordModulePath);
	SetThreadRecursive(false);
	if (BaseAddress == DiscordVoiceModuleBaseAddress) return STATUS_SUCCESS; // successfully unmapped (didnt)
	return oNtUnmapViewOfSection(ProcessHandle, BaseAddress);
}

BOOL __stdcall K32EnumProcessModulesHook(HANDLE hProcess, HMODULE* lphModule, DWORD cb, LPDWORD lpcbNeeded)
{
	BOOL result = oK32EnumProcessModules(hProcess, lphModule, cb, lpcbNeeded);
	if (IsThreadRecursive()) return result;
	SetThreadRecursive(true);
	for (uint16_t i = 0; i < (lpcbNeeded ? *lpcbNeeded / sizeof(HMODULE) : cb / sizeof(HMODULE)); i++)
	{
		char ModuleName[MAX_PATH] = {};
		if (!K32GetModuleBaseNameA(hProcess, lphModule[i], ModuleName, sizeof(ModuleName))) continue;
		if (!memcmp(ModuleName, HIDDEN_MODULE_NAME, sizeof(HIDDEN_MODULE_NAME) - 1))
		{
			lphModule[i] = lphModule[0];
			break;
		}
	}
	SetThreadRecursive(false);
	return true;
}

LONG __stdcall WinVerifyTrustHook(HWND hwnd, GUID* pgActionID, LPVOID pWVTData)
{
	if (hwnd && pgActionID && pWVTData) return 0;
	return oWinVerifyTrust(hwnd, pgActionID, pWVTData);
}

void MainFunction()
{
	MH_Initialize();

#if (__debug__)
	DWORD ProcessId = GetCurrentProcessId();
	if (!AttachConsole(ProcessId) && GetLastError() != ERROR_ACCESS_DENIED)
	{
		if (!AllocConsole())
		{
			//MessageBoxA(NULL, xorstr("Failed to allocate console"), xorstr("Discord"), MB_ICONERROR);
			return;
		}
	}
	freopen("conin$", "r", stdin);
	freopen("conout$", "w", stdout);
	freopen("conout$", "w", stderr);
#endif

	HMODULE Kernel32 = GetModuleHandleA("kernel32.dll");
	if (!Kernel32)
	{
		_printf("failed to get kernel32\n");
		return;
	}

	HMODULE KernelBase = GetModuleHandleA("kernelbase.dll");
	if (!KernelBase)
	{
		_printf("failed to get kernelbase\n");
		return;
	}

	HMODULE NTDLL = GetModuleHandleA("ntdll.dll");
	if (!NTDLL)
	{
		_printf("failed to get ntdll\n");
		return;
	}

	void* ReadProcessMemoryAddress = GetProcAddress(Kernel32, "ReadProcessMemory"); // special case because of K32GetModuleBaseNameA to avoid recursion
	void* WriteProcessMemoryAddress = GetProcAddress(KernelBase, "WriteProcessMemory");
	void* NtMapViewOfSectionAddress = GetProcAddress(NTDLL, "NtMapViewOfSection");
	void* NtUnmapViewOfSectionAddress = GetProcAddress(NTDLL, "NtUnmapViewOfSection");
	void* K32EnumProcessModulesAddress = GetProcAddress(KernelBase, "K32EnumProcessModules");
	void* VirtualQueryExAddress = GetProcAddress(KernelBase, "VirtualQueryEx");

	if (!ReadProcessMemoryAddress || !WriteProcessMemoryAddress || !NtMapViewOfSectionAddress || !NtUnmapViewOfSectionAddress || !K32EnumProcessModulesAddress || !VirtualQueryExAddress)
	{
		_printf("export missing\n");
		return;
	}

	if (MH_CreateHook(ReadProcessMemoryAddress, ReadProcessMemoryHook, (void**)&oReadProcessMemory))
	{
		_printf("failed to create hook for RPM\n");
		return;
	}

	if (MH_CreateHook(WriteProcessMemoryAddress, WriteProcessMemoryHook, (void**)&oWriteProcessMemory))
	{
		_printf("failed to create hook for WPM\n");
		return;
	}

	if (MH_CreateHook(NtMapViewOfSectionAddress, NtMapViewOfSectionHook, (void**)&oNtMapViewOfSection))
	{
		_printf("failed to create hook for NMV\n");
		return;
	}

	if (MH_CreateHook(NtUnmapViewOfSectionAddress, NtUnmapViewOfSectionHook, (void**)&oNtUnmapViewOfSection))
	{
		_printf("failed to create hook for NUV\n");
		return;
	}

	if (MH_CreateHook(K32EnumProcessModulesAddress, K32EnumProcessModulesHook, (void**)&oK32EnumProcessModules))
	{
		_printf("failed to create hook for EPM\n");
		return;
	}

	if (MH_CreateHook(VirtualQueryExAddress, VirtualQueryExHook, (void**)&oVirtualQueryEx))
	{
		_printf("failed to create hook for VQE\n");
		return;
	}

	HMODULE Wintrust = GetModuleHandleA("wintrust.dll");
	if (Wintrust)
	{
		void* WinVerifyTrustAddress = GetProcAddress(Wintrust, "WinVerifyTrust");
		if (MH_CreateHook(WinVerifyTrustAddress, WinVerifyTrustHook, (void**)&oWinVerifyTrust))
		{
			_printf("failed to create hook for WVT\n");
			return;
		}
	}

	MH_EnableHook(MH_ALL_HOOKS);
	while (true) Sleep(100);
}

int __stdcall DllMain(HMODULE Module, DWORD CallReason, PVOID)
{
	if (CallReason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(Module);
		std::thread(MainFunction).detach();
	}
	return 0;
}