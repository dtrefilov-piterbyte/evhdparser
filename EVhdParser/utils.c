#include "stdafx.h"
#include "utils.h"	

NTSTATUS SynchronouseCall(PFILE_OBJECT pFileObject, ULONG ulControlCode, PVOID InputBuffer,
	ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength)
{
	NTSTATUS status = STATUS_SUCCESS;
	IO_STATUS_BLOCK StatusBlock = { 0 };

	KEVENT Event;
	KeInitializeEvent(&Event, SynchronizationEvent, FALSE);
	PDEVICE_OBJECT pDeviceObject = IoGetRelatedDeviceObject(pFileObject);
	PIRP Irp = IoBuildDeviceIoControlRequest(ulControlCode, pDeviceObject, InputBuffer, InputBufferLength,
		OutputBuffer, OutputBufferLength, FALSE, &Event, &StatusBlock);
	IoGetNextIrpStackLocation(Irp)->FileObject = pFileObject;

	status = IoCallDriver(pDeviceObject, Irp);
	if (NT_SUCCESS(status))
		KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
	return status;
}

NTSTATUS DuplicateUnicodeString(PCUNICODE_STRING Source, PUNICODE_STRING Destination)
{
	Destination->Length = Source->Length;
	Destination->MaximumLength = Source->MaximumLength;
	Destination->Buffer = ExAllocatePool(NonPagedPoolNx, Destination->MaximumLength + sizeof(WCHAR));
	if (!Destination->Buffer)
		return STATUS_NO_MEMORY;
	RtlMoveMemory(Destination->Buffer, Source->Buffer, Source->Length);
	Destination->Buffer[Source->Length / sizeof(WCHAR)] = 0;
	return STATUS_SUCCESS;
}

NTSTATUS NormalizePath(PUNICODE_STRING Source)
{
	if (!Source)
		return STATUS_INVALID_PARAMETER;
	SIZE_T i = 0;
	for (; i < Source->Length / sizeof(WCHAR); ++i)
		if (L'/' == Source->Buffer[i]) Source->Buffer[i] = '\\';
	return STATUS_SUCCESS;
}
