#pragma once
#include <ntifs.h>

NTSTATUS SynchronouseCall(PFILE_OBJECT pFileObject, ULONG ulControlCode, PVOID InputBuffer,
	ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength);

/** Crates UNICODE_STRING copy */
NTSTATUS DuplicateUnicodeString(PCUNICODE_STRING Source, PUNICODE_STRING Destination);
/** Replaces forward slashes with backward */
NTSTATUS NormalizePath(PUNICODE_STRING Source);

#define DEBUG(Format, ...) DbgPrint(Format, __VA_ARGS__)
