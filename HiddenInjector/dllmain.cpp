#include <iostream>
#include <windows.h>
#include <tlhelp32.h>

// this dll can be turned into a dll hijack or not, this method will not be documented on discord server
/*
* you can inject this dll into a process and this dll will act as the injector, making it harder to find the injector
* for the actual hook, you can manual map it
*/
int __stdcall DllMain(HMODULE hModule, DWORD CallReason, LPVOID)
{
    if (CallReason == DLL_PROCESS_ATTACH)
    {

    }
}

