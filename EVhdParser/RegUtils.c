#include "stdafx.h"
#include "RegUtils.h"

NTSTATUS Reg_GetDwordValue(_In_ HANDLE hKey, _In_ LPCWSTR pszValueName, _Out_ PULONG32 pValue)
{
	NTSTATUS Status = STATUS_SUCCESS;
	UNICODE_STRING ValueName;
	RtlInitUnicodeString(&ValueName, pszValueName);
	struct {
		KEY_VALUE_PARTIAL_INFORMATION Info;
		ULONG32 Extra;
	} Buffer;
	ULONG ResultLength;
	Status = ZwQueryValueKey(hKey, &ValueName, KeyValuePartialInformation, &Buffer, sizeof(Buffer), &ResultLength);
	if (NT_SUCCESS(Status))
	{
		if (REG_DWORD == Buffer.Info.Type) {
			ASSERT(Buffer.Info.DataLength == sizeof(ULONG32));
			*pValue = *(PULONG32)Buffer.Info.Data;
		}
		else
			Status = STATUS_INVALID_BUFFER_SIZE;
	}
	return Status;
}

NTSTATUS Reg_SetDwordValue(_In_ HANDLE hKey, _In_ LPCWSTR pszValueName, _In_ ULONG32 Value)
{
	UNICODE_STRING ValueName;
	RtlInitUnicodeString(&ValueName, pszValueName);
	return ZwSetValueKey(hKey, &ValueName, 0, REG_DWORD, &Value, sizeof(ULONG32));
}

NTSTATUS Reg_GetQwordValue(_In_ HANDLE hKey, _In_ LPCWSTR pszValueName, _Out_ PULONG64 pValue)
{
	NTSTATUS Status = STATUS_SUCCESS;
	UNICODE_STRING ValueName;
	RtlInitUnicodeString(&ValueName, pszValueName);
	struct {
		KEY_VALUE_PARTIAL_INFORMATION Info;
		ULONG64 Extra;
	} Buffer;
	ULONG ResultLength;
	Status = ZwQueryValueKey(hKey, &ValueName, KeyValuePartialInformation, &Buffer, sizeof(Buffer), &ResultLength);
	if (NT_SUCCESS(Status))
	{
		if (REG_QWORD == Buffer.Info.Type) {
			ASSERT(Buffer.Info.DataLength == sizeof(ULONG64));
			*pValue = *(PULONG64)Buffer.Info.Data;
		}
		else
			Status = STATUS_INVALID_BUFFER_SIZE;
	}
	return Status;
}

NTSTATUS Reg_SetQwordValue(_In_ HANDLE hKey, _In_ LPCWSTR pszValueName, _In_ ULONG64 Value)
{
	UNICODE_STRING ValueName;
	RtlInitUnicodeString(&ValueName, pszValueName);
	return ZwSetValueKey(hKey, &ValueName, 0, REG_QWORD, &Value, sizeof(ULONG64));
}

NTSTATUS Reg_GetGuidValue(_In_ HANDLE hKey, _In_ LPCWSTR pszValueName, _Out_ PGUID pValue)
{
	NTSTATUS Status = STATUS_SUCCESS;
	UNICODE_STRING ValueName;
	RtlInitUnicodeString(&ValueName, pszValueName);
	struct {
		KEY_VALUE_PARTIAL_INFORMATION Info;
		GUID Extra;
	} Buffer;
	ULONG ResultLength;
	Status = ZwQueryValueKey(hKey, &ValueName, KeyValuePartialInformation, &Buffer, sizeof(Buffer), &ResultLength);
	if (NT_SUCCESS(Status))
	{
		if (REG_BINARY == Buffer.Info.Type && sizeof(GUID) == Buffer.Info.DataLength) {
			RtlMoveMemory(pValue, Buffer.Info.Data, sizeof(GUID));
		}
		else
			Status = STATUS_INVALID_BUFFER_SIZE;
	}
	return Status;
}

NTSTATUS Reg_SetGuidValue(_In_ HANDLE hKey, _In_ LPCWSTR pszValueName, _In_ PGUID pValue)
{
	UNICODE_STRING ValueName;
	RtlInitUnicodeString(&ValueName, pszValueName);
	return ZwSetValueKey(hKey, &ValueName, 0, REG_BINARY, pValue, sizeof(GUID));
}

NTSTATUS Reg_GetStringValue(_In_ HANDLE hKey, _In_ LPCWSTR pszValueName, _Out_ PWSTR *ppValue)
{
	NTSTATUS Status = STATUS_SUCCESS;
	UNICODE_STRING ValueName;
	RtlInitUnicodeString(&ValueName, pszValueName);
	ULONG ResultLength = 0;
	Status = ZwQueryValueKey(hKey, &ValueName, KeyValuePartialInformation, NULL, 0, &ResultLength);
	if (STATUS_BUFFER_OVERFLOW == Status || STATUS_BUFFER_TOO_SMALL == Status)
	{
		KEY_VALUE_PARTIAL_INFORMATION *pBuffer = ExAllocatePool(PagedPool, ResultLength);
		if (pBuffer)
		{
			Status = ZwQueryValueKey(hKey, &ValueName, KeyValuePartialInformation, pBuffer, ResultLength, &ResultLength);
			if (NT_SUCCESS(Status) && pBuffer->Type == REG_SZ) {
				*ppValue = ExAllocatePool(PagedPool, pBuffer->DataLength);
				if (*ppValue)
					RtlMoveMemory(*ppValue, pBuffer->Data, pBuffer->DataLength);
				else
					Status = STATUS_INSUFFICIENT_RESOURCES;
			}
			ExFreePool(pBuffer);
		}
	}
	else if (NT_SUCCESS(Status)) {
		*ppValue = NULL;
	}

	return Status;
}
