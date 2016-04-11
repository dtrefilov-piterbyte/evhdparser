#include "stdafx.h"
#include "Log.h"
#include "RegUtils.h"
#include "utils.h"

// 5 MB
#define MINIMUM_ROTATION_SIZE 5242880

const ULONG LogAllocationTag = 'VLOG';
static PDEVICE_OBJECT LogDeviceObject = NULL;
static HANDLE LogParametersKey = NULL;
static HANDLE LogFile = NULL;
LOG_SETTINGS LogSettings = {
	.LogLevel = LL_MAX,
	.LogCategories = LOG_CTG_DEFAULT,
	.MaxFileSize = MINIMUM_ROTATION_SIZE,
	.MaxKeptRotatedFiles = 5
};
static PWSTR LogFileName = NULL;
static volatile LONG LogScheduledPrints = 0;
static volatile LONG LogRotationInProgress = 0;

#define MAX_LOG_STRING_SIZE 1024

typedef struct _LOG_COUNTED_STRING {
	_Field_range_(0, (MAX_LOG_STRING_SIZE))
	USHORT DataLength;	// in bytes
	CHAR Data[MAX_LOG_STRING_SIZE];
} LOG_COUNTED_STRING, *PLOG_COUNTED_STRING;

NTSTATUS Log_StartFileLogging(LPCWSTR pszFileName)
{
	OBJECT_ATTRIBUTES fAttrs;
	UNICODE_STRING FileName;
	NTSTATUS Status = STATUS_SUCCESS;
	IO_STATUS_BLOCK StatusBlock = { 0 };

	RtlInitUnicodeString(&FileName, pszFileName);
	InitializeObjectAttributes(&fAttrs, &FileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
	Status = ZwCreateFile(&LogFile, FILE_APPEND_DATA | SYNCHRONIZE, &fAttrs,
		&StatusBlock, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_DELETE, FILE_OPEN_IF,
		FILE_SYNCHRONOUS_IO_NONALERT | FILE_SEQUENTIAL_ONLY, NULL, 0);

	return Status;
}

NTSTATUS Log_CompressFile(HANDLE FileHandle)
{
	NTSTATUS Status = STATUS_SUCCESS;
	IO_STATUS_BLOCK StatusBlock = { 0 };
	USHORT InputBuffer = COMPRESSION_FORMAT_DEFAULT;

	Status = ZwFsControlFile(FileHandle, NULL, NULL, NULL, &StatusBlock, FSCTL_SET_COMPRESSION,
		&InputBuffer, sizeof(InputBuffer), NULL, 0);
	return Status;
}

NTSTATUS Log_Rotate()
{
	NTSTATUS Status = STATUS_SUCCESS;
	HANDLE hFile = NULL;
	OBJECT_ATTRIBUTES fAttrs;
	UNICODE_STRING FileName;
	IO_STATUS_BLOCK StatusBlock = { 0 };
	ULONG RotateIndex = 0;
	FILE_RENAME_INFORMATION *RenameBuffer = NULL;
	LPCWSTR BaseFileName = LogFileName;
	ULONG BaseFileNameLength = 0;

	if (!LogFileName || !LogFile)
		return STATUS_INVALID_DEVICE_STATE;

	LPWSTR End1 = wcsrchr(LogFileName, L'\\'), End2 = wcsrchr(LogFileName, L'/');
	if ((SIZE_T)End1 > (SIZE_T)End2 > 0)
		BaseFileName = End1 ? End1 + 1 : LogFileName;
	else
		BaseFileName = End2 ? End2 + 1 : LogFileName;
	BaseFileNameLength = (ULONG)(wcslen(BaseFileName) + 4) * sizeof(WCHAR);

	ULONG FileNameLength = (ULONG)(wcslen(LogFileName) + 5) * sizeof(WCHAR);
	RenameBuffer = ExAllocatePoolWithTag(PagedPool, FileNameLength + FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName),
		LogAllocationTag);
	if (!RenameBuffer) {
		Status = STATUS_INSUFFICIENT_RESOURCES;
		goto Cleanup;
	}
	RenameBuffer->FileNameLength = BaseFileNameLength;
	RenameBuffer->ReplaceIfExists = FALSE;
	RenameBuffer->RootDirectory = NULL;

	// Rename already rotated files first
	Status = StringCbPrintf(RenameBuffer->FileName, FileNameLength, L"%ws.%03d", LogFileName, LogSettings.MaxKeptRotatedFiles);
	if (!SUCCEEDED(Status))
		goto Cleanup;
	RtlInitUnicodeString(&FileName, RenameBuffer->FileName);
	InitializeObjectAttributes(&fAttrs, &FileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
	ZwDeleteFile(&fAttrs);

	for (RotateIndex = LogSettings.MaxKeptRotatedFiles - 1; RotateIndex > 0; RotateIndex--)
	{
		Status = StringCbPrintf(RenameBuffer->FileName, FileNameLength, L"%ws.%03d", LogFileName, RotateIndex);
		if (!SUCCEEDED(Status))
			goto Cleanup;
		RtlInitUnicodeString(&FileName, RenameBuffer->FileName);
		InitializeObjectAttributes(&fAttrs, &FileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
		Status = ZwOpenFile(&hFile, DELETE, &fAttrs, &StatusBlock, 0, 0);
		if (!NT_SUCCESS(Status))
			continue;

		Status = StringCbPrintf(RenameBuffer->FileName, FileNameLength, L"%ws.%03d", BaseFileName, RotateIndex + 1);
		if (!SUCCEEDED(Status))
			goto Cleanup;
		Status = ZwSetInformationFile(hFile, &StatusBlock, RenameBuffer, FileNameLength + FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName),
			FileRenameInformation);
		if (!NT_SUCCESS(Status))
			goto Cleanup;

		ZwClose(hFile);
		hFile = NULL;
	}

	// Rename it
	Status = StringCbPrintf(RenameBuffer->FileName, FileNameLength, L"%ws.%03d", BaseFileName, 1);
	if (!SUCCEEDED(Status))
		goto Cleanup;

	// Compress
	Status = Log_CompressFile(LogFile);

	Status = ZwSetInformationFile(LogFile, &StatusBlock, RenameBuffer, FileNameLength + FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName),
		FileRenameInformation);
	if (!NT_SUCCESS(Status))
		goto Cleanup;

	// And start logging into the new file
	ZwClose(LogFile);
	LogFile = NULL;
	Status = Log_StartFileLogging(LogFileName);

Cleanup:
	if (hFile)
		ZwClose(hFile);
	if (RenameBuffer)
		ExFreePoolWithTag(RenameBuffer, LogAllocationTag);
	
	return Status;
}

NTSTATUS Log_WriteLine(PLOG_COUNTED_STRING Line)
{
	NTSTATUS Status = STATUS_SUCCESS;
	IO_STATUS_BLOCK StatusBlock;
	FILE_STANDARD_INFORMATION Info;

	if (!LogFile)
		return STATUS_INVALID_HANDLE;

	if (!InterlockedCompareExchange(&LogRotationInProgress, 1, 0))
	{
		Status = ZwQueryInformationFile(LogFile, &StatusBlock, &Info, sizeof(Info), FileStandardInformation);
		if (!NT_SUCCESS(Status)) {
			return Status;
		}

		if (Info.DeletePending && LogFileName) {
			ZwClose(LogFile);
			LogFile = NULL;
			Status = Log_StartFileLogging(LogFileName);
			if (!NT_SUCCESS(Status))
				return Status;
		}
		else if (Info.EndOfFile.QuadPart >= LogSettings.MaxFileSize) {
			Status = Log_Rotate();
		}

		LogRotationInProgress = 0;
	}

	if (NT_SUCCESS(Status) && LogFile)
		Status = ZwWriteFile(LogFile, NULL, NULL, NULL, &StatusBlock, Line->Data, Line->DataLength, NULL, NULL);

	return Status;
}

NTSTATUS Log_Initialize(PDEVICE_OBJECT DeviceObject, PUNICODE_STRING RegistryPath)
{
	NTSTATUS Status = STATUS_SUCCESS;
	HANDLE hKey = NULL;
	OBJECT_ATTRIBUTES fAttrs;
	UNICODE_STRING SubkeyName;

	LogDeviceObject = DeviceObject;
	ObReferenceObject(LogDeviceObject);

	InitializeObjectAttributes(&fAttrs, RegistryPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
	Status = ZwOpenKey(&hKey, KEY_CREATE_SUB_KEY | KEY_SET_VALUE | KEY_WRITE | KEY_QUERY_VALUE | KEY_READ, &fAttrs);
	if (!NT_SUCCESS(Status))
		return Status;

	RtlInitUnicodeString(&SubkeyName, L"Logging");
	InitializeObjectAttributes(&fAttrs, &SubkeyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, hKey, NULL);
	Status = ZwCreateKey(&LogParametersKey, GENERIC_WRITE | GENERIC_READ, &fAttrs, 0, NULL, REG_OPTION_NON_VOLATILE, NULL);
	if (NT_SUCCESS(Status))
	{
		Reg_GetDwordValue(LogParametersKey, L"LogLevel", &LogSettings.LogLevel);
		Reg_GetDwordValue(LogParametersKey, L"LogCategoryMask", &LogSettings.LogCategories);
		Reg_GetDwordValue(LogParametersKey, L"MaxFileSize", &LogSettings.MaxFileSize);
		Reg_GetDwordValue(LogParametersKey, L"MaxKeptRotatedFiles", &LogSettings.MaxKeptRotatedFiles);
		Reg_GetStringValue(LogParametersKey, L"LogFileName", &LogFileName);
	}

	if (LogSettings.MaxFileSize < MINIMUM_ROTATION_SIZE)
		LogSettings.MaxFileSize = MINIMUM_ROTATION_SIZE;

	if (LogFileName)
	{
		Status = Log_StartFileLogging(LogFileName);
	}

	ZwClose(hKey);

	return Status;
}

VOID Log_Cleanup()
{
	// wait for all pending prints to finish
	LARGE_INTEGER delay;
	delay.QuadPart = -100000;	// 10 ms
	while (LogScheduledPrints) {
		KeDelayExecutionThread(KernelMode, TRUE, &delay);
	}

	if (LogFile) {
		ZwClose(LogFile);
		LogFile = NULL;
	}

	if (LogFileName) {
		ExFreePool(LogFileName);
		LogFileName = NULL;
	}

	if (LogDeviceObject)
	{
		ObDereferenceObject(LogDeviceObject);
		LogDeviceObject = NULL;
	}
}

VOID Log_WriteWorker(_In_ PDEVICE_OBJECT DeviceObject, _In_opt_ PVOID Context, _In_ PIO_WORKITEM IoWorkItem)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	PLOG_COUNTED_STRING line = Context;
	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	Log_WriteLine(line);

	InterlockedDecrement(&LogScheduledPrints);

	IoFreeWorkItem(IoWorkItem);
	ExFreePoolWithTag(Context, LogAllocationTag);
}

NTSTATUS Log_Print(LOG_LEVEL Level, LPCSTR pszFormat, ...)
{
	NTSTATUS Status = STATUS_SUCCESS;
	LARGE_INTEGER SystemTime, LocalTime;
	TIME_FIELDS TimeFields;
	PLOG_COUNTED_STRING Line = NULL;
	va_list args;
	KIRQL Irql = KeGetCurrentIrql();
	LPCSTR StrLevel = "       :";

	if (!LogFile)
		return STATUS_INVALID_DEVICE_STATE;

	if (Irql > DISPATCH_LEVEL)
		return STATUS_INVALID_LEVEL;

	KeQuerySystemTime(&SystemTime);
	ExSystemTimeToLocalTime(&SystemTime, &LocalTime);
	RtlTimeToTimeFields(&LocalTime, &TimeFields);

	Line = ExAllocatePoolWithTag(NonPagedPool, MAX_LOG_STRING_SIZE + FIELD_OFFSET(LOG_COUNTED_STRING, Data), LogAllocationTag);
	if (!Line)
		return STATUS_INSUFFICIENT_RESOURCES;

	switch (Level)
	{
	case LL_FATAL:
#if DBG
		DbgBreakPointWithStatus(DBG_STATUS_FATAL);
#endif
		StrLevel = "FATAL  :";
		break;
	case LL_ERROR:
		StrLevel = "ERROR  :";
		break;
	case LL_WARNING:
		StrLevel = "WARNING:";
		break;
	case LL_INFO:
		StrLevel = "INFO   :";
		break;
	case LL_VERBOSE:
		StrLevel = "VERBOSE:";
		break;
	case LL_DEBUG:
		StrLevel = "DEBUG  :";
		break;
	}

	Status = StringCbPrintfA(Line->Data, MAX_LOG_STRING_SIZE, "%04u.%02u.%02u %02u:%02u:%02u.%03u PR:0x%04X TH:0x%04X IL:%d %s ",
		TimeFields.Year,
		TimeFields.Month,
		TimeFields.Day,
		TimeFields.Hour,
		TimeFields.Minute,
		TimeFields.Second,
		TimeFields.Milliseconds,
		(ULONG)PsGetCurrentProcessId() & 0xFFFF,
		(ULONG)PsGetCurrentThreadId() & 0xFFFF,
		(ULONG)Irql,
		StrLevel);

	if (SUCCEEDED(Status))
	{
		va_start(args, pszFormat);
		Line->DataLength = (USHORT)strlen(Line->Data);
		Status = StringCbVPrintfA(Line->Data + Line->DataLength, MAX_LOG_STRING_SIZE - Line->DataLength, pszFormat, args);
		if (SUCCEEDED(Status))
		{
			Line->DataLength = (USHORT)strlen(Line->Data);
			if (Irql != PASSIVE_LEVEL) {
				PIO_WORKITEM pWorkItem = IoAllocateWorkItem(LogDeviceObject);
				InterlockedIncrement(&LogScheduledPrints);
				IoQueueWorkItemEx(pWorkItem, Log_WriteWorker, DelayedWorkQueue, Line);
				Status = STATUS_PENDING;
			}
			else
			{
				Status = Log_WriteLine(Line);
			}
		}

		va_end(args);
	}

	if (Status != STATUS_PENDING)
		ExFreePoolWithTag(Line, LogAllocationTag);

	return Status;
}

NTSTATUS Log_SetSetting(_In_ LOG_SETTINGS *Settings)
{
	if (Settings->MaxFileSize < MINIMUM_ROTATION_SIZE)
		return STATUS_INVALID_PARAMETER;
	LogSettings.LogCategories = Settings->LogCategories;
	LogSettings.LogLevel = Settings->LogLevel;
	LogSettings.MaxFileSize = Settings->MaxFileSize;
	LogSettings.MaxKeptRotatedFiles = Settings->MaxKeptRotatedFiles;

	return STATUS_SUCCESS;
}

NTSTATUS Log_QueryLogSettings(_Out_ LOG_SETTINGS *Settings)
{
	Settings->LogCategories = LogSettings.LogCategories;
	Settings->LogLevel = LogSettings.LogLevel;
	Settings->MaxFileSize = LogSettings.MaxFileSize;
	Settings->MaxKeptRotatedFiles = LogSettings.MaxKeptRotatedFiles;

	return STATUS_SUCCESS;
}
