#include <iostream>
#include <vector>
#include <windows.h>
#include <psapi.h>
#include <deque>
#include "offsets.hpp"
#include "minhook/minhook.h"

using SendData_t = void(*)(uint32_t frame_type, uint8_t payload_type, uint32_t timestamp, uint8_t* payload_data, size_t payload_len_bytes, int64_t absolute_capture_timestamp_ms);
using ProxyRoutine_t = PVOID(*)(void* unk1, void* unk2);
using SendRtpAudio_t = PVOID(*)(__int64 a1, int a2, int a3, int a4, __int64* a5, __int64 a6);
using PrioritizedPacketQueue_constructor_t = PVOID(*)(void* This, void* creation_time);
using Pop_t = PVOID(*)(void* This, void* unk1);

// next task is to get every PacketQueue and have a button that clears all of them either you do it by spamming ::Pop() or clear them all by ssrc

// class VTable
struct AudioPacketizationCallback
{
	void** vfunc1ref;
	void** vfunc2ref;
	void** vfunc3ref;
};

struct AudioPacketizationParent
{
	AudioPacketizationCallback* TransportClass;
};

using RegisterTransportCallback_t = PVOID(*)(void* This, AudioPacketizationParent* ADPClass);

// confirm that this is the actual sender and it is
SendData_t SendData;
void SendDataHook(uint32_t frame_type, uint8_t payload_type, uint32_t timestamp, uint8_t* payload_data, size_t payload_len_bytes, int64_t absolute_capture_timestamp_ms)
{
	//printf("sent data\n");
	return SendData(frame_type, payload_type, timestamp, payload_data, payload_len_bytes, absolute_capture_timestamp_ms);
}

SendRtpAudio_t SendRtpAudio;
extern "C" PVOID SendRtpAudioHook(__int64 a1, int a2, int a3, int a4, __int64* a5, __int64 a6);
extern "C" PVOID SendRtpAudioCallback(__int64 a1, int a2, int a3, int a4, __int64* a5, __int64 a6)
{
	return SendRtpAudio(a1, a2, a3, a4, a5, a6);
}

bool IsAddressValid(void* VirtualAddress);

/* 
* INFO ABOUT THIS METHOD
* - Determined that the cause for packet line clogging is not in the deque objects, it is further back in the control flow
*/
Pop_t Pop;
uint64_t counter = 0;
int GetPacketSkipRate();
void* PopHook(void* This, void* unk1)
{
	uint64_t v4 = *(int*)((uintptr_t)This + 296);
	auto deque_object = (std::deque<void*>*)*(void**)(*(uint64_t*)(*(uint64_t*)((uintptr_t)This + 40 * v4 + 144)
		+ 8 * ((*(uint64_t*)((uintptr_t)This + 40 * v4 + 160) >> 1) & (*(uint64_t*)((uintptr_t)This + 40 * v4 + 152) - 1LL)))
		+ 8 * (*(uint64_t*)((uintptr_t)This + 40 * v4 + 160) & 1LL));
	if ((counter++ % GetPacketSkipRate()) == 0) Pop(This, unk1);
	return Pop(This, unk1);
}

PrioritizedPacketQueue_constructor_t PrioritizedPacketQueue_constructor;
void* PrioritizedPacketQueue_constructorHook(void* This, void* creation_time)
{
	printf("created packet queue\n");
	return PrioritizedPacketQueue_constructor(This, creation_time);
}

// not currently used but was intended for full clearage of all std::deque lines
std::vector<uint32_t> ssrcs = {};
void InsertSSRC(uint32_t ssrc)
{
	for (uint32_t i = 0; i < ssrcs.size(); i++) if (ssrcs[i] == ssrc) return;
	ssrcs.push_back(ssrc);
}

bool AlreadyHooked = false;
extern void* VoiceModuleBaseAddress;
extern uint64_t VoiceModuleSize;
RegisterTransportCallback_t RegisterTransportCallback;
void* RegisterTransportCallbackHook(void* This, AudioPacketizationParent* ADPClass)
{
	if (AlreadyHooked) goto RTC_end;
	AlreadyHooked = true;
	if (This && ADPClass && IsAddressValid(This) && IsAddressValid(ADPClass))
	{
		AudioPacketizationCallback* TransportClass = ADPClass->TransportClass;
		printf("%p, %p, %p, %p, %p, %p, discord_voice.node+0x%p\n", VoiceModuleBaseAddress, This, ADPClass, TransportClass, (void*)((uintptr_t)TransportClass->vfunc1ref - (uintptr_t)VoiceModuleBaseAddress), TransportClass->vfunc3ref, (void*)((uintptr_t)TransportClass->vfunc3ref - (uintptr_t)VoiceModuleBaseAddress));
		DWORD oldprot = 0;
		if (!VirtualProtect(TransportClass->vfunc3ref, 0x1000, PAGE_EXECUTE_READWRITE, &oldprot))
		{
			printf("could not gain write access to \"SendData\", cannot syncronize packet schedule\n");
			goto RTC_end;
		}
		if (MH_CreateHook(TransportClass->vfunc3ref, SendDataHook, (void**)&SendData) != MH_OK)
		{
			printf("failed to create hook at \"SendData\", cannot syncronize packet schedule\n");
			goto RTC_end;
		}
		if (MH_EnableHook(TransportClass->vfunc3ref) != MH_OK)
		{
			printf("failed to do hook at \"SendData\", cannot syncronize packet schedule\n");
			goto RTC_end;
		}
		DWORD unused = 0;
		VirtualProtect(TransportClass->vfunc3ref, oldprot, oldprot, &unused);
	}
	RTC_end:
	return RegisterTransportCallback(This, ADPClass);
}

bool InitializeScheduler(HMODULE DiscordVoiceModule)
{
	MODULEINFO ModuleInfo = {};
	if (!K32GetModuleInformation((HANDLE)(~0ull), DiscordVoiceModule, &ModuleInfo, sizeof(ModuleInfo))) return false;
	VoiceModuleBaseAddress = DiscordVoiceModule;
	VoiceModuleSize = ModuleInfo.SizeOfImage;

	//if (MH_CreateHook((void*)((uintptr_t)DiscordVoiceModule + offsets::AudioCodingModuleImpl__RegisterTransportCallback()), RegisterTransportCallbackHook, (void**)&RegisterTransportCallback) != MH_OK) return false;
	if (MH_CreateHook((void*)((uintptr_t)DiscordVoiceModule + offsets::PrioritizedPacketQueue_constructor()), PrioritizedPacketQueue_constructorHook, (void**)&PrioritizedPacketQueue_constructor) != MH_OK) return false;
	if (MH_CreateHook((void*)((uintptr_t)DiscordVoiceModule + offsets::Pop()), PopHook, (void**)&Pop) != MH_OK) return false;
	//if (MH_CreateHook((void*)((uintptr_t)DiscordVoiceModule + offsets::ChannelSend__SendRtpAudio()), SendRtpAudioHook, (void**)&SendRtpAudio) != MH_OK) return false;

	return true;
}