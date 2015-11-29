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

static NTSTATUS EvhdQosStatusControl(ParserInstance *parser, ULONG ControlCode, ULONG InputBufferSize, ULONG OutputBufferSize,
	PIO_COMPLETION_ROUTINE pfnCompletionRoutine)
{
	PDEVICE_OBJECT pDeviceObject = NULL;

	IoReuseIrp(parser->pQoSStatusIrp, STATUS_PENDING);
	parser->pQoSStatusIrp->Tail.Overlay.Thread = (PETHREAD)__readgsqword(0x188);				// Pointer to calling thread control block
	parser->pQoSStatusIrp->AssociatedIrp.SystemBuffer = parser->pQoSStatusBuffer;				// IO buffer for buffered control code
	PIO_STACK_LOCATION pStackFrame = IoGetNextIrpStackLocation(parser->pQoSStatusIrp);
	pDeviceObject = IoGetRelatedDeviceObject(parser->pVhdmpFileObject);
	pStackFrame->FileObject = parser->pVhdmpFileObject;
	pStackFrame->DeviceObject = pDeviceObject;
	pStackFrame->Parameters.DeviceIoControl.IoControlCode = ControlCode;
	pStackFrame->Parameters.DeviceIoControl.InputBufferLength = InputBufferSize;
	pStackFrame->Parameters.DeviceIoControl.OutputBufferLength = OutputBufferSize;
	pStackFrame->MajorFunction = IRP_MJ_DEVICE_CONTROL;
	pStackFrame->MinorFunction = 0;
	pStackFrame->Flags = 0;
	pStackFrame->Control = SL_INVOKE_ON_CANCEL | SL_INVOKE_ON_SUCCESS | SL_INVOKE_ON_ERROR;
	pStackFrame->Context = parser;
	pStackFrame->CompletionRoutine = pfnCompletionRoutine;
	IoCallDriver(pDeviceObject, parser->pQoSStatusIrp);
	return STATUS_PENDING;
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

static void EvhdPostProcessScsiPacket(ScsiPacket *pPacket, NTSTATUS status)
{
	pPacket->pRequest->SrbStatus = pPacket->pInner->Srb.SrbStatus;
	pPacket->pRequest->ScsiStatus = pPacket->pInner->Srb.ScsiStatus;
	pPacket->pRequest->SenseInfoBufferLength = pPacket->pInner->Srb.SenseInfoBufferLength;
	pPacket->pRequest->DataTransferLength = pPacket->pInner->Srb.DataTransferLength;
	if (!NT_SUCCESS(status))
	{
		if (SRB_STATUS_SUCCESS != pPacket->pInner->Srb.SrbStatus)
			pPacket->pRequest->SrbStatus = SRB_STATUS_ERROR;
	}
	else
	{
		switch (pPacket->pInner->Srb.Cdb[0])
		{
		case SCSI_OP_CODE_INQUIRY:
			pPacket->pRequest->bFlags |= 1;
			break;
		}
	}
}

NTSTATUS EvhdCompleteScsiRequest(ScsiPacket *pPacket, NTSTATUS status)
{
	EvhdPostProcessScsiPacket(pPacket, status);
	return VstorCompleteScsiRequest(pPacket);
}

NTSTATUS EvhdSendNotification(void *param1, INT param2)
{
	return VstorSendNotification(param1, param2);
}

NTSTATUS EvhdSendMediaNotification(void *param1)
{
	return VstorSendMediaNotification(param1);
}

static NTSTATUS EvhdRegisterIo(ParserInstance *parser, BOOLEAN flag1, BOOLEAN flag2, PGUID pUnkGuid)
{
	NTSTATUS status = STATUS_SUCCESS;

	if (!parser->bIoRegistered)
	{
		RegisterIoRequest request = { 0 };
		RegisterIoResponse response = { 0 };
		SCSI_ADDRESS scsiAddressResponse = { 0 };

		request.dwVersion = 1;
		request.dwFlags = flag1 ? 9 : 8;
		if (flag2) request.wFlags |= 0x1;
		request.unkGuid = *pUnkGuid;
		request.pfnCompleteScsiRequest = &EvhdCompleteScsiRequest;
		request.pfnSendMediaNotification = &EvhdSendMediaNotification;
		request.pfnSendNotification = &EvhdSendNotification;
		request.pVstorInterface = parser->pVstorInterface;
		request.wMountFlags = parser->wMountFlags;

		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_REGISTER_IO, &request, sizeof(RegisterIoRequest),
			&response, sizeof(RegisterIoResponse));

		if (!NT_SUCCESS(status))
		{
			DEBUG("IOCTL_STORAGE_REGISTER_IO failed with error 0x%0X\n", status);
			return status;
		}

		parser->bIoRegistered = TRUE;
		parser->dwDiskSaveSize = response.dwDiskSaveSize;
		parser->dwInnerBufferSize = sizeof(ScsiPacketInnerRequest) + response.dwExtensionBufferSize;
		parser->Io = response.Io;

		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_SCSI_GET_ADDRESS, NULL, 0, &scsiAddressResponse, sizeof(SCSI_ADDRESS));

		if (!NT_SUCCESS(status))
		{
			DEBUG("IOCTL_SCSI_GET_ADDRESS failed with error 0x%0X\n", status);
			return status;
		}

		parser->ScsiLun = scsiAddressResponse.Lun;
		parser->ScsiPathId = scsiAddressResponse.PathId;
		parser->ScsiTargetId = scsiAddressResponse.TargetId;
	}

	return status;
}

static NTSTATUS EvhdUnregisterIo(ParserInstance *parser)
{
	NTSTATUS status = STATUS_SUCCESS;

	if (parser->bIoRegistered)
	{
#pragma pack(push, 1)
		struct {
			INT dwVersion;
			INT dwUnk1;
			INT dwUnk2;
		} RemoveVhdRequest = { 1 };
#pragma pack(pop)

		EvhdDirectIoControl(parser, IOCTL_STORAGE_REMOVE_VIRTUAL_DISK, &RemoveVhdRequest, sizeof(RemoveVhdRequest));

		parser->bIoRegistered = FALSE;
		parser->Io.pIoInterface = NULL;
		parser->Io.pfnStartIo = NULL;
		parser->Io.pfnSaveData = NULL;
		parser->Io.pfnRestoreData = NULL;
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

	parser->wMountFlags = OpenFlags & 0x40 ? 4 : OpenFlags & 1;	// 4 - ignore SCSI_SYNCHRONIZE_CACHE opcodes, 1 - read only mount
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
	if (parser->bMounted)
	{
		EvhdUnregisterIo(parser);
		parser->bMounted = FALSE;
	}
	EvhdFinalize(parser);
	ExFreePoolWithTag(parser, EvhdPoolTag);
}
NTSTATUS EVhdMountDisk(ParserInstance *parser, UCHAR flags, PGUID pUnkGuid, __out MountInfo *mountInfo)
{
	NTSTATUS status = STATUS_SUCCESS;

	status = EvhdRegisterIo(parser, flags & 1, (flags >> 1) & 1, pUnkGuid);
	if (!NT_SUCCESS(status))
	{
		DEBUG("VHD: EvhdRegisterIo failed with error 0x%0x\n", status);
		EvhdUnregisterIo(parser);
		return status;
	}
	parser->bMounted = TRUE;
	mountInfo->dwInnerBufferSize = parser->dwInnerBufferSize;
	mountInfo->bUnk = FALSE;
	mountInfo->bFastPause = parser->bFastPause;
	mountInfo->bFastClose = parser->bFastClose;

	return status;
}

NTSTATUS EVhdDismountDisk(ParserInstance *parser)
{
	NTSTATUS status = STATUS_SUCCESS;

	status = EvhdUnregisterIo(parser);
	parser->bMounted = FALSE;
	return status;
}

NTSTATUS EVhdQueryMountStatusDisk(ParserInstance *parser, /* TODO */ ULONG32 unkParam)
{
	NTSTATUS status = STATUS_SUCCESS;
	if (!parser->bIoRegistered)
		return status;
#pragma pack(push, 1)
	struct {
		ULONG32 unkParam;
	} Request;
#pragma pack(pop)
	Request.unkParam = unkParam;
	status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_VALIDATE, &Request, sizeof(Request), NULL, 0);
	return status;
}

NTSTATUS EVhdExecuteScsiRequestDisk(ParserInstance *parser, ScsiPacket *pPacket)
{
	NTSTATUS status = STATUS_SUCCESS;
	ScsiPacketInnerRequest *pInner = pPacket->pInner;
	ScsiPacketRequest *pRequest = pPacket->pRequest;
	PMDL pMdl = pPacket->pMdl;
	SCSI_OP_CODE opCode = (UCHAR)pRequest->Sense.Cdb6.OpCode;
	memset(&pInner->Srb, 0, SCSI_REQUEST_BLOCK_SIZE);
	pInner->pParser = parser;
	pInner->Srb.Length = SCSI_REQUEST_BLOCK_SIZE;
	pInner->Srb.SrbStatus = pRequest->SrbStatus;
	pInner->Srb.ScsiStatus = pRequest->ScsiStatus;
	pInner->Srb.PathId = parser->ScsiPathId;
	pInner->Srb.TargetId = parser->ScsiTargetId;
	pInner->Srb.Lun = parser->ScsiLun;
	pInner->Srb.CdbLength = pRequest->CdbLength;
	pInner->Srb.SenseInfoBufferLength = pRequest->SenseInfoBufferLength;
	pInner->Srb.SrbFlags = pRequest->bDataIn ? SRB_FLAGS_DATA_IN : SRB_FLAGS_DATA_OUT;
	pInner->Srb.DataTransferLength = pRequest->DataTransferLength;
	pInner->Srb.SrbFlags |= pRequest->SrbFlags & 8000;			  // Non-standard
	pInner->Srb.SrbExtension = pInner + 1;	// variable-length extension right after the inner request block
	pInner->Srb.SenseInfoBuffer = &pRequest->Sense;
	memmove(pInner->Srb.Cdb, &pRequest->Sense, pRequest->CdbLength);
	switch (opCode)
	{
	default:
		if (pMdl)
		{
			pInner->Srb.DataBuffer = pMdl->MdlFlags & (MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL) ?
				pMdl->MappedSystemVa : MmMapLockedPagesSpecifyCache(pMdl, KernelMode, MmCached, NULL, FALSE,
				NormalPagePriority | MdlMappingNoExecute);
			if (!pInner->Srb.DataBuffer)
			{
				status = STATUS_INSUFFICIENT_RESOURCES;
				break;
			}
		}
		break;
	case SCSI_OP_CODE_WRITE_6:
	case SCSI_OP_CODE_WRITE_10:
	case SCSI_OP_CODE_WRITE_12:
	case SCSI_OP_CODE_WRITE_16:
	case SCSI_OP_CODE_READ_6:
	case SCSI_OP_CODE_READ_10:
	case SCSI_OP_CODE_READ_12:
	case SCSI_OP_CODE_READ_16:
		break;
	}

	if (NT_SUCCESS(status))
		status = parser->Io.pfnStartIo(parser->Io.pIoInterface, pPacket, pInner, pMdl, pPacket->bUnkFlag,
		pPacket->bUseInternalSenseBuffer ? &pPacket->Sense : NULL);
	else
		pRequest->SrbStatus = SRB_STATUS_INTERNAL_ERROR;

	if (STATUS_PENDING != status)
	{
		EvhdPostProcessScsiPacket(pPacket, status);
		status = VstorCompleteScsiRequest(pPacket);
	}

	return status;
}

NTSTATUS EVhdQueryInformationDisk(ParserInstance *parser, EDiskInfoType type, INT unused1, INT unused2, PVOID pBuffer, INT *pBufferSize)
{
	NTSTATUS status = STATUS_SUCCESS;
	EDiskInfoType Request = EDiskInfoType_Geometry;
	DiskInfoResponse Response = { 0 };

	UNREFERENCED_PARAMETER(unused1);
	UNREFERENCED_PARAMETER(unused2);

	if (EDiskInfo_Format == type)
	{
		ASSERT(0x38 == sizeof(DiskInfo_Format));
		if (*pBufferSize < sizeof(DiskInfo_Format))
			return status = STATUS_BUFFER_TOO_SMALL;
		DiskInfo_Format *pRes = (DiskInfo_Format *)pBuffer;
		memset(pBuffer, 0, sizeof(DiskInfo_Format));

		Request = EDiskInfoType_Type;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
			&Response, sizeof(DiskInfoResponse));
		if (!NT_SUCCESS(status))
		{
			DEBUG("Failed to retrieve disk type. 0x%0X\n", status);
			return status;
		}
		pRes->DiskType = Response.vals[0].dwLow;

		Request = EDiskInfoType_ParserInfo;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
			&Response, sizeof(DiskInfoResponse));
		if (!NT_SUCCESS(status))
		{
			DEBUG("Failed to retrieve parser info. 0x%0X\n", status);
			return status;
		}
		pRes->DiskFormat = Response.vals[0].dwLow;

		Request = EDiskInfoType_Geometry;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
			&Response, sizeof(DiskInfoResponse));
		if (!NT_SUCCESS(status))
		{
			DEBUG("Failed to retrieve size info. 0x%0X\n", status);
			return status;
		}
		pRes->dwBlockSize = Response.vals[2].dwLow;
		pRes->qwDiskSize = Response.vals[1].qword;

		Request = EDiskInfoType_LinkageId;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
			&Response, sizeof(DiskInfoResponse));
		if (!NT_SUCCESS(status))
		{
			DEBUG("Failed to retrieve linkage identifier. 0x%0X\n", status);
			return status;
		}
		pRes->LinkageId = Response.guid;

		Request = EDiskInfoType_InUseFlag;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
			&Response, sizeof(DiskInfoResponse));
		if (!NT_SUCCESS(status))
		{
			DEBUG("Failed to retrieve in use flag. 0x%0X\n", status);
			return status;
		}
		pRes->bIsInUse = (BOOLEAN)Response.vals[0].dwLow;

		Request = EDiskInfoType_IsFullyAllocated;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
			&Response, sizeof(DiskInfoResponse));
		if (!NT_SUCCESS(status))
		{
			DEBUG("Failed to retrieve fully allocated flag. 0x%0X\n", status);
			return status;
		}
		pRes->bIsFullyAllocated = (BOOLEAN)Response.vals[0].dwLow;

		Request = EDiskInfoType_Unk9;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
			&Response, sizeof(DiskInfoResponse));
		if (!NT_SUCCESS(status))
		{
			DEBUG("Failed to retrieve unk9 flag. 0x%0X\n", status);
			return status;
		}
		pRes->f_1C = (BOOLEAN)Response.vals[0].dwLow;

		Request = EDiskInfoType_Page83Data;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
			&Response, sizeof(DiskInfoResponse));
		if (!NT_SUCCESS(status))
		{
			DEBUG("Failed to retrieve disk identifier. 0x%0X\n", status);
			return status;
		}
		pRes->DiskIdentifier = Response.guid;

		*pBufferSize = sizeof(DiskInfo_Format);
	}
	else if (EDiskInfo_Fragmentation == type)
	{
		status = STATUS_INVALID_DEVICE_REQUEST;
	}
	else if (EDiskInfo_ParentNameList == type)
	{
#pragma pack(push, 1)
		typedef struct {
			ULONG64 f_0;
			UCHAR f_8;
			BOOLEAN bHaveParent;
			UCHAR _align[2];
			INT size;
			UCHAR data[1];
		} ParentNameResponse;

		typedef struct {
			UCHAR f_0;
			BOOLEAN bHaveParent;
			UCHAR _align[2];
			ULONG size;
			UCHAR data[1];
		} ResultBuffer;
#pragma pack(pop)
		if (*pBufferSize < 8)
			return status = STATUS_BUFFER_TOO_SMALL;
		ResultBuffer *pRes = (ResultBuffer *)pBuffer;
		memset(pRes, 0, 8);
		INT ResponseBufferSize = *pBufferSize + 0x18;
		ParentNameResponse *ResponseBuffer = (ParentNameResponse *)ExAllocatePoolWithTag(PagedPool, ResponseBufferSize, EvhdPoolTag);
		if (!ResponseBuffer)
			return STATUS_INSUFFICIENT_RESOURCES;
		Request = EDiskInfoType_ParentNameList;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
			ResponseBuffer, ResponseBufferSize);
		if (!NT_SUCCESS(status))
		{
			DEBUG("Failed to retrieve type info. 0x%0X\n", status);
			ExFreePoolWithTag(ResponseBuffer, EvhdPoolTag);
			return status;
		}
		pRes->f_0 = ResponseBuffer->f_8;
		pRes->bHaveParent = ResponseBuffer->bHaveParent;
		pRes->size = ResponseBuffer->size;
		if (ResponseBuffer->bHaveParent)
		{
			memmove(pRes->data, ResponseBuffer->data, pRes->size);
			ExFreePoolWithTag(ResponseBuffer, EvhdPoolTag);
			*pBufferSize = 8 + pRes->size;
		}
		else
			*pBufferSize = 8;
	}
	else if (EDiskInfo_PreloadDiskMetadata == type)
	{
		if (*pBufferSize < sizeof(BOOLEAN))
			return status = STATUS_BUFFER_TOO_SMALL;
		BOOLEAN *pRes = (BOOLEAN *)pBuffer;
		BOOLEAN Response = FALSE;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_PRELOAD_DISK_METADATA_V2, NULL, 0,
			&Response, sizeof(BOOLEAN));
		if (!NT_SUCCESS(status))
		{
			DEBUG("Failed to retrieve type info. 0x%0X\n", status);
			return status;
		}
		*pRes = Response;
		*pBufferSize = sizeof(BOOLEAN);
	}
	else if (EDiskInfo_Geometry == type)
	{
		ASSERT(0x10 == sizeof(DiskInfo_Geometry));
		if (*pBufferSize < sizeof(DiskInfo_Geometry))
			return status = STATUS_BUFFER_TOO_SMALL;
		DiskInfo_Geometry *pRes = (DiskInfo_Geometry *)pBuffer;
		Request = EDiskInfoType_Geometry;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
			&Response, sizeof(DiskInfoResponse));
		if (!NT_SUCCESS(status))
		{
			DEBUG("Failed to retrieve size info. 0x%0X\n", status);
			return status;
		}
		pRes->dwSectorSize = Response.vals[2].dwHigh;
		pRes->qwDiskSize = Response.vals[0].qword;
		Request = EDiskInfoType_NumSectors;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
			&Response, sizeof(DiskInfoResponse));
		if (!NT_SUCCESS(status))
		{
			DEBUG("Failed to retrieve number of sectors. 0x%0X\n", status);
			return status;
		}
		pRes->dwNumSectors = Response.vals[0].dwLow;
		*pBufferSize = sizeof(DiskInfo_Geometry);
	}
	else
	{
		DEBUG("Unknown Disk info type %X\n", type);
		status = STATUS_INVALID_DEVICE_REQUEST;
	}

	return status;
}

NTSTATUS EVhdQuerySaveVersionDisk(ParserInstance *parser, INT *pVersion)
{
	UNREFERENCED_PARAMETER(parser);
	*pVersion = 1;
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS EVhdSaveDisk(ParserInstance *parser, PVOID data, ULONG32 size, ULONG32 *dataStored)
{
	ULONG32 dwSize = parser->dwDiskSaveSize + 0x20;
	if (dwSize < parser->dwDiskSaveSize)
		return STATUS_INTEGER_OVERFLOW;
	if (dwSize > size)
		return STATUS_BUFFER_TOO_SMALL;
	parser->Io.pfnSaveData(parser->Io.pIoInterface, (UCHAR *)data + 0x20, parser->dwDiskSaveSize);
	*dataStored = dwSize;
	return STATUS_SUCCESS;
}

NTSTATUS EVhdRestoreDisk(ParserInstance *parser, INT revision, PVOID data, ULONG32 size)
{
	if (revision != 1)	 // Assigned by IOCTL_VIRTUAL_DISK_SET_SAVE_VERSION
		return STATUS_REVISION_MISMATCH;
	ULONG32 dwSize = parser->dwDiskSaveSize + 0x20;
	if (dwSize < parser->dwDiskSaveSize)
		return STATUS_INTEGER_OVERFLOW;
	if (dwSize > size)
		return STATUS_INVALID_BUFFER_SIZE;

	parser->Io.pfnRestoreData(parser->Io.pIoInterface, (UCHAR *)data + 0x20, parser->dwDiskSaveSize);

	return STATUS_SUCCESS;
}

static NTSTATUS EvhdSetCachePolicy(ParserInstance *parser, BOOLEAN bEnable)
{
#pragma pack(push, 1)
	struct {
		ULONG dwVersion;
		USHORT wFlags;
		UCHAR _align[2];
	} Request = { 0 };
#pragma pack(pop)
	Request.dwVersion = 1;
	Request.wFlags = bEnable ? 4 : 0;
	return SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_SET_CACHE_POLICY, NULL, 0, NULL, 0);
}

NTSTATUS EVhdSetBehaviourDisk(ParserInstance *parser, INT behaviour, BOOLEAN *enableCache, INT param)
{
	switch (behaviour)
	{
	case 0x40000001:
		return SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_ISO_EJECT_MEDIA, NULL, 0, NULL, 0);
	case 0x40000002:
		return SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_ISO_INSERT_MEDIA, NULL, 0, NULL, 0);
	case 0x40000003:
		if (param < 1)
			return STATUS_INVALID_PARAMETER;
		return EvhdSetCachePolicy(parser, *enableCache != 0);
	default:
		return STATUS_INVALID_DEVICE_REQUEST;
	}
}

NTSTATUS EVhdSetQosPolicyDisk(ParserInstance *parser, PVOID pInputBuffer, ULONG32 dwSize)
{
	return SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_SET_QOS_POLICY, pInputBuffer, dwSize, NULL, 0);
}

static NTSTATUS EvhdGetQosStatusCompletionRoutine(PDEVICE_OBJECT DeviceObject, PIRP pIrp, PVOID pContext)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	ParserInstance *parser = (ParserInstance *)pContext;
	parser->pfnQoSStatusCallback(pIrp->IoStatus.Status, parser->pQoSStatusBuffer, parser->pQoSStatusInterface);
	return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS EVhdGetQosStatusDisk(ParserInstance *parser, PVOID pSystemBuffer, ULONG32 dwSize, QoSStatusCompletionRoutine pfnCompletionCb, PVOID pInterface)
{
	parser->pQoSStatusInterface = pInterface;
	parser->pfnQoSStatusCallback = pfnCompletionCb;
	memmove(parser->pQoSStatusBuffer, pSystemBuffer, dwSize);
	return EvhdQosStatusControl(parser, IOCTL_STORAGE_VHD_GET_QOS_STATUS, dwSize, 0x58, EvhdGetQosStatusCompletionRoutine);
}

NTSTATUS EVhdChangeTrackingGetParameters(ParserInstance *parser, CTParameters *pParams)
{
	CTParameters Response = { 0 };
	NTSTATUS status = STATUS_SUCCESS;
	status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_GET_CTLOG_INFO, NULL, 0, &Response, sizeof(Response));
	if (NT_SUCCESS(status))
	{
		pParams->dwVersion = Response.dwVersion;
		pParams->qwUncommitedSize = Response.qwUncommitedSize;
	}
	return status;
}

NTSTATUS EVhdChangeTrackingStart(ParserInstance *parser, CTStartParam *pParam)
{
#pragma pack(push, 1)
	typedef struct {
		ULONG32 dwInnerSize;
		ULONG32 dwSize;
		ULONG64 qwMaximumDiffSize;
		GUID CdpSnapshotId;
		BOOLEAN bSkipSnapshoting;
		UCHAR _reserved1[7];
		ULONG32 FeatureFlags;	// overrides flags
		UCHAR _reserved2[20];
	} EnableChangeTrackingRequest;

#pragma pack(pop)						  
	NTSTATUS status = STATUS_SUCCESS;

	EnableChangeTrackingRequest *pRequest = ExAllocatePoolWithTag(PagedPool, sizeof(EnableChangeTrackingRequest) + pParam->dwInnerBufferSize, EvhdPoolTag);
	if (!pRequest)
		return STATUS_INSUFFICIENT_RESOURCES;

	memmove((UCHAR *)pRequest + sizeof(EnableChangeTrackingRequest), (UCHAR *)pParam + sizeof(CTStartParam), pParam->dwInnerBufferSize);

	pRequest->dwSize = sizeof(EnableChangeTrackingRequest);
	pRequest->dwInnerSize = pParam->dwInnerBufferSize;
	pRequest->qwMaximumDiffSize = pParam->qwMaximumDiffSize;
	pRequest->CdpSnapshotId = pParam->CdpSnapshotId;
	pRequest->bSkipSnapshoting = pParam->SkipSnapshoting;
	pRequest->FeatureFlags = pParam->FeatureFlags;

	status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_ENABLE_TRACKING, pRequest, sizeof(EnableChangeTrackingRequest) + pParam->dwInnerBufferSize, NULL, 0);

	ExFreePoolWithTag(pRequest, EvhdPoolTag);

	return status;
}

NTSTATUS EVhdChangeTrackingStop(ParserInstance *parser, const ULONG32 *pInput, ULONG32 *pOutput)
{
	ULONG32 Request = *pInput;
	ULONG32 Response;
	NTSTATUS status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_DISABLE_TRACKING, &Request, sizeof(Request), &Response, sizeof(Response));
	if (NT_SUCCESS(status))
		*pOutput = Response;

	return status;
}

NTSTATUS EVhdChangeTrackingSwitchLogs(ParserInstance *parser, CTSwitchLogParam *pParam, ULONG32 *pOutput)
{

#pragma pack(push, 1)
	typedef struct {
		ULONG32 dwSize;
		ULONG32 dwInnerSize;
		GUID OfflineCdpAppId;
		ULONG32 CTState;
		ULONG32 FeatureFlags;
		GUID CdpSnapshotId;
		UCHAR _reserved[16];
	} SwitchCTLogsRequest;

#pragma pack(pop)						  
	NTSTATUS status = STATUS_SUCCESS;
	ULONG32 Response;

	SwitchCTLogsRequest *pRequest = ExAllocatePoolWithTag(PagedPool, sizeof(SwitchCTLogsRequest) + pParam->dwInnerBufferSize, EvhdPoolTag);
	if (!pRequest)
		return STATUS_INSUFFICIENT_RESOURCES;

	pRequest->dwSize = sizeof(SwitchCTLogsRequest);
	pRequest->dwInnerSize = pParam->dwInnerBufferSize;
	pRequest->OfflineCdpAppId = pParam->OfflineCdpId;
	pRequest->CTState = pParam->CTState;
	pRequest->FeatureFlags = pParam->FeatureFlags;
	pRequest->CdpSnapshotId = pParam->CdpSnapshotId;
	memmove((UCHAR *)pRequest + sizeof(SwitchCTLogsRequest), (UCHAR *)pParam + sizeof(CTSwitchLogParam), pParam->dwInnerBufferSize);

	status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_CHANGE_CTLOG_FILE, pRequest, sizeof(SwitchCTLogsRequest) + pParam->dwInnerBufferSize, &Response, sizeof(Response));
	if (NT_SUCCESS(status))
		*pOutput = Response;

	ExFreePoolWithTag(pRequest, EvhdPoolTag);

	return status;
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
