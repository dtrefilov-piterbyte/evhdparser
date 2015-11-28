#include "parser.h"	  
#include "Ioctl.h"
#include "utils.h"	   
#include <fltKernel.h>

static const ULONG EvhdPoolTag = 'VVpp';
static const ULONG EvhdQoSPoolTag = 'VVpc';

static const SIZE_T QoSBufferSize = 0x470;

extern PUNICODE_STRING g_pRegistryPath;

static NTSTATUS EvhdQueryBoolParameter(LPCWSTR lpszParameterName, BOOLEAN bDefaultValue, BOOLEAN *pResult)
{
	NTSTATUS status = STATUS_SUCCESS;
	HANDLE KeyHandle = NULL, SubKeyHandle = NULL;
	OBJECT_ATTRIBUTES ObjectAttributes = { 0 };
	UNICODE_STRING SubKeyName, ValueName;
	BOOLEAN Result = bDefaultValue;
	KEY_VALUE_PARTIAL_INFORMATION KeyValueInformation;
	ULONG ResultLength;

	ObjectAttributes.Length = sizeof(OBJECT_ATTRIBUTES);
	ObjectAttributes.ObjectName = g_pRegistryPath;
	ObjectAttributes.Attributes = OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE;

	status = ZwOpenKey(&KeyHandle, FILE_GENERIC_READ, &ObjectAttributes);
	if (NT_SUCCESS(status))
	{
		RtlInitUnicodeString(&SubKeyName, L"Parameters");
		ObjectAttributes.RootDirectory = KeyHandle;
		ObjectAttributes.ObjectName = &SubKeyName;
		status = ZwOpenKey(&SubKeyHandle, FILE_GENERIC_READ, &ObjectAttributes);
		if (NT_SUCCESS(status))
		{
			RtlInitUnicodeString(&ValueName, lpszParameterName);
			if (NT_SUCCESS(ZwQueryValueKey(SubKeyHandle, &ValueName, KeyValuePartialInformation, &KeyValueInformation,
				sizeof(KeyValueInformation), &ResultLength)))
			{
				if (ResultLength == sizeof(KeyValueInformation) &&
					REG_DWORD == KeyValueInformation.Type &&
					sizeof(ULONG32) == KeyValueInformation.DataLength)
				{
					Result = 0 != *(ULONG32 *)KeyValueInformation.Data;
				}
			}

			ZwClose(SubKeyHandle);
		}

		ZwClose(KeyHandle);
	}

	*pResult = Result;
	return status;
}

static NTSTATUS EvhdDirectIoControl(ParserInstance *parser, ULONG ControlCode, PVOID pInputBuffer, ULONG InputBufferSize)
{
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_OBJECT pDeviceObject = NULL;
	KeEnterCriticalRegion();
	FltAcquirePushLockExclusive(&parser->DirectIoPushLock);

	IoReuseIrp(parser->pDirectIoIrp, STATUS_PENDING);
	parser->pDirectIoIrp->Flags |= IRP_NOCACHE;
	parser->pDirectIoIrp->Tail.Overlay.Thread = (PETHREAD)__readgsqword(0x188);		// Pointer to calling thread control block
	parser->pDirectIoIrp->AssociatedIrp.SystemBuffer = pInputBuffer;				// IO buffer for buffered control code
	// fill stack frame parameters for synchronous IRP call
	PIO_STACK_LOCATION pStackFrame = IoGetNextIrpStackLocation(parser->pDirectIoIrp);
	pDeviceObject = IoGetRelatedDeviceObject(parser->pVhdmpFileObject);
	pStackFrame->FileObject = parser->pVhdmpFileObject;
	pStackFrame->DeviceObject = pDeviceObject;
	pStackFrame->Parameters.DeviceIoControl.IoControlCode = ControlCode;
	pStackFrame->Parameters.DeviceIoControl.InputBufferLength = InputBufferSize;
	pStackFrame->Parameters.DeviceIoControl.OutputBufferLength = 0;
	pStackFrame->MajorFunction = IRP_MJ_DEVICE_CONTROL;
	pStackFrame->MinorFunction = 0;
	pStackFrame->Flags = 0;
	pStackFrame->Control = 0;
	IoSynchronousCallDriver(pDeviceObject, parser->pDirectIoIrp);
	status = parser->pDirectIoIrp->IoStatus.Status;
	FltReleasePushLock(&parser->DirectIoPushLock);
	KeLeaveCriticalRegion();
	return status;
}

static NTSTATUS EvhdInitialize(HANDLE hFileHandle, PFILE_OBJECT pFileObject, ParserInstance *parser)
{
	NTSTATUS status = STATUS_SUCCESS;

	parser->pVhdmpFileObject = pFileObject;
	parser->FileHandle = hFileHandle;
	
	/* Initialize Direct IO */
	parser->pDirectIoIrp = IoAllocateIrp(IoGetRelatedDeviceObject(parser->pVhdmpFileObject)->StackSize, FALSE);
	if (!parser->pDirectIoIrp)
	{
		DEBUG("IoAllocateIrp failed\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	
	/* Initialize QoS */
	parser->pQoSStatusIrp = IoAllocateIrp(IoGetRelatedDeviceObject(parser->pVhdmpFileObject)->StackSize, FALSE);
	if (!parser->pQoSStatusIrp)
	{
		DEBUG("IoAllocateIrp failed\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	parser->pQoSStatusBuffer = ExAllocatePoolWithTag(NonPagedPoolNx, QoSBufferSize, EvhdQoSPoolTag);
	if (!parser->pQoSStatusBuffer)
	{
		DEBUG("ExAllocatePoolWithTag failed\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	/* Initialize CTL */
	ExInitializeRundownProtection(&parser->RecoveryRundownProtection);
	parser->pRecoveryStatusIrp = IoAllocateIrp(IoGetRelatedDeviceObject(parser->pVhdmpFileObject)->StackSize, FALSE);
	if (!parser->pRecoveryStatusIrp)
	{
		DEBUG("IoAllocateIrp failed\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	parser->FileHandle = hFileHandle;
	parser->pVhdmpFileObject = pFileObject;

	return status;
}

static VOID EvhdFinalize(ParserInstance *parser)
{
	if (parser->pRecoveryStatusIrp)
	{
		IoCancelIrp(parser->pRecoveryStatusIrp);
		ExWaitForRundownProtectionRelease(&parser->RecoveryRundownProtection);
		IoFreeIrp(parser->pRecoveryStatusIrp);
		parser->pRecoveryStatusIrp = NULL;
	}

	if (parser->pDirectIoIrp)
	{
		IoFreeIrp(parser->pDirectIoIrp);
		parser->pDirectIoIrp = NULL;
	}

	if (parser->pQoSStatusIrp)
	{
		IoFreeIrp(parser->pQoSStatusIrp);
		parser->pQoSStatusIrp = NULL;
	}

	if (parser->pQoSStatusBuffer)
	{
		ExFreePoolWithTag(parser->pQoSStatusBuffer, EvhdQoSPoolTag);
		parser->pQoSStatusBuffer = NULL;
	}

	if (parser->pVhdmpFileObject)
	{
		ObDereferenceObject(parser->pVhdmpFileObject);
		parser->pVhdmpFileObject = NULL;
	}

	if (parser->FileHandle)
	{
		ZwClose(parser->FileHandle);
		parser->FileHandle = NULL;
	}
}

static NTSTATUS EvhdUnregisterIo(ParserInstance *parser)
{
	NTSTATUS status = STATUS_SUCCESS;

	if (parser->bMounted)
	{
#pragma pack(push, 1)
		struct {
			INT dwVersion;
			INT dwUnk1;
			INT dwUnk2;
		} RemoveVhdRequest = { 1 };
#pragma pack(pop)

		EvhdDirectIoControl(parser, IOCTL_STORAGE_REMOVE_VIRTUAL_DISK, &RemoveVhdRequest, sizeof(RemoveVhdRequest));

		parser->bMounted = FALSE;
		parser->IoInfo.pIoInterface = NULL;
		parser->IoInfo.pfnStartIo = NULL;
		parser->IoInfo.pfnSaveData = NULL;
		parser->IoInfo.pfnRestoreData = NULL;
		parser->dwDiskSaveSize = 0;
		parser->dwInnerBufferSize = 0;
	}

	return status;
}

NTSTATUS EVhdOpenDisk(PCUNICODE_STRING diskPath, ULONG32 OpenFlags, GUID *pVmId, PVOID vstorInterface, __out ParserInstance **ppOutParser)
{
	NTSTATUS status = STATUS_SUCCESS;
	PFILE_OBJECT pFileObject = NULL;
	HANDLE FileHandle = NULL;
	ParserInstance *parser = NULL;
	ResiliencyInfoEa vmInfo = { 0 };

	if (pVmId)
	{
		vmInfo.NextEntryOffset = 0;
		vmInfo.EaValueLength = sizeof(GUID);
		vmInfo.EaNameLength = sizeof(vmInfo.EaName) - 1;
		strncpy(vmInfo.EaName, OPEN_FILE_RESILIENCY_INFO_EA_NAME, sizeof(vmInfo.EaName));
		vmInfo.EaValue = *pVmId;
	}
	status = OpenVhdmpDevice(&FileHandle, OpenFlags, &pFileObject, diskPath, pVmId ? &vmInfo : NULL);
	if (!NT_SUCCESS(status))
	{
		DEBUG("Failed to open vhdmp device for virtual disk file %S\n", diskPath->Buffer);
		goto failure_cleanup;
	}

	parser = (ParserInstance *)ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(ParserInstance), EvhdPoolTag);
	if (!parser)
	{
		DEBUG("Failed to allocate memory for ParserInstance\n");
		status = STATUS_NO_MEMORY;
		goto failure_cleanup;
	}

	memset(parser, 0, sizeof(ParserInstance));

	status = EvhdInitialize(FileHandle, pFileObject, parser);

	if (!NT_SUCCESS(status))
		goto failure_cleanup;

	parser->wUnkIoFlags = OpenFlags & 0x40 ? 4 : OpenFlags & 1;
	EvhdQueryBoolParameter(L"FastPause", TRUE, &parser->bFastPause);
	EvhdQueryBoolParameter(L"FastClose", TRUE, &parser->bFastClose);
	parser->pVstorInterface = vstorInterface;

	*ppOutParser = parser;

	goto cleanup;

failure_cleanup:
	if (FileHandle)
	{
		ZwClose(FileHandle);
		parser->FileHandle = NULL;
	}
	if (parser)
	{
		EVhdCloseDisk(parser);
	}

cleanup:
	return status;
}

VOID EVhdCloseDisk(ParserInstance *parser)
{
	if (parser->bIoRegistered)
	{
		EvhdUnregisterIo(parser);
		parser->bIoRegistered = FALSE;
	}
	EvhdFinalize(parser);
	ExFreePoolWithTag(parser, EvhdPoolTag);
}
NTSTATUS EVhdMountDisk(ParserInstance *parser, UCHAR flags, PGUID pUnkGuid, __out MountInfo *mountInfo)
{
	UNREFERENCED_PARAMETER(parser);
	UNREFERENCED_PARAMETER(flags);
	UNREFERENCED_PARAMETER(pUnkGuid);
	UNREFERENCED_PARAMETER(mountInfo);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdDismountDisk(ParserInstance *parser)
{
	UNREFERENCED_PARAMETER(parser);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdQueryMountStatusDisk(ParserInstance *parser, /* TODO */ ULONG32 unk)
{
	UNREFERENCED_PARAMETER(parser);
	UNREFERENCED_PARAMETER(unk);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdExecuteScsiRequestDisk(ParserInstance *parser, ScsiPacket *pPacket)
{
	UNREFERENCED_PARAMETER(parser);
	UNREFERENCED_PARAMETER(pPacket);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdQueryInformationDisk(ParserInstance *parser, EDiskInfoType type, INT unused1, INT unused2, PVOID pBuffer, INT *pBufferSize)
{
	UNREFERENCED_PARAMETER(parser);
	UNREFERENCED_PARAMETER(type);
	UNREFERENCED_PARAMETER(unused1);
	UNREFERENCED_PARAMETER(unused2);
	UNREFERENCED_PARAMETER(pBuffer);
	UNREFERENCED_PARAMETER(pBufferSize);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdQuerySaveVersionDisk(ParserInstance *parser, INT *pVersion)
{
	UNREFERENCED_PARAMETER(parser);
	UNREFERENCED_PARAMETER(pVersion);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdSaveDisk(ParserInstance *parser, PVOID data, ULONG32 size, ULONG32 *dataStored)
{
	UNREFERENCED_PARAMETER(parser);
	UNREFERENCED_PARAMETER(data);
	UNREFERENCED_PARAMETER(size);
	UNREFERENCED_PARAMETER(dataStored);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdRestoreDisk(ParserInstance *parser, INT revision, PVOID data, ULONG32 size)
{
	UNREFERENCED_PARAMETER(parser);
	UNREFERENCED_PARAMETER(revision);
	UNREFERENCED_PARAMETER(data);
	UNREFERENCED_PARAMETER(size);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdSetBehaviourDisk(ParserInstance *parser, INT behaviour)
{
	UNREFERENCED_PARAMETER(parser);
	UNREFERENCED_PARAMETER(behaviour);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdSetQosConfigurationDisk(ParserInstance *parser, PVOID pConfig)
{
	UNREFERENCED_PARAMETER(parser);
	UNREFERENCED_PARAMETER(pConfig);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdGetQosInformationDisk(ParserInstance *parser, PVOID pInfo)
{
	UNREFERENCED_PARAMETER(parser);
	UNREFERENCED_PARAMETER(pInfo);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdChangeTrackingGetParameters(ParserInstance *parser, CTParameters *pParams)
{
	UNREFERENCED_PARAMETER(parser);
	UNREFERENCED_PARAMETER(pParams);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdChangeTrackingStart(ParserInstance *parser, CTEnableParam *pParams)
{
	UNREFERENCED_PARAMETER(parser);
	UNREFERENCED_PARAMETER(pParams);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdChangeTrackingStop(ParserInstance *parser, const ULONG32 *dwBytesTransferred, ULONG32 bForce)
{
	UNREFERENCED_PARAMETER(parser);
	UNREFERENCED_PARAMETER(dwBytesTransferred);
	UNREFERENCED_PARAMETER(bForce);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdChangeTrackingSwitchLogs(ParserInstance *parser, CTSwitchLogParam *pParams)
{
	UNREFERENCED_PARAMETER(parser);
	UNREFERENCED_PARAMETER(pParams);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdNotifyRecoveryStatus(ParserInstance *parser, RecoveryStatusCompletionRoutine pfnCompletionCb, void *pInterface)
{
	UNREFERENCED_PARAMETER(parser);
	UNREFERENCED_PARAMETER(pfnCompletionCb);
	UNREFERENCED_PARAMETER(pInterface);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdGetRecoveryStatus(ParserInstance *parser, ULONG32 *pStatus)
{
	UNREFERENCED_PARAMETER(parser);
	UNREFERENCED_PARAMETER(pStatus);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdPrepareMetaOperation(ParserInstance *parser, void *pMetaOperationBuffer, MetaOperationCompletionRoutine pfnCompletionCb, void *pInterface, MetaOperation **ppOperation)
{
	UNREFERENCED_PARAMETER(parser);
	UNREFERENCED_PARAMETER(pMetaOperationBuffer);
	UNREFERENCED_PARAMETER(pfnCompletionCb);
	UNREFERENCED_PARAMETER(pInterface);
	UNREFERENCED_PARAMETER(ppOperation);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdStartMetaOperation(MetaOperation *pOperation)
{
	UNREFERENCED_PARAMETER(pOperation);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdCancelMetaOperation(MetaOperation *pOperation)
{
	UNREFERENCED_PARAMETER(pOperation);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdQueryMetaOperationProgress(MetaOperation *pOperation)
{
	UNREFERENCED_PARAMETER(pOperation);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdCleanupMetaOperation(MetaOperation *pOperation)
{
	UNREFERENCED_PARAMETER(pOperation);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdParserDeleteSnapshot(ParserInstance *parser, void *pInputBuffer /* TODO:sizeof=0x18 */)
{
	UNREFERENCED_PARAMETER(parser);
	UNREFERENCED_PARAMETER(pInputBuffer);
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdParserQueryChanges(ParserInstance *parser, void *pInputBuffer, ULONG32 dwInputBufferLength)
{
	UNREFERENCED_PARAMETER(parser);
	UNREFERENCED_PARAMETER(pInputBuffer);
	UNREFERENCED_PARAMETER(dwInputBufferLength);
	return STATUS_NOT_SUPPORTED;
}
