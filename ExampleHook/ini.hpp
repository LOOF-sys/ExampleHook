#include <iostream>
#include <string>
#include <vector>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

typedef struct
{
	LPCSTR ValueName;
	PVOID Value;
} TEXTLINE, * PTEXTLINE;

__forceinline void Error(LPCSTR ErrorText)
{
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 12);
	std::cout << "[INI Parser] " << ErrorText << std::endl;
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);
}

__forceinline void Warning(LPCSTR ErrorText)
{
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 14);
	std::cout << "[INI Parser] " << ErrorText << std::endl;
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);
}

__forceinline bool StringToBool(LPCSTR String)
{
	if (!String) return false;
	return (!stricmp(String, "true"));
}

__forceinline bool IsEqualSign(char Character)
{
	return (Character == '=');
}

__forceinline bool IsNumber(char Character)
{
	return (Character >= '0' && Character <= '9');
}

__forceinline bool IsUtf8Character(char Character)
{
	return ((Character >= '!' && Character <= 'Z') || (Character >= 'a' && Character <= 'z'));
}

__forceinline bool IsComment(char Character)
{
	return (Character == '#');
}

__forceinline bool IsInvalidCharacter(char Character)
{
	return (Character == '\n' || Character == '\0' || Character == 13);
}

__forceinline uint64_t WaitTillEndline(PBYTE Data, uint64_t iterator, uint64_t Size)
{
	for (uint64_t i = iterator; i < Size; i++)
	{
		if (Data[i] == '\n') return i;
	}
}

class IniParser
{
public:
	IniParser(LPCSTR FileName) // taking in a file handle and handling that entire process support can be added if necessary
	{
		// FileName ignored
		char ConfigFileDirectory[MAX_PATH] = {};
		if (!GetFullPathNameA("config.ini", sizeof(ConfigFileDirectory), ConfigFileDirectory, NULL))
		{
			printf("Constructor failed [0:1]\n");
			return;
		}

		OFSTRUCT BasicInfo = {};
		auto ConfigFile = (HANDLE)OpenFile(ConfigFileDirectory, &BasicInfo, OF_READ);
		if (ConfigFile == INVALID_HANDLE_VALUE || !ConfigFile)
		{
			printf("Constructor failed [1]\n");
			return;
		}

		LARGE_INTEGER FullFileSize = {};
		if (!GetFileSizeEx(ConfigFile, &FullFileSize))
		{
			printf("Constructor failed [2]\n");
			return;
		}

		uint64_t FileSize = FullFileSize.QuadPart;
		auto FileData = (PBYTE)VirtualAlloc(nullptr, FileSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!FileData)
		{
			printf("Constructor failed [3]\n");
			return;
		}

		DWORD BytesRead = 0;
		if (!ReadFile(ConfigFile, FileData, FileSize, &BytesRead, NULL))
		{
			printf("Constructor failed [4]\n");
			VirtualFree(FileData, 0, MEM_RELEASE);
			return;
		}

		auto ValueName = (PBYTE)VirtualAlloc(nullptr, FileSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		auto RawValue = (PBYTE)VirtualAlloc(nullptr, FileSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!ValueName || !RawValue)
		{
			printf("Constructor failed [5]\n");
			VirtualFree(FileData, 0, MEM_RELEASE);
			VirtualFree(ValueName, 0, MEM_RELEASE);
			VirtualFree(RawValue, 0, MEM_RELEASE);
			return;
		}

		for (uint64_t i = 0; i < FileSize; i++)
		{
			if (FileData[i] == '#') // comented out
			{
				i = WaitTillEndline(FileData, i, FileSize);
				continue;
			}
			// value entry
			RtlZeroMemory(ValueName, FileSize);
			RtlZeroMemory(RawValue, FileSize);
			TEXTLINE Line = {};
			uint64_t Idx = 0;
			for (i; i < FileSize; i++, Idx++)
			{
				// current value
				if (IsEqualSign(FileData[i]))
				{
					auto AllocatedValueName = (PBYTE)VirtualAlloc(nullptr, Idx, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
					if (!AllocatedValueName)
					{
						printf("Constructor failed [6]\n");
						return;
					}

					memcpy(AllocatedValueName, ValueName, Idx);
					Line.ValueName = (LPCSTR)AllocatedValueName; ++i;
					if (IsNumber(FileData[i]))
					{
						Idx = 0;
						for (i; i < FileSize; i++, Idx++)
						{
							if (IsInvalidCharacter(FileData[i]))
							{
								i = WaitTillEndline(FileData, i, FileSize);
								break;
							}

							if (!IsNumber(FileData[i]))
							{
								Error("Invalid syntax, number needs to be a number");
								i = WaitTillEndline(FileData, i, FileSize);
								break;
							}

							if (Idx >= 19)
							{
								Warning("Invalid data type, number is too large");
								i = WaitTillEndline(FileData, i, FileSize);
								break;
							}
							RawValue[Idx] = FileData[i];
						}
						std::string Value = (LPCSTR)RawValue;
						Line.Value = (PVOID)std::stoull(Value);
						Lines.push_back(Line);
						break;
					}
					else if (IsUtf8Character(FileData[i]))
					{
						Idx = 0;
						for (i; i < FileSize; i++, Idx++)
						{
							if (IsInvalidCharacter(FileData[i]))
							{
								i = WaitTillEndline(FileData, i, FileSize);
								break;
							}
							RawValue[Idx] = FileData[i];
						}

						auto AllocatedValue = (PBYTE)VirtualAlloc(nullptr, FileSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
						if (!AllocatedValue)
						{
							printf("Constructor failed [7]\n");
							return;
						}

						memcpy(AllocatedValue, RawValue, FileSize);
						Line.Value = AllocatedValue;
						Lines.push_back(Line);
						break;
					}
					else
					{
						Error("Invalid syntax, values are required to fill in these");
						Line.Value = (PVOID)"";
						Lines.push_back(Line);
						break;
					}
				}
				ValueName[Idx] = FileData[i];
			}
		}
		VirtualFree(ValueName, 0, MEM_RELEASE);
		VirtualFree(RawValue, 0, MEM_RELEASE);
		VirtualFree(FileData, 0, MEM_RELEASE);
		//CloseHandle()
		Initalized = true;
		Default.Value = 0;
		Default.ValueName = "Default";
	}

	void discard()
	{
		for (uint64_t i = 0; i < Lines.size(); i++)
		{
			MEMORY_BASIC_INFORMATION MemoryInfo = {};
			VirtualFree((PVOID)Lines[i].ValueName, 0, MEM_RELEASE);
			if (VirtualQuery(Lines[i].Value, &MemoryInfo, sizeof(MemoryInfo))) if (MemoryInfo.State != MEM_FREE) VirtualFree(Lines[i].Value, 0, MEM_RELEASE);
		}
		Lines.clear();
	}

	PTEXTLINE GetLineByName(LPCSTR LineName)
	{
		for (uint64_t i = 0; i < Lines.size(); i++)
		{
			if (!strcmp(Lines[i].ValueName, LineName)) return &Lines[i];
		}
		return &Default;
	}

	PTEXTLINE GetLine(uint64_t Line)
	{
		return &Lines[Line];
	}

	bool IsInitialized()
	{
		return Initalized;
	}

private:
	TEXTLINE Default;
	std::vector<TEXTLINE> Lines;
	bool Initalized = false;
};