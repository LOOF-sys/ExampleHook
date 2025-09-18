int stdmemcmp(const void* Memory1, const void* Memory2, unsigned long long length)
{
	auto _Memory1 = (char*)Memory1;
	auto _Memory2 = (char*)Memory2;
	for (unsigned long long i = 0; i < length; i++)
	{
		if (_Memory1[i] != _Memory2[i])
		{
			if (_Memory1[i] > _Memory2[i]) return 1;
			if (_Memory1[i] < _Memory2[i]) return -1;
			return 2; // idk
		}
	}
	return 0;
}

char* stdstrstr(const void* Memory1, const void* Memory2, unsigned long long length, unsigned long long segment_length)
{
	if (segment_length > length) return {};
	auto _Memory1 = (char*)Memory1;
	auto _Memory2 = (char*)Memory2;
	for (unsigned long long i = 0; i < (length - (segment_length - 1)); i++)
	{
		for (unsigned long long ii = 0; ii < segment_length; ii++)
		{
			if (!stdmemcmp(_Memory1 + ii + i, _Memory2, segment_length))
			{
				return _Memory1 + ii + i;
			}
		}
	}
	return 0;
}

unsigned long wstrlen(const wchar_t* Memory1)
{
	for (unsigned long i = 0; i < 0xfffffffe; i++) if (!Memory1[i]) return i - 1;
	return 0xffffffff;
}

wchar_t* wstdstrstr(const void* Memory1, const void* Memory2, unsigned long long length, unsigned long long segment_length)
{
	if (segment_length > length) return {};
	auto _Memory1 = (wchar_t*)Memory1;
	auto _Memory2 = (wchar_t*)Memory2;
	for (unsigned long long i = 0; i < (length - (segment_length - 1)); i++)
	{
		for (unsigned long long ii = 0; ii < segment_length; ii++)
		{
			if (!stdmemcmp(_Memory1 + ii + i, _Memory2, segment_length))
			{
				return _Memory1 + ii + i;
			}
		}
	}
	return 0;
}