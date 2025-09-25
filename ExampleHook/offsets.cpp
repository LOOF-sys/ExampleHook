#include <stdint.h>

uintptr_t opus_encode = 0x8AFCC0;
uintptr_t opus_encode_native = 0x8ABFB0;
uintptr_t HighPassFilter = 0x4B1C22;
uintptr_t opus_decode = 0x8B5740;

namespace offsets
{
	uintptr_t opus_encode()
	{
		return ::opus_encode;
	}
	uintptr_t opus_encode_native()
	{
		return ::opus_encode_native;
	}
	uintptr_t HighPassFilter()
	{
		return ::HighPassFilter;
	}
	uintptr_t opus_decode()
	{
		return ::opus_decode;
	}
}