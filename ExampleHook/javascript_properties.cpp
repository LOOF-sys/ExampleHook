#include <stdio.h>
#include "minhook/MinHook.h"
#include "offsets.hpp"

struct napi_env
{
	void* unk1;
};

struct napi_value
{
	void* unk1;
};

//#define _printf(...) printf(__VA_ARGS__)
#define _printf()

using napi_get_named_property_t = uint64_t(*)(napi_env* env, napi_value* object, const char* name, napi_value** result);
using napi_get_value_uint32_t = uint64_t(*)(napi_env* env, napi_value* value, uint32_t* result);
using napi_get_value_int32_t = uint64_t(*)(napi_env* env, napi_value* value, int32_t* result);
using napi_get_value_bool_t = uint64_t(*)(napi_env* env, napi_value* value, bool* result);
using napi_get_value_double_t = uint64_t(*)(napi_env* env, napi_value* value, double* result);
using napi_set_named_property_t = uint64_t(*)(napi_env* env, napi_value* object, const char* name, napi_value* value);
using napi_get_value_string_utf8_t = uint64_t(*)(napi_env* env, napi_value* value, char* buffer, uint64_t bufsize, uint64_t* length);
using napi_create_string_utf8_t = uint64_t(*)(napi_env* env, const char* str, uint64_t length, napi_value* string);

napi_set_named_property_t napi_set_named_property;
napi_get_named_property_t napi_get_named_property;
napi_get_value_uint32_t napi_get_value_uint32;
napi_get_value_int32_t napi_get_value_int32;
napi_get_value_bool_t napi_get_value_bool;
napi_get_value_double_t napi_get_value_double;
napi_get_value_string_utf8_t napi_get_value_string_utf8;
napi_create_string_utf8_t napi_create_string_utf8;

napi_value* packetloss_object = {};
float GetPacketLossRate();
uint64_t napi_get_value_doublehook(napi_env* env, napi_value* value, double* result)
{
	if (value && value == packetloss_object)
	{
		_printf("packetLossRate spoofed\n");
		*result = (double)GetPacketLossRate();
		return 0;
	}
	return napi_get_value_double(env, value, result);
}

napi_value* fec_object = {};
uint64_t napi_get_value_boolhook(napi_env* env, napi_value* value, bool* result)
{
	if (value && value == (PVOID)fec_object)
	{
		_printf("fec spoofed\n");
		*result = false;
		return 0;
	}
	return napi_get_value_bool(env, value, result);
}

int32_t GetPacketBitrate();
napi_value* bitrate_object = {};
uint64_t napi_get_value_int32hook(napi_env* env, napi_value* value, int32_t* result)
{
	if (value && value == (PVOID)bitrate_object)
	{
		_printf("bitrate spoofed\n");
		*result = GetPacketBitrate();
		return 0;
	}
	//_printf("converted int32_t\n");
	return napi_get_value_int32(env, value, result);
}

napi_value* channels_object = {};
napi_value* ssrc_object = {};
uint64_t napi_get_value_uint32hook(napi_env* env, napi_value* value, uint32_t* result)
{
	if (value && value == (PVOID)channels_object)
	{
		_printf("channels spoofed\n");
		*result = 2;
		return 0;
	}
	if (value && value == (PVOID)ssrc_object)
	{
		uint64_t ret = napi_get_value_uint32(env, value, result);
		printf("found ssrc %lu\n", *result);
		return ret;
	}
	//_printf("converted uint32_t\n");
	return napi_get_value_uint32(env, value, result);
}

void* Token = nullptr;
napi_env* token_env = {};
napi_value* token_value = {};
napi_value* token_object = {};
uint64_t napi_get_value_string_utf8_hook(napi_env* env, napi_value* value, char* buffer, uint64_t bufsize, uint64_t* length)
{
	uint64_t result = napi_get_value_string_utf8(env, value, buffer, bufsize, length);
	if (value && value == token_value)
	{
		token_env = env;
		memcpy(Token, buffer, bufsize);
	}
	return result;
}

uint64_t napi_get_named_propertyhook(napi_env* env, napi_value* object, const char* name, napi_value** result)
{
	_printf("property: %s\n", name);
	if (!strcmp(name, "encodingVoiceBitRate")) // value is not defined in js env
	{
		*result = bitrate_object;
		return 0;
	}
	uint64_t Return = napi_get_named_property(env, object, name, result);
	/*
	if (!strcmp(name, "token"))
	{
		token_value = *result;
		token_object = object;
	}
	*/
	if (!strcmp(name, "channels")) channels_object = *result;
	if (!strcmp(name, "fec")) fec_object = *result;
	if (!strcmp(name, "packetLossRate")) packetloss_object = *result;
	if (!strcmp(name, "ssrc")) ssrc_object = *result;
	return Return;
}

napi_value* temp_value = {};
bool InitializeNodeApiHooks(HMODULE Discord)
{
	PVOID napi_get_named_property_address = GetProcAddress(Discord, "napi_get_named_property");
	if (!napi_get_named_property_address)
	{
		_printf("Failed to get napi_get_named_property\n");
		return false;
	}

	PVOID napi_get_value_uint32_address = GetProcAddress(Discord, "napi_get_value_uint32");
	if (!napi_get_value_uint32_address)
	{
		_printf("Failed to get napi_get_value_uint32\n");
		return false;
	}

	PVOID napi_get_value_int32_address = GetProcAddress(Discord, "napi_get_value_int32");
	if (!napi_get_value_int32_address)
	{
		_printf("Failed to get napi_get_value_int32\n");
		return false;
	}

	PVOID napi_get_value_bool_address = GetProcAddress(Discord, "napi_get_value_bool");
	if (!napi_get_value_bool_address)
	{
		_printf("Failed to get napi_get_value_bool\n");
		return false;
	}

	PVOID napi_get_value_double_address = GetProcAddress(Discord, "napi_get_value_double");
	if (!napi_get_value_double_address)
	{
		_printf("Failed to get napi_get_value_double\n");
		return false;
	}

	PVOID napi_get_value_string_utf8_address = GetProcAddress(Discord, "napi_get_value_string_utf8");
	if (!napi_get_value_string_utf8_address)
	{
		_printf("Failed to get napi_get_value_string_utf8\n");
		return false;
	}

	napi_set_named_property = (napi_set_named_property_t)GetProcAddress(Discord, "napi_set_named_property");
	if (!napi_set_named_property)
	{
		_printf("Failed to get napi_set_named_property\n");
		return false;
	}

	napi_create_string_utf8 = (napi_create_string_utf8_t)GetProcAddress(Discord, "napi_create_string_utf8");
	if (!napi_create_string_utf8)
	{
		_printf("Failed to get napi_create_string_utf8\n");
		return false;
	}

	bitrate_object = (napi_value*)VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!bitrate_object)
	{
		_printf("Failed to allocate bitrate_object\n");
		return false;
	}

	temp_value = (napi_value*)VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!temp_value)
	{
		_printf("Failed to allocate temporary storage\n");
		return false;
	}

	Token = VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!Token)
	{
		_printf("Failed to allocate for usage\n");
		return false;
	}

	if (MH_CreateHook(napi_get_named_property_address, napi_get_named_propertyhook, (void**)&napi_get_named_property) != MH_OK)
	{
		_printf("Failed to create hook at napi_get_named_property\n");
		return false;
	}

	if (MH_CreateHook(napi_get_value_uint32_address, napi_get_value_uint32hook, (void**)&napi_get_value_uint32) != MH_OK)
	{
		_printf("Failed to create hook at napi_get_value_uint32\n");
		return false;
	}

	if (MH_CreateHook(napi_get_value_int32_address, napi_get_value_int32hook, (void**)&napi_get_value_int32) != MH_OK)
	{
		_printf("Failed to create hook at napi_get_value_int32\n");
		return false;
	}

	if (MH_CreateHook(napi_get_value_bool_address, napi_get_value_boolhook, (void**)&napi_get_value_bool) != MH_OK)
	{
		_printf("Failed to create hook at napi_get_value_bool\n");
		return false;
	}

	if (MH_CreateHook(napi_get_value_double_address, napi_get_value_doublehook, (void**)&napi_get_value_double) != MH_OK)
	{
		_printf("Failed to create hook at napi_get_value_double\n");
		return false;
	}

	/*
	if (MH_CreateHook(napi_get_value_string_utf8_address, napi_get_value_string_utf8_hook, (void**)&napi_get_value_string_utf8) != MH_OK)
	{
		_printf("Failed to create hook at napi_get_value_string_utf8\n");
		return false;
	}
	*/

	return true;
}