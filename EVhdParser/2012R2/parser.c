#include "stdafx.h"
#include "parser.h"
#include "Ioctl.h"
#include "utils.h"
#include "Vdrvroot.h"
#include "Guids.h"
#include "Log.h"
#include "Extension.h"

#define LOG_PARSER(level, format, ...) LOG_FUNCTION(level, LOG_CTG_PARSER, format, __VA_ARGS__)

static const ULONG EvhdPoolTag = 'VVpp';

static NTSTATUS EVhd_InitializeExtension(ParserInstance *parser, PGUID applicationId, PCUNICODE_STRING diskPath)
{
	NTSTATUS status = STATUS_SUCCESS;
	DISK_INFO_RESPONSE Response = { 0 };
    DISK_INFO_REQUEST Request = { EDiskInfoType_ParserInfo };
	EDiskFormat DiskFormat = EDiskFormat_Unknown;
	status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION,
        &Request, sizeof(DISK_INFO_REQUEST), &Response, sizeof(DISK_INFO_RESPONSE));
	if (!NT_SUCCESS(status))
	{
        LOG_PARSER(LL_FATAL, "Failed to retreive parser info. 0x%0X\n", status);
		return status;
	}
	DiskFormat = Response.vals[0].dwLow;

	Request.RequestCode = EDiskInfoType_LinkageId;
    status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION,
        &Request, sizeof(DISK_INFO_REQUEST), &Response, sizeof(DISK_INFO_RESPONSE));
	if (!NT_SUCCESS(status))
	{
        LOG_PARSER(LL_FATAL, "Failed to retreive virtual disk identifier. 0x%0X\n", status);
		return status;
	}
    status = Ext_Create(diskPath, applicationId, DiskFormat, &Response.guid, &parser->pExtension);
	return status;
}

static NTSTATUS EVhd_FinalizeExtension(ParserInstance *parser)
{
	NTSTATUS status = STATUS_SUCCESS;
	if (parser->pExtension)
	{
        status = Ext_Delete(parser->pExtension);
	}
	return status;
}

static NTSTATUS EVhd_Initialize(HANDLE hFileHandle, PFILE_OBJECT pFileObject, ParserInstance *parser)
{
	NTSTATUS status = STATUS_SUCCESS;
	DISK_INFO_RESPONSE Response = { 0 };
    DISK_INFO_REQUEST Request = { EDiskInfoType_NumSectors };

	parser->pVhdmpFileObject = pFileObject;
	parser->FileHandle = hFileHandle;
	parser->pIrp = IoAllocateIrp(IoGetRelatedDeviceObject(parser->pVhdmpFileObject)->StackSize, FALSE);
	if (!parser->pIrp)
	{
        LOG_PARSER(LL_FATAL, "IoAllocateIrp failed\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

    status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION,
        &Request, sizeof(DISK_INFO_REQUEST), &Response, sizeof(DISK_INFO_RESPONSE));

    FltInitializePushLock(&parser->IoLock);

	if (!NT_SUCCESS(status))
	{
        LOG_PARSER(LL_FATAL, "SynchronouseCall IOCTL_STORAGE_VHD_GET_INFORMATION failed with error 0x%0x\n", status);
	}
	else
		parser->dwNumSectors = Response.vals[0].dwLow;

	return status;
}

static VOID EVhd_Finalize(ParserInstance *parser)
{
	EVhd_FinalizeExtension(parser);

	if (parser->pIrp)
		IoFreeIrp(parser->pIrp);

	if (parser->pVhdmpFileObject)
	{
		ObfDereferenceObject(parser->pVhdmpFileObject);
		parser->pVhdmpFileObject = NULL;
	}

	if (parser->FileHandle)
	{
		ZwClose(parser->FileHandle);
		parser->FileHandle = NULL;
	}
}

static NTSTATUS EVhd_RegisterQosInterface(ParserInstance *parser)
{
	NTSTATUS status = STATUS_SUCCESS;

	if (!parser->bQosRegistered)
	{
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_REGISTER_QOS_INTERFACE, NULL, 0,
			&parser->Qos, sizeof(PARSER_QOS_INFO));
		parser->bQosRegistered = TRUE;
	}

	return status;
}

static NTSTATUS EVhd_UnregisterQosInterface(ParserInstance *parser)
{
	NTSTATUS status = STATUS_SUCCESS;

	if (parser->bQosRegistered)
	{
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_UNREGISTER_QOS_INTERFACE, &parser->Qos, sizeof(PARSER_QOS_INFO),
			&parser->Qos, sizeof(PARSER_QOS_INFO));
		parser->bQosRegistered = FALSE;
	}
	return status;
}

static void EVhd_PostProcessScsiPacket(SCSI_PACKET *pPacket, NTSTATUS status)
{
    pPacket->pVscRequest->SrbStatus = pPacket->pVspRequest->Srb.SrbStatus;
    pPacket->pVscRequest->ScsiStatus = pPacket->pVspRequest->Srb.ScsiStatus;
    pPacket->pVscRequest->SenseInfoBufferLength = pPacket->pVspRequest->Srb.SenseInfoBufferLength;
    pPacket->pVscRequest->DataTransferLength = pPacket->pVspRequest->Srb.DataTransferLength;
	if (!NT_SUCCESS(status))
	{
        if (SRB_STATUS_SUCCESS != pPacket->pVspRequest->Srb.SrbStatus)
            pPacket->pVscRequest->SrbStatus = SRB_STATUS_ERROR;
	}
	else
	{
        switch (pPacket->pVspRequest->Srb.Cdb[0])
        {
        case SCSI_OP_CODE_INQUIRY:
            pPacket->pVscRequest->bReserved |= 1;
            break;
        }
	}

    ParserInstance *pParser = pPacket->pContext;
    if (pParser->pExtension) {
        EVHD_EXT_SCSI_PACKET ExtPacket;
        ExtPacket.pMdl = pPacket->pMdl;
        ExtPacket.pSenseBuffer = &pPacket->Sense;
        ExtPacket.SenseBufferLength = pPacket->pVspRequest->Srb.SenseInfoBufferLength;
        ExtPacket.Srb = &pPacket->pVspRequest->Srb;
        status = Ext_CompleteScsiRequest(pParser->pExtension, &ExtPacket, status);
        if (NT_SUCCESS(status)) {
            pPacket->pMdl = ExtPacket.pMdl;
        }
    }
}

NTSTATUS EVhd_CompleteScsiRequest(SCSI_PACKET *pPacket, NTSTATUS VspStatus)
{
    NTSTATUS status;
    //TRACE_FUNCTION_IN();
    EVhd_PostProcessScsiPacket(pPacket, VspStatus);
    status = VstorCompleteScsiRequest(pPacket);
    //TRACE_FUNCTION_OUT_STATUS(status);
    return status;
}

NTSTATUS EVhd_SendNotification(void *param1, INT param2)
{
    TRACE_FUNCTION_IN();
	NTSTATUS status = VstorSendNotification(param1, param2);
    TRACE_FUNCTION_OUT_STATUS(status);
    return status;
}

NTSTATUS EVhd_SendMediaNotification(void *param1)
{
    TRACE_FUNCTION_IN();
	NTSTATUS status = VstorSendMediaNotification(param1);
    TRACE_FUNCTION_OUT_STATUS(status);
    return status;
}


static NTSTATUS EVhd_RegisterIo(ParserInstance *parser, BOOLEAN flag1, BOOLEAN flag2)
{
    TRACE_FUNCTION_IN();
	NTSTATUS status = STATUS_SUCCESS;

    if (parser->pExtension)
    {
        status = Ext_Mount(parser->pExtension);
        if (!NT_SUCCESS(status))
        {
            return status;
        }
    }

	if (!parser->bIoRegistered)
	{
		REGISTER_IO_REQUEST request = { 0 };
		REGISTER_IO_RESPONSE response = { 0 };
		SCSI_ADDRESS scsiAddressResponse = { 0 };

		request.dwVersion = 1;
		request.dwFlags = flag1 ? 9 : 8;
		request.wFlags = 0x2;
		if (flag2) request.wFlags |= 0x1;
		request.pfnCompleteScsiRequest = &EVhd_CompleteScsiRequest;
		request.pfnSendMediaNotification = &EVhd_SendMediaNotification;
		request.pfnSendNotification = &EVhd_SendNotification;
		request.pVstorInterface = parser->pVstorInterface;

        status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_REGISTER_IO, &request, sizeof(REGISTER_IO_REQUEST),
            &response, sizeof(REGISTER_IO_RESPONSE));

		if (!NT_SUCCESS(status))
		{
			LOG_PARSER(LL_ERROR, "IOCTL_STORAGE_REGISTER_IO failed with error 0x%0X\n", status);
			return status;
		}

		parser->bIoRegistered = TRUE;
		parser->dwDiskSaveSize = response.dwDiskSaveSize;
		parser->dwInnerBufferSize = sizeof(PARSER_STATE) + response.dwExtensionBufferSize;
		parser->Io = response.Io;

		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_SCSI_GET_ADDRESS, NULL, 0, &scsiAddressResponse, sizeof(SCSI_ADDRESS));

		if (!NT_SUCCESS(status))
		{
			LOG_PARSER(LL_ERROR, "IOCTL_SCSI_GET_ADDRESS failed with error 0x%0X\n", status);
			return status;
		}

		parser->ScsiLun = scsiAddressResponse.Lun;
		parser->ScsiPathId = scsiAddressResponse.PathId;
		parser->ScsiTargetId = scsiAddressResponse.TargetId;
	}

    TRACE_FUNCTION_OUT_STATUS(status);

	return status;
}

static NTSTATUS EVhd_DirectIoControl(ParserInstance *parser, ULONG ControlCode, PVOID pInputBuffer, ULONG InputBufferSize)
{
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_OBJECT pDeviceObject = NULL;
	KeEnterCriticalRegion();
	FltAcquirePushLockExclusive(&parser->IoLock);

	IoReuseIrp(parser->pIrp, STATUS_PENDING);
	parser->pIrp->Flags |= IRP_NOCACHE;
	parser->pIrp->Tail.Overlay.Thread = (PETHREAD)__readgsqword(0x188);		// Pointer to calling thread control block
	parser->pIrp->AssociatedIrp.SystemBuffer = pInputBuffer;				// IO buffer for buffered control code
	// fill stack frame parameters for synchronous IRP call
	PIO_STACK_LOCATION pStackFrame = IoGetNextIrpStackLocation(parser->pIrp);
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
	IoSynchronousCallDriver(pDeviceObject, parser->pIrp);
	status = parser->pIrp->IoStatus.Status;
	FltReleasePushLock(&parser->IoLock);
	KeLeaveCriticalRegion();
	return status;
}

static NTSTATUS EVhd_UnregisterIo(ParserInstance *parser)
{
    TRACE_FUNCTION_IN();
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

		EVhd_DirectIoControl(parser, IOCTL_STORAGE_REMOVE_VIRTUAL_DISK, &RemoveVhdRequest, sizeof(RemoveVhdRequest));

		parser->bIoRegistered = FALSE;
		parser->Io.pIoInterface = NULL;
		parser->Io.pfnStartIo = NULL;
		parser->Io.pfnSaveData = NULL;
		parser->Io.pfnRestoreData = NULL;
		parser->dwDiskSaveSize = 0;
		parser->dwInnerBufferSize = 0;
	}

    if (parser->pExtension)
        status = Ext_Dismount(parser->pExtension);

    TRACE_FUNCTION_OUT_STATUS(status);

	return status;
}

/** Create virtual disk parser */
NTSTATUS EVhd_OpenDisk(PCUNICODE_STRING diskPath, ULONG32 OpenFlags, GUID *pVmId, PVOID vstorInterface, PVOID *ppOutContext)
{
    TRACE_FUNCTION_IN();
	NTSTATUS status = STATUS_SUCCESS;
	PFILE_OBJECT pFileObject = NULL;
	HANDLE FileHandle = NULL;
	ParserInstance *parser = NULL;
	RESILIENCY_INFO_EA vmInfo = { 0 };

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
        LOG_PARSER(LL_ERROR, "Failed to open vhdmp device for virtual disk file %S\n", diskPath->Buffer);
		goto failure_cleanup;
	}

	parser = (ParserInstance *)ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(ParserInstance), EvhdPoolTag);
	if (!parser)
	{
        LOG_PARSER(LL_ERROR, "Failed to allocate memory for ParserInstance\n");
		status = STATUS_NO_MEMORY;
		goto failure_cleanup;
	}

	memset(parser, 0, sizeof(ParserInstance));

	status = EVhd_Initialize(FileHandle, pFileObject, parser);

	if (!NT_SUCCESS(status))
		goto failure_cleanup;

	status = EVhd_RegisterQosInterface(parser);

	if (!NT_SUCCESS(status))
		goto failure_cleanup;

	//status = EvhdQueryFastPauseValue(parser);
	//
	//if (!NT_SUCCESS(status))
	//	goto failure_cleanup;

	parser->pVstorInterface = vstorInterface;

	status = EVhd_InitializeExtension(parser, pVmId, diskPath);
	if (!NT_SUCCESS(status))
	{
        LOG_PARSER(LL_ERROR, "EVhd_InitializeExtension failed with error 0x%08X\n", status);
		goto failure_cleanup;
	}

	*ppOutContext = parser;

	goto cleanup;

failure_cleanup:
	if (FileHandle)
	{
		ZwClose(FileHandle);
		parser->FileHandle = NULL;
	}
	if (parser)
	{
		EVhd_CloseDisk(parser);
	}

cleanup:
    TRACE_FUNCTION_OUT_STATUS(status);

	return status;
}

/** Destroy virtual disk parser */
VOID EVhd_CloseDisk(PVOID pContext)
{
    TRACE_FUNCTION_IN();
    ParserInstance *parser = pContext;
	if (parser->pVhdmpFileObject && parser->bQosRegistered && parser->Qos.pQosInterface)
		EVhd_UnregisterQosInterface(parser);
	if (parser->bMounted)
		EVhd_UnregisterIo(parser);

	EVhd_Finalize(parser);
	ExFreePoolWithTag(parser, EvhdPoolTag);
    TRACE_FUNCTION_OUT();
}

/** Initiate virtual disk IO */
NTSTATUS EVhd_MountDisk(PVOID pContext, UCHAR flags, PARSER_MOUNT_INFO *mountInfo)
{
    TRACE_FUNCTION_IN();
	NTSTATUS status = STATUS_SUCCESS;
    ParserInstance *parser = pContext;

	status = EVhd_RegisterIo(parser, flags & 1, (flags >> 1) & 1);
	if (!NT_SUCCESS(status))
	{
        LOG_PARSER(LL_ERROR, "VHD: EvhdRegisterIo failed with error 0x%0x\n", status);
		EVhd_UnregisterIo(parser);
		return status;
	}
	parser->bMounted = TRUE;
	mountInfo->dwInnerBufferSize = parser->dwInnerBufferSize;
	mountInfo->bUnk = FALSE;
	mountInfo->bFastPause = parser->bFastPause;
    TRACE_FUNCTION_OUT_STATUS(status);
	return status;
}

/** Finalize virtual disk IO */
NTSTATUS EVhd_DismountDisk(PVOID pContext)
{
    TRACE_FUNCTION_IN();
	NTSTATUS status = STATUS_SUCCESS;
    ParserInstance *parser = pContext;
	status = EVhd_UnregisterIo(parser);
	parser->bMounted = FALSE;
    TRACE_FUNCTION_OUT_STATUS(status);
	return status;
}

/** Validate mounted disk */
NTSTATUS EVhd_QueryMountStatusDisk(PVOID pContext)
{
    TRACE_FUNCTION_IN();
	NTSTATUS status = STATUS_SUCCESS;
    ParserInstance *parser = pContext;
	if (!parser->bIoRegistered)
		return status;
#pragma pack(push, 1)
	struct {
		INT dwVersion;
		INT dwUnk1;
		LONG64 qwUnk1;
		LONG64 qwUnk2;
	} Request;
#pragma pack(pop)
	Request.dwVersion = 0;
	Request.dwUnk1 = 0;
	Request.qwUnk1 = 0;
	Request.qwUnk2 = 0;
	status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_VALIDATE, &Request, sizeof(Request), NULL, 0);
    TRACE_FUNCTION_OUT_STATUS(status);
	return status;
}

/** Scsi request filter function */
NTSTATUS EVhd_ExecuteScsiRequestDisk(PVOID pContext, SCSI_PACKET *pPacket)
{
    //TRACE_FUNCTION_IN();
	NTSTATUS status = STATUS_SUCCESS;
    ParserInstance *parser = pContext;
    STORVSP_REQUEST *pVspRequest = pPacket->pVspRequest;
    STORVSC_REQUEST *pVscRequest = pPacket->pVscRequest;
    PMDL pMdl = pPacket->pMdl;
	memset(&pVspRequest->Srb, 0, SCSI_REQUEST_BLOCK_SIZE);
	pVspRequest->pContext = pContext;
	pVspRequest->Srb.Length = SCSI_REQUEST_BLOCK_SIZE;
    pVspRequest->Srb.SrbStatus = pVscRequest->SrbStatus;
    pVspRequest->Srb.ScsiStatus = pVscRequest->ScsiStatus;
	pVspRequest->Srb.PathId = parser->ScsiPathId;
	pVspRequest->Srb.TargetId = parser->ScsiTargetId;
	pVspRequest->Srb.Lun = parser->ScsiLun;
    pVspRequest->Srb.CdbLength = pVscRequest->CdbLength;
    pVspRequest->Srb.SenseInfoBufferLength = pVscRequest->SenseInfoBufferLength;
    switch (pVscRequest->bDataIn) {
    case READ_TYPE:
        pVspRequest->Srb.SrbFlags = SRB_FLAGS_DATA_IN;
        break;
    default:
        pVspRequest->Srb.SrbFlags = SRB_FLAGS_DATA_OUT;
    }
    pVspRequest->Srb.DataTransferLength = pVscRequest->DataTransferLength;
    pVspRequest->Srb.SrbFlags |= pVscRequest->Extension.SrbFlags & 8000;			  // Non-standard
	pVspRequest->Srb.SrbExtension = pVspRequest + 1;	// variable-length extension right after the inner request block
    pVspRequest->Srb.SenseInfoBuffer = &pVscRequest->Sense;
    memmove(pVspRequest->Srb.Cdb, &pVscRequest->Sense, pVscRequest->CdbLength);
    switch (pVspRequest->Srb.Cdb[0])
    {
    default:
        if (pMdl)
        {
            pVspRequest->Srb.DataBuffer = MmGetSystemAddressForMdlSafe(pMdl, NormalPagePriority | MdlMappingNoExecute);
            if (!pVspRequest->Srb.DataBuffer)
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

    if (NT_SUCCESS(status) && parser->pExtension) {
        EVHD_EXT_SCSI_PACKET ExtPacket;
        ExtPacket.pMdl = pPacket->pMdl;
        ExtPacket.pSenseBuffer = &pPacket->Sense;
        ExtPacket.SenseBufferLength = pPacket->pVspRequest->Srb.SenseInfoBufferLength;
        ExtPacket.Srb = &pVspRequest->Srb;
        status = Ext_StartScsiRequest(parser->pExtension, &ExtPacket);
        pPacket->pMdl = ExtPacket.pMdl;
    }

    if (NT_SUCCESS(status)) {
        status = parser->Io.pfnStartIo(parser->Io.pIoInterface, pPacket, pVspRequest, pPacket->pMdl, pPacket->bUnkFlag,
            pPacket->bUseInternalSenseBuffer ? &pPacket->Sense : NULL);
    }
	else
        pVscRequest->SrbStatus = SRB_STATUS_INTERNAL_ERROR;

	if (STATUS_PENDING != status) {
        EVhd_PostProcessScsiPacket(pPacket, status);
        status = VstorCompleteScsiRequest(pPacket);
	}
    //TRACE_FUNCTION_OUT_STATUS(status);
	
	return status;

}

/** Query virtual disk info */
NTSTATUS EVhd_QueryInformationDisk(PVOID pContext, EDiskInfoType type, INT unused1, INT unused2, PVOID pBuffer, INT *pBufferSize)
{
    TRACE_FUNCTION_IN();
	NTSTATUS status = STATUS_SUCCESS;
    DISK_INFO_REQUEST Request = { EDiskInfoType_Geometry };
	DISK_INFO_RESPONSE Response = { 0 };
    ParserInstance *parser = pContext;

	UNREFERENCED_PARAMETER(unused1);
	UNREFERENCED_PARAMETER(unused2);

	if (EDiskInfo_Format == type)
	{
		ASSERT(0x38 == sizeof(DISK_INFO_FORMAT));
		if (*pBufferSize < sizeof(DISK_INFO_FORMAT))
			return status = STATUS_BUFFER_TOO_SMALL;
		DISK_INFO_FORMAT *pRes = pBuffer;
		memset(pBuffer, 0, sizeof(DISK_INFO_FORMAT));

		Request.RequestCode = EDiskInfoType_Type;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION,
            &Request, sizeof(DISK_INFO_REQUEST), &Response, sizeof(DISK_INFO_RESPONSE));
		if (!NT_SUCCESS(status))
		{
            LOG_PARSER(LL_ERROR, "Failed to retreive disk type. 0x%0X\n", status);
			return status;
		}
		pRes->DiskType = Response.vals[0].dwLow;

		Request.RequestCode = EDiskInfoType_ParserInfo;
        status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION,
            &Request, sizeof(DISK_INFO_REQUEST), &Response, sizeof(DISK_INFO_RESPONSE));
		if (!NT_SUCCESS(status))
		{
            LOG_PARSER(LL_ERROR, "Failed to retreive parser info. 0x%0X\n", status);
			return status;
		}
		pRes->DiskFormat = Response.vals[0].dwLow;

		Request.RequestCode = EDiskInfoType_Geometry;
        status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION,
            &Request, sizeof(DISK_INFO_REQUEST), &Response, sizeof(DISK_INFO_RESPONSE));
		if (!NT_SUCCESS(status))
		{
            LOG_PARSER(LL_ERROR, "Failed to retreive size info. 0x%0X\n", status);
			return status;
		}
		pRes->dwBlockSize = Response.vals[2].dwLow;
		pRes->qwDiskSize = Response.vals[1].qword;

		Request.RequestCode = EDiskInfoType_LinkageId;
        status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION,
            &Request, sizeof(DISK_INFO_REQUEST), &Response, sizeof(DISK_INFO_RESPONSE));
		if (!NT_SUCCESS(status))
		{
            LOG_PARSER(LL_ERROR, "Failed to retreive linkage identifier. 0x%0X\n", status);
			return status;
		}
		pRes->LinkageId = Response.guid;

		Request.RequestCode = EDiskInfoType_InUseFlag;
        status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION,
            &Request, sizeof(DISK_INFO_REQUEST), &Response, sizeof(DISK_INFO_RESPONSE));
		if (!NT_SUCCESS(status))
		{
            LOG_PARSER(LL_ERROR, "Failed to retreive in use flag. 0x%0X\n", status);
			return status;
		}
		pRes->bIsInUse = (BOOLEAN)Response.vals[0].dwLow;

		Request.RequestCode = EDiskInfoType_IsFullyAllocated;
        status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION,
            &Request, sizeof(DISK_INFO_REQUEST), &Response, sizeof(DISK_INFO_RESPONSE));
		if (!NT_SUCCESS(status))
		{
            LOG_PARSER(LL_ERROR, "Failed to retreive fully allocated flag. 0x%0X\n", status);
			return status;
		}
		pRes->bIsFullyAllocated = (BOOLEAN)Response.vals[0].dwLow;

        Request.RequestCode = EDiskInfoType_PhysicalDisk;
        status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION,
            &Request, sizeof(DISK_INFO_REQUEST), &Response, sizeof(DISK_INFO_RESPONSE));
		if (!NT_SUCCESS(status))
		{
            LOG_PARSER(LL_ERROR, "Failed to retreive unk9 flag. 0x%0X\n", status);
			return status;
		}
		pRes->IsRemote = (BOOLEAN)Response.vals[0].dwLow;

        Request.RequestCode = EDiskInfoType_Page83Data;
        status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION,
            &Request, sizeof(DISK_INFO_REQUEST), &Response, sizeof(DISK_INFO_RESPONSE));
		if (!NT_SUCCESS(status))
		{
            LOG_PARSER(LL_ERROR, "Failed to retreive disk identifier. 0x%0X\n", status);
			return status;
		}
		pRes->DiskIdentifier = Response.guid;

		*pBufferSize = sizeof(DISK_INFO_FORMAT);
	}
	else if (EDiskInfo_Fragmentation == type)
	{
		if (*pBufferSize < sizeof(INT))
			return status = STATUS_BUFFER_TOO_SMALL;
		INT *pRes = (INT *)pBuffer;
        Request.RequestCode = EDiskInfoType_FragmentationPercentage;
        status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION,
            &Request, sizeof(DISK_INFO_REQUEST), &Response, sizeof(DISK_INFO_RESPONSE));
		if (!NT_SUCCESS(status))
		{
            LOG_PARSER(LL_ERROR, "Failed to retreive type info. 0x%0X\n", status);
			return status;
		}
		*pRes = Response.vals[0].dwLow;
		*pBufferSize = sizeof(INT);
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
        Request.RequestCode = EDiskInfoType_ParentNameList;
        status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION,
            &Request, sizeof(DISK_INFO_REQUEST), &Response, sizeof(DISK_INFO_RESPONSE));
		if (!NT_SUCCESS(status))
		{
            LOG_PARSER(LL_ERROR, "Failed to retreive type info. 0x%0X\n", status);
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
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_PRELOAD_DISK_METADATA, NULL, 0,
			&Response, sizeof(BOOLEAN));
		if (!NT_SUCCESS(status))
		{
            LOG_PARSER(LL_ERROR, "Failed to retreive type info. 0x%0X\n", status);
			return status;
		}
		*pRes = Response;
		*pBufferSize = sizeof(BOOLEAN);
	}
	else if (EDiskInfo_Geometry == type)
	{
		ASSERT(0x10 == sizeof(DISK_INFO_GEOMETRY));
		if (*pBufferSize < sizeof(DISK_INFO_GEOMETRY))
			return status = STATUS_BUFFER_TOO_SMALL;
		DISK_INFO_GEOMETRY *pRes = pBuffer;
        Request.RequestCode = EDiskInfoType_Geometry;
        status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION,
            &Request, sizeof(DISK_INFO_REQUEST), &Response, sizeof(DISK_INFO_RESPONSE));
		if (!NT_SUCCESS(status))
		{
            LOG_PARSER(LL_ERROR, "Failed to retreive size info. 0x%0X\n", status);
			return status;
		}
		pRes->dwSectorSize = Response.vals[2].dwHigh;
		pRes->qwDiskSize = Response.vals[0].qword;
		pRes->dwNumSectors = parser->dwNumSectors;
		*pBufferSize = sizeof(DISK_INFO_GEOMETRY);
	}
	else
	{
        LOG_PARSER(LL_ERROR, "Unknown Disk info type %X\n", type);
		status = STATUS_INVALID_DEVICE_REQUEST;
	}
    TRACE_FUNCTION_OUT_STATUS(status);

	return status;
}

NTSTATUS EVhd_QuerySaveVersionDisk(PVOID pContext, INT *pVersion)
{
	UNREFERENCED_PARAMETER(pContext);
	*pVersion = 1;
	return STATUS_SUCCESS;
}

/** Pause VM */
NTSTATUS EVhd_SaveDisk(PVOID pContext, PVOID data, ULONG32 size, ULONG32 *dataStored)
{
    TRACE_FUNCTION_IN();
    NTSTATUS status = STATUS_SUCCESS;
    ParserInstance *parser = pContext;
	ULONG32 dwSize = parser->dwDiskSaveSize + sizeof(PARSER_STATE);
    if (dwSize < parser->dwDiskSaveSize) {
        status = STATUS_INTEGER_OVERFLOW;
    }
    else if (dwSize > size) {
        status = STATUS_BUFFER_TOO_SMALL;
    }
    else {
        parser->Io.pfnSaveData(parser->Io.pIoInterface, (UCHAR *)data + sizeof(PARSER_STATE), parser->dwDiskSaveSize);
        *dataStored = dwSize;
    }
    TRACE_FUNCTION_OUT_STATUS(status);
    return status;
}

/** Resume VM */
NTSTATUS EVhd_RestoreDisk(PVOID pContext, INT revision, PVOID data, ULONG32 size)
{
    TRACE_FUNCTION_IN();
    NTSTATUS status = STATUS_SUCCESS;
    ParserInstance *parser = pContext;
    ULONG32 dwSize = parser->dwDiskSaveSize + sizeof(PARSER_STATE);
    if (revision != 1) {	 // Assigned by IOCTL_VIRTUAL_DISK_SET_SAVE_VERSION
        status = STATUS_REVISION_MISMATCH;
    }
    else if (dwSize < parser->dwDiskSaveSize) {
        status = STATUS_INTEGER_OVERFLOW;
    }
    else if (dwSize > size) {
        status = STATUS_INVALID_BUFFER_SIZE;
    }
    else {
        status = parser->Io.pfnRestoreData(parser->Io.pIoInterface, (UCHAR *)data + 0x20, parser->dwDiskSaveSize);
    }
    TRACE_FUNCTION_OUT_STATUS(status);
	return status;
}

NTSTATUS EVhd_SetBehaviourDisk(PVOID pContext, INT behaviour)
{
    TRACE_FUNCTION_IN();
    NTSTATUS status = STATUS_SUCCESS;
    ParserInstance *parser = pContext;
	INT IoControl;
	if (behaviour == 1)
		IoControl = IOCTL_STORAGE_VHD_ISO_EJECT_MEDIA;
	else if (behaviour == 2)
		IoControl = IOCTL_STORAGE_VHD_ISO_INSERT_MEDIA;
	else
		return STATUS_INVALID_DEVICE_REQUEST;

	status = SynchronouseCall(parser->pVhdmpFileObject, IoControl, NULL, 0, NULL, 0);
    TRACE_FUNCTION_OUT_STATUS(status);
    return status;
}

/** Set Qos interface configuration */
NTSTATUS EVhd_SetQosConfigurationDisk(PVOID pContext, PVOID pConfig)
{
    TRACE_FUNCTION_IN();
    NTSTATUS status = STATUS_SUCCESS;
    ParserInstance *parser = pContext;
	if (parser->Qos.pfnSetQosConfiguration)
		status = parser->Qos.pfnSetQosConfiguration(parser->Qos.pQosInterface, pConfig);
	else
		status = STATUS_INVALID_DEVICE_REQUEST;
    TRACE_FUNCTION_OUT_STATUS(status);
    return status;
}

/** Unimplemented on R2 */
NTSTATUS EVhd_GetQosInformationDisk(PVOID pContext, PVOID pInfo)
{
    TRACE_FUNCTION_IN();
    NTSTATUS status = STATUS_SUCCESS;
    ParserInstance *parser = pContext;
	if (parser->Qos.pfnGetQosInformation)
		status = parser->Qos.pfnGetQosInformation(parser->Qos.pQosInterface, pInfo);
	else
		status = STATUS_INVALID_DEVICE_REQUEST;
    return status;
}
