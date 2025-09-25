#include <iostream>
#include <vector>
#include <string>
#include <format>
#include <windows.h>

void* BaseAddress = nullptr;
uint64_t LoadedModuleSize = 0;

std::string GetExceptionCodeAsString(DWORD Code)
{
	switch (Code)
	{
	case EXCEPTION_ACCESS_VIOLATION:
		return "EXCEPTION_ACCESS_VIOLATION";
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
	case EXCEPTION_BREAKPOINT:
		return "EXCEPTION_BREAKPOINT";
	case EXCEPTION_DATATYPE_MISALIGNMENT:
		return "EXCEPTION_DATATYPE_MISALIGNMENT";
	case EXCEPTION_FLT_DENORMAL_OPERAND:
		return "EXCEPTION_FLT_DENORMAL_OPERAND";
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
		return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
	case EXCEPTION_FLT_INEXACT_RESULT:
		return "EXCEPTION_FLT_INEXACT_RESULT";
	case EXCEPTION_FLT_INVALID_OPERATION:
		return "EXCEPTION_FLT_INVALID_OPERATION";
	case EXCEPTION_FLT_OVERFLOW:
		return "EXCEPTION_FLT_OVERFLOW";
	case EXCEPTION_FLT_STACK_CHECK:
		return "EXCEPTION_FLT_STACK_CHECK";
	case EXCEPTION_FLT_UNDERFLOW:
		return "EXCEPTION_FLT_UNDERFLOW";
	case EXCEPTION_ILLEGAL_INSTRUCTION:
		return "EXCEPTION_ILLEGAL_INSTRUCTION";
	case EXCEPTION_IN_PAGE_ERROR:
		return "EXCEPTION_IN_PAGE_ERROR";
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
		return "EXCEPTION_INT_DIVIDE_BY_ZERO";
	case EXCEPTION_INT_OVERFLOW:
		return "EXCEPTION_INT_OVERFLOW";
	case EXCEPTION_INVALID_DISPOSITION:
		return "EXCEPTION_INVALID_DISPOSITION";
	case EXCEPTION_NONCONTINUABLE_EXCEPTION:
		return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
	case EXCEPTION_PRIV_INSTRUCTION:
		return "EXCEPTION_PRIV_INSTRUCTION";
	case EXCEPTION_SINGLE_STEP:
		return "EXCEPTION_SINGLE_STEP";
	case EXCEPTION_STACK_OVERFLOW:
		return "EXCEPTION_STACK_OVERFLOW";
	default:
		return "UNDEFINED";
	}
	return "UNDEFINED";
}


bool IsAddressValid(void* VirtualAddress)
{
	MEMORY_BASIC_INFORMATION MemoryInfo = {};
	if (!VirtualQuery(VirtualAddress, &MemoryInfo, sizeof(MemoryInfo))) return false;
	return (MemoryInfo.Protect != PAGE_NOACCESS && MemoryInfo.State != MEM_FREE);
}


void AnalyzeStack(std::string &crashlog, void* StackAddress)
{
	std::vector<void*> values = {};
	for (uint8_t i = 0; i < 10; i++)
	{
		if (!IsAddressValid((void*)((uint64_t)StackAddress + (i * sizeof(void*))))) goto failed;
		values.push_back(*(void**)((uint64_t)StackAddress + (i * sizeof(void*))));
	}
	crashlog = crashlog + "Last 10 elements of the stack:\n{\n";
	for (uint8_t i = 0; i < 10; i++)
	{
		if (values[i] >= BaseAddress && ((uintptr_t)BaseAddress + LoadedModuleSize) >= (uintptr_t)values[i])
		{
			crashlog = crashlog + "     [+" +std::to_string(i * 8) + "] ExampleHook+0x" + std::format("{:x}", (uint64_t)values[i] - (uint64_t)BaseAddress) + ",\n";
			continue;
		}
		crashlog = crashlog + "     [+" + std::to_string(i * 8) + "] " + std::format("{:x}", (uint64_t)values[i]) + ",\n";
	}
	crashlog = crashlog + "}\n";
	goto completed;
failed:
	crashlog = crashlog + "Could not trace stack, invalid stack\n";
completed:
	return;
}

bool Crashed = false;
extern "C" void ReportError(LPCSTR Error);
LONG VectoredExceptionHandler(PEXCEPTION_POINTERS ExceptionPointers)
{
	if (Crashed)
	{
		// bandaid fix cause idk wtf is causing the problem
		if (ExceptionPointers->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
		{
			//printf("Page Fault occured at %p, auto resolve has been attempted\n", (void*)ExceptionPointers->ExceptionRecord->ExceptionInformation[1]);
			VirtualAlloc((void*)ExceptionPointers->ExceptionRecord->ExceptionInformation[1], 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		}
		return EXCEPTION_CONTINUE_EXECUTION; // keeps process loaded
	}

	if (ExceptionPointers->ExceptionRecord->ExceptionAddress >= BaseAddress && ((uint64_t)BaseAddress) + LoadedModuleSize >= (uint64_t)ExceptionPointers->ExceptionRecord->ExceptionAddress)
	{
		SYSTEMTIME SystemTime = {};
		GetSystemTime(&SystemTime);
		std::string filename = "exceptionlog_";
		filename = filename + std::to_string(SystemTime.wMilliseconds) + "-" + std::to_string(SystemTime.wMinute) + "-" + std::to_string(SystemTime.wHour) + "-" + std::to_string(SystemTime.wDay) + "-" + std::to_string(SystemTime.wMonth) + "-" + std::to_string(SystemTime.wYear) + ".txt";
		HANDLE LogFile = CreateFileA(filename.c_str(), GENERIC_READ | GENERIC_WRITE, NULL, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (LogFile == INVALID_HANDLE_VALUE)
		{
			printf("Failed to create log file\n");
			ReportError("Failed to create log file");
			return 0;
		}

		std::string crashlog = "Potential fatal error has just occured\nCurrent BaseAddress: 0x" + std::format("{:x}", (uint64_t)BaseAddress) + ", ImageSize: 0x" + std::format("{:x}", LoadedModuleSize) + "\nException Code: " + GetExceptionCodeAsString(ExceptionPointers->ExceptionRecord->ExceptionCode) + "\n";
		auto StackAddress = (void*)ExceptionPointers->ContextRecord->Rsp;
		void* ExceptionAddress = ExceptionPointers->ExceptionRecord->ExceptionAddress;
		if (ExceptionPointers->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
		{
			auto PageFaultAddress = (void*)ExceptionPointers->ExceptionRecord->ExceptionInformation[1];
			crashlog = crashlog + "Page Fault access (not instruction) occured at 0x" + std::format("{:x}", (uint64_t)PageFaultAddress) + "\n";
		}
		crashlog = crashlog + "Instruction that caused exception located at ExampleHook+0x" + std::format("{:x}", (uint64_t)ExceptionAddress - (uint64_t)BaseAddress) + "\n";
		AnalyzeStack(crashlog, StackAddress);
		crashlog = crashlog + "rsp: " + std::format("{:x}", (uint64_t)ExceptionPointers->ContextRecord->Rsp) + "\n";
		crashlog = crashlog + "rax: 0x" + std::format("{:x}", (uint64_t)ExceptionPointers->ContextRecord->Rax) + "\n";
		crashlog = crashlog + "rbx: 0x" + std::format("{:x}", (uint64_t)ExceptionPointers->ContextRecord->Rbx) + "\n";
		crashlog = crashlog + "rcx: 0x" + std::format("{:x}", (uint64_t)ExceptionPointers->ContextRecord->Rcx) + "\n";
		crashlog = crashlog + "rdx: 0x" + std::format("{:x}", (uint64_t)ExceptionPointers->ContextRecord->Rdx) + "\n";
		crashlog = crashlog + " r8: 0x" + std::format("{:x}", (uint64_t)ExceptionPointers->ContextRecord->R8) + "\n";
		crashlog = crashlog + " r9: 0x" + std::format("{:x}", (uint64_t)ExceptionPointers->ContextRecord->R9) + "\n";
		crashlog = crashlog + "r10: 0x" + std::format("{:x}", (uint64_t)ExceptionPointers->ContextRecord->R10) + "\n";
		crashlog = crashlog + "r11: 0x" + std::format("{:x}", (uint64_t)ExceptionPointers->ContextRecord->R11) + "\n";
		crashlog = crashlog + "r12: 0x" + std::format("{:x}", (uint64_t)ExceptionPointers->ContextRecord->R12) + "\n";
		crashlog = crashlog + "r13: 0x" + std::format("{:x}", (uint64_t)ExceptionPointers->ContextRecord->R13) + "\n";
		crashlog = crashlog + "r14: 0x" + std::format("{:x}", (uint64_t)ExceptionPointers->ContextRecord->R14) + "\n";
		crashlog = crashlog + "r15: 0x" + std::format("{:x}", (uint64_t)ExceptionPointers->ContextRecord->R15) + "\n";
		crashlog = crashlog + "if your audio has stopped working or discord has crashed, report this, otherwise ignore it\n";
		if (!WriteFile(LogFile, crashlog.c_str(), crashlog.size(), NULL, NULL))
		{
			printf("Failed to write to crashlog somehow\n");
			ReportError("Failed to write to crashlog somehow");
			return 0;
		}
		Crashed = true;
		std::cout << crashlog << std::endl;
		return EXCEPTION_CONTINUE_EXECUTION;
	}
	return 0;
}

void SetupExceptionHandler(HMODULE ExampleHook, uint64_t ImageSize)
{
	BaseAddress = ExampleHook;
	LoadedModuleSize = ImageSize;
	HANDLE ExceptionHandler = AddVectoredExceptionHandler(0, VectoredExceptionHandler);
	if (!ExceptionHandler)
	{
		printf("failed to attach exception handler\n");
		return;
	}
	printf("exception handler loaded %p\n", ExceptionHandler);
}