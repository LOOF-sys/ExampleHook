#include <ntifs.h>
#include <ntddk.h>

// ill add this driver for injecting and masking the entire hook and traces way later (and maybe network diversion based)
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegisteryPath)
{
	return STATUS_SUCCESS;
}