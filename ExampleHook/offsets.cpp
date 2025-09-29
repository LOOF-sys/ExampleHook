#include <stdint.h>

uintptr_t opus_encode = 0x8AFCC0;
uintptr_t opus_encode_native = 0x8ABFB0;
uintptr_t HighPassFilter = 0x4B1C22;
uintptr_t opus_decode = 0x8B5740;
uintptr_t AudioCodingModuleImpl__RegisterTransportCallback = 0x68F740;
uintptr_t ChannelSend__SendRtpAudio = 0x4D3F68;
uintptr_t DequeuePacket = 0x8CB1C4;
uintptr_t PrioritizedPacketQueue_constructor = 0x8CB312;
uintptr_t Pop = 0x8CBBE6;

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
	uintptr_t AudioCodingModuleImpl__RegisterTransportCallback()
	{
		return ::AudioCodingModuleImpl__RegisterTransportCallback;
	}
	uintptr_t ChannelSend__SendRtpAudio()
	{
		return ::ChannelSend__SendRtpAudio;
	}
	uintptr_t Pop()
	{
		return ::Pop;
	}
	uintptr_t PrioritizedPacketQueue_constructor()
	{
		return ::PrioritizedPacketQueue_constructor;
	}
}