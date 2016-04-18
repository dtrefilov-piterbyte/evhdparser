#include "stdafx.h"
#include "Log.h"
#include "parser.h"
#include "Guids.h"
#include "Ioctl.h"
#include "Vdrvroot.h"	 
#include "utils.h"

#define LOG_PARSER(level, format, ...) LOG_FUNCTION(level, LOG_CTG_PARSER, format, __VA_ARGS__)

static const ULONG EvhdPoolTag = 'VVpp';

static NTSTATUS EVhd_Initialize(SrbCallbackInfo *callbackInfo, ULONG32 dwFlags, HANDLE hFileHandle, PFILE_OBJECT pFileObject, ParserInstance *parser)
{
    TRACE_FUNCTION_IN();
	NTSTATUS status = STATUS_SUCCESS;
	DiskInfoResponse resp = { 0 };
	EDiskInfoType req = EDiskInfoType_Geometry;

	if (0 == (dwFlags & 0x80000))
		parser->bSuspendable = TRUE;

	KeInitializeSpinLock(&parser->SpinLock);

	status = SynchronouseCall(pFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &req, sizeof(req),
		&resp, sizeof(DiskInfoResponse));

	if (!NT_SUCCESS(status))
	{
        LOG_PARSER(LL_FATAL, "SynchronouseCall IOCTL_STORAGE_VHD_GET_INFORMATION failed with error 0x%0x\n", status);
	}
	else
	{
		parser->dwSectorSize = resp.vals[2].dwHigh;
		parser->dwNumSectors = resp.vals[2].dwLow;
		parser->qwDiskSize = resp.vals[0].qword;

		parser->pfnVstorSrbSave = callbackInfo->pfnVstorSrbSave;
		parser->pfnVstorSrbRestore = callbackInfo->pfnVstorSrbRestore;
		parser->pfnVstorSrbPrepare = callbackInfo->pfnVstorSrbPrepare;

        if (!parser->bSuspendable)
		{
			parser->pVhdmpFileObject = pFileObject;
			parser->FileHandle = hFileHandle;
			pFileObject = NULL;
		}
	}

	if (pFileObject)
		ObDereferenceObject(pFileObject);

    TRACE_FUNCTION_OUT_STATUS(status);
	return status;
}

static VOID EVhd_Finalize(ParserInstance *parser)
{
    TRACE_FUNCTION_IN();
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
    TRACE_FUNCTION_OUT();
}

static NTSTATUS EVhd_SrbPrepare(PVOID pOuterInterface, SCSI_PACKET *pPacket,
	SCSI_PREPARE_REQUEST *pVstorRequest, PVOID pfnCallDriver, PVOID arg1, PVOID arg2)
{
    ParserInstance *parser = pPacket->pContext;
	memset(pVstorRequest, 0, sizeof(SCSI_PREPARE_REQUEST));
	pVstorRequest->pLinkedUnk = pPacket->pLinkedUnknown;
	pVstorRequest->pOuterInterface = pOuterInterface;
	pVstorRequest->pfnCallDriver = pfnCallDriver;
	return parser->pfnVstorSrbPrepare(pVstorRequest, arg1, arg2);
}

static VOID EVhd_PostProcessSrbPacket(SCSI_PACKET *pPacket, NTSTATUS status);
static NTSTATUS EVhd_SrbCompleteRequest(SCSI_PACKET *pPacket, NTSTATUS status)
{
	EVhd_PostProcessSrbPacket(pPacket, status);
	return pPacket->pfnCompleteSrbRequest(pPacket, status);
}

static NTSTATUS EVhd_SrbSave(ParserInstance *parser, PVOID arg1, PVOID arg2)
{
    NTSTATUS status = STATUS_SUCCESS;
    TRACE_FUNCTION_IN();
	status = parser->pfnVstorSrbSave(arg1, arg2);
    TRACE_FUNCTION_OUT_STATUS(status);
    return status;
}

static NTSTATUS EVhd_SrbRestore(ParserInstance *parser, PVOID arg)
{
    NTSTATUS status = STATUS_SUCCESS;
    TRACE_FUNCTION_IN();
	status = parser->pfnVstorSrbRestore(arg, __readcr8() >= 2);	// task priority class
    TRACE_FUNCTION_OUT_STATUS(status);
    return status;
}

static NTSTATUS EVhd_RegisterQoS(ParserInstance *parser)
{
    TRACE_FUNCTION_IN();
    REGISTER_QOS_REQUEST Request = { 0 };
    REGISTER_QOS_RESPONSE Response = { 0 };
	NTSTATUS status = STATUS_SUCCESS;

	if (!parser->bQoSRegistered)
	{
		Request.pContext = parser;
		Request.pfnSrbPrepare = EVhd_SrbPrepare;
		Request.pfnSrbCompleteRequest = EVhd_SrbCompleteRequest;
		Request.pfnSrbSave = EVhd_SrbSave;
		Request.pfnSrbRestore = EVhd_SrbRestore;

		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_REGISTER_QOS_INTERFACE,
            &Request, sizeof(REGISTER_QOS_REQUEST), &Response, sizeof(REGISTER_QOS_RESPONSE));
		if (NT_SUCCESS(status))
		{
			parser->QoS.pIoInterface = Response.pIoInterface;
			parser->QoS.pfnStartIo = Response.pfnStartIo;
			parser->QoS.dwDiskSaveSize = Response.dwDiskSaveSize;
			parser->QoS.pfnSaveData = Response.pfnSaveData;
			parser->QoS.pfnRestoreData = Response.pfnRestoreData;
			parser->bQoSRegistered = TRUE;
		}
	}
    TRACE_FUNCTION_OUT_STATUS(status);

	return status;
}

static NTSTATUS EVhd_UnregisterQoS(ParserInstance *parser)
{
    TRACE_FUNCTION_IN();
	NTSTATUS status = STATUS_SUCCESS;
	if (parser->bQoSRegistered)
	{
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_UNREGISTER_QOS_INTERFACE, NULL, 0, NULL, 0);
		if (NT_SUCCESS(status))
		{
			memset(&parser->QoS, 0, sizeof(QoSInfo));
			parser->bQoSRegistered = FALSE;
		}
	}
    TRACE_FUNCTION_OUT_STATUS(status);
	return status;
}

static NTSTATUS EVhd_RegisterIo(ParserInstance *parser, BOOLEAN bFlag)
{
    TRACE_FUNCTION_IN();
    REGISTER_IO_REQUEST Request = { 0 };
	NTSTATUS status = STATUS_SUCCESS;
	SCSI_ADDRESS scsiAddressResponse = { 0 };

	status = EVhd_RegisterQoS(parser);
    if (!NT_SUCCESS(status))
    {
        LOG_PARSER(LL_FATAL, "EvhdRegisterQoS failed with error 0x%08X\n", status);
        goto Cleanup;
    }
    if (!parser->bIoRegistered)
    {
        Request.dwVersion = 1;
        Request.dwFlags = bFlag ? 0x9 : 0x8;
        status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_REGISTER_IO,
            &Request, sizeof(REGISTER_IO_REQUEST), NULL, 0);

        if (!NT_SUCCESS(status))
        {
            LOG_PARSER(LL_FATAL, "IOCTL_STORAGE_REGISTER_IO failed with error 0x % 08X\n", status);
            goto Cleanup;
        }
        parser->bIoRegistered = TRUE;
    }

    status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_SCSI_GET_ADDRESS, NULL, 0, &scsiAddressResponse, sizeof(SCSI_ADDRESS));

    if (!NT_SUCCESS(status))
    {
        LOG_PARSER(LL_FATAL, "IOCTL_SCSI_GET_ADDRESS failed with error 0x % 0X\n", status);
        goto Cleanup;
    }

    parser->ScsiLun = scsiAddressResponse.Lun;
    parser->ScsiPathId = scsiAddressResponse.PathId;
    parser->ScsiTargetId = scsiAddressResponse.TargetId;
Cleanup:

	return status;
}

static NTSTATUS EVhd_UnregisterIo(ParserInstance *parser)
{
	NTSTATUS status = STATUS_SUCCESS;
    TRACE_FUNCTION_IN();
	if (parser->bIoRegistered)
	{
        UNREGISTER_IO_REQUEST Request = { 1 };

		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_REMOVE_VIRTUAL_DISK,
            &Request, sizeof(UNREGISTER_IO_REQUEST), NULL, 0);
		if (!NT_SUCCESS(status))
		{
            LOG_PARSER(LL_FATAL, "IOCTL_STORAGE_REMOVE_VIRTUAL_DISK failed with error 0x%0X\n", status);
            goto Cleanup;
		}
		parser->bIoRegistered = FALSE;
	}
	status = EVhd_UnregisterQoS(parser);
Cleanup:
    TRACE_FUNCTION_OUT_STATUS(status);
	return status;
}

static VOID EVhd_PostProcessSrbPacket(SCSI_PACKET *pPacket, NTSTATUS status)
{
	STORVSP_REQUEST *pVspRequest = pPacket->pVspRequest;
	STORVSC_REQUEST *pVscRequest = pPacket->pVscRequest;

    pVscRequest->SrbStatus = pVspRequest->Srb.SrbStatus;
    pVscRequest->ScsiStatus = pVspRequest->Srb.ScsiStatus;
    pVscRequest->SenseInfoBufferLength = pVspRequest->Srb.SenseInfoBufferLength;
    pVscRequest->DataTransferLength = pVspRequest->Srb.DataTransferLength;
	if (NT_SUCCESS(pPacket->Status = status))
        pPacket->DataTransferLength = pVspRequest->Srb.DataTransferLength;
	else
		pPacket->DataTransferLength = 0;
}

static NTSTATUS EVhd_SrbInitializeInner(SCSI_PACKET *pPacket)
{
    NTSTATUS status = STATUS_SUCCESS;
    STORVSP_REQUEST *pVspRequest = pPacket->pVspRequest;
    STORVSC_REQUEST *pVscRequest = pPacket->pVscRequest;
	PMDL pMdl = pPacket->pMdl;
	memset(&pVspRequest->Srb, 0, SCSI_REQUEST_BLOCK_SIZE);
    pVspRequest->Srb.Length = SCSI_REQUEST_BLOCK_SIZE;
    pVspRequest->Srb.SrbStatus = pVscRequest->SrbStatus;
    pVspRequest->Srb.ScsiStatus = pVscRequest->ScsiStatus;
    pVspRequest->Srb.PathId = pVscRequest->ScsiPathId;
    pVspRequest->Srb.TargetId = pVscRequest->ScsiTargetId;
    pVspRequest->Srb.Lun = pVscRequest->ScsiLun;
    pVspRequest->Srb.CdbLength = pVscRequest->CdbLength;
    pVspRequest->Srb.SenseInfoBufferLength = pVscRequest->SenseInfoBufferLength;
    pVspRequest->Srb.SrbFlags = pVscRequest->bDataIn ? SRB_FLAGS_DATA_IN : SRB_FLAGS_DATA_OUT;
    pVspRequest->Srb.DataTransferLength = pVscRequest->DataTransferLength;
    pVspRequest->Srb.SrbFlags |= pVscRequest->SrbFlags & 8000;			  // Non-standard
	
    // align 0x10
    pVspRequest->Srb.SrbExtension = (PVOID)(((INT_PTR)(pVspRequest + 1) + 0xF) & ~0xF);	// variable-length extension right after the inner request block
    pVspRequest->Srb.SenseInfoBuffer = &pVscRequest->Sense;
    memmove(pVspRequest->Srb.Cdb, &pVscRequest->Sense, pVscRequest->CdbLength);

    switch ((UCHAR)pVscRequest->Sense.Cdb6.OpCode)
	{
	default:
		if (pMdl)
        {
            pVspRequest->Srb.DataBuffer = MmGetSystemAddressForMdlSafe(pMdl, NormalPagePriority);
            if (!pVspRequest->Srb.DataBuffer)
			{
				status = STATUS_INSUFFICIENT_RESOURCES;
				break;
			}
		}
        break;
	case SCSI_OP_CODE_READ_6:
	case SCSI_OP_CODE_READ_10:
	case SCSI_OP_CODE_READ_12:
	case SCSI_OP_CODE_READ_16:
	case SCSI_OP_CODE_WRITE_6:
	case SCSI_OP_CODE_WRITE_10:
	case SCSI_OP_CODE_WRITE_12:
	case SCSI_OP_CODE_WRITE_16:
		break;
	}
	return status;
}

NTSTATUS EVhd_GetFinalSaveSize(ULONG32 dwDiskSaveSize, ULONG32 dwHeaderSize, ULONG32 *pFinalSaveSize)
{
	ULONG32 dwSize = dwDiskSaveSize + dwHeaderSize;
	if (dwSize < dwDiskSaveSize)
	{
		*pFinalSaveSize = MAXULONG32;
		return STATUS_INTEGER_OVERFLOW;
	}
	*pFinalSaveSize = dwSize;
	return STATUS_SUCCESS;
}


NTSTATUS EVhd_Init(SrbCallbackInfo *callbackInfo, PCUNICODE_STRING diskPath, ULONG32 OpenFlags, void **pInOutParam)
{
    TRACE_FUNCTION_IN();
	NTSTATUS status = STATUS_SUCCESS;
	PFILE_OBJECT pFileObject = NULL;
	HANDLE FileHandle = NULL;
	ParserInstance *parser = NULL;
	ResiliencyInfoEa *pResiliency = (ResiliencyInfoEa *)*pInOutParam;
	ULONG32 dwFlags = OpenFlags ? ((OpenFlags & 0x80000000) ? 0xD0000 : 0x3B0000) : 0x80000;

	status = OpenVhdmpDevice(&FileHandle, OpenFlags, &pFileObject, diskPath, pResiliency);
	if (!NT_SUCCESS(status))
	{
        LOG_PARSER(LL_FATAL, "Failed to open vhdmp device for virtual disk file %S\n", diskPath->Buffer);
		goto failure_cleanup;
	}

	parser = (ParserInstance *)ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(ParserInstance), EvhdPoolTag);
	if (!parser)
	{
        LOG_PARSER(LL_FATAL, "Failed to allocate memory for ParserInstance\n");
		status = STATUS_NO_MEMORY;
		goto failure_cleanup;
	}

	memset(parser, 0, sizeof(ParserInstance));

	status = EVhd_Initialize(callbackInfo, dwFlags, FileHandle, pFileObject, parser);

	if (!NT_SUCCESS(status))
	{
		status = STATUS_INVALID_PARAMETER;
		goto failure_cleanup;
	}

	*pInOutParam = parser;

	goto cleanup;

failure_cleanup:
	if (FileHandle)
		ZwClose(FileHandle);
	if (parser)
	{
		EVhd_Cleanup(parser);
	}

cleanup:
    TRACE_FUNCTION_OUT_STATUS(status);
	return status;
}

VOID EVhd_Cleanup(ParserInstance *parser)
{
    TRACE_FUNCTION_IN();
	EVhd_Mount(parser, FALSE, FALSE);
	EVhd_Finalize(parser);
	ExFreePoolWithTag(parser, EvhdPoolTag);
    TRACE_FUNCTION_OUT();
}

VOID EVhd_GetGeometry(ParserInstance *parser, ULONG32 *pSectorSize, ULONG64 *pDiskSize, ULONG32 *pNumSectors)
{
    TRACE_FUNCTION_IN();
	if (parser)
	{
		if (pSectorSize) *pSectorSize = parser->dwSectorSize;
		if (pDiskSize) *pDiskSize = parser->qwDiskSize;
		if (pNumSectors) *pNumSectors = parser->dwNumSectors;
	}
    TRACE_FUNCTION_OUT();
}

VOID EVhd_GetCapabilities(ParserInstance *parser, PPARSER_CAPABILITIES pCapabilities)
{
    TRACE_FUNCTION_IN();
	if (parser && pCapabilities)
	{
		pCapabilities->dwUnk1 = 7;
		pCapabilities->dwUnk2 = 0;
		pCapabilities->bMounted = parser->bMounted;
	}
    TRACE_FUNCTION_OUT();
}

NTSTATUS EVhd_Mount(ParserInstance *parser, BOOLEAN bMountDismount, BOOLEAN registerIoFlag)
{
    TRACE_FUNCTION_IN();
	NTSTATUS status = STATUS_SUCCESS;

	if (parser->bMounted == bMountDismount)
	{
        LOG_PARSER(LL_FATAL, "Already in a target state");
		return STATUS_INVALID_DEVICE_STATE;
	}

	if (bMountDismount)
	{
		status = EVhd_RegisterIo(parser, registerIoFlag);
		if (!NT_SUCCESS(status))
			EVhd_UnregisterIo(parser);
		else
			parser->bMounted = TRUE;
	}
	else
	{
		status = EVhd_UnregisterIo(parser);
		parser->bMounted = FALSE;
	}
    TRACE_FUNCTION_OUT_STATUS(status);
	return status;
}

NTSTATUS EVhd_ExecuteSrb(SCSI_PACKET *pPacket)
{
	NTSTATUS status = STATUS_SUCCESS;
    ParserInstance *parser = NULL;
    STORVSP_REQUEST *pVspRequest = pPacket->pVspRequest;
    STORVSC_REQUEST *pVscRequest = pPacket->pVscRequest;
	
	if (!pPacket)
		return STATUS_INVALID_PARAMETER;
	parser = pPacket->pContext;
	if (!parser)
		return STATUS_INVALID_PARAMETER;
	if (parser->bSuspendable)
	{
        LOG_PARSER(LL_FATAL, "Requested asynchronouse Io for synchronouse parser instance");
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	if (!parser->QoS.pfnStartIo)
	{
        LOG_PARSER(LL_FATAL, "Backing store doesn't provide a valid asynchronouse Io");
		return STATUS_INVALID_DEVICE_REQUEST;
	}
    if (!pVspRequest)
		return STATUS_INVALID_PARAMETER;
    memset(pVspRequest, 0, sizeof(STORVSP_REQUEST));
	status = EVhd_SrbInitializeInner(pPacket);
	if (NT_SUCCESS(status))
        status = parser->QoS.pfnStartIo(parser->QoS.pIoInterface, pPacket, pVspRequest, pPacket->pMdl);
	else
		pVscRequest->SrbStatus = SRB_STATUS_INTERNAL_ERROR;

	if (STATUS_PENDING != status)
		EVhd_PostProcessSrbPacket(pPacket, status);

	return status;
}

NTSTATUS EVhd_BeginSave(ParserInstance *parser, PPARSER_SAVE_STATE_INFO pSaveInfo)
{
    TRACE_FUNCTION_IN();
	ULONG32 dwFinalSize = 0;
	NTSTATUS status = STATUS_SUCCESS;
    if (!pSaveInfo)
    {
        status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }
	status = EVhd_GetFinalSaveSize(parser->QoS.dwDiskSaveSize, sizeof(PARSER_STATE), &dwFinalSize);
    if (!NT_SUCCESS(status))
    {
        goto Cleanup;
    }
	pSaveInfo->dwVersion = 1;
	pSaveInfo->dwFinalSize = dwFinalSize;
Cleanup:
    TRACE_FUNCTION_OUT_STATUS(status);

	return status;
}

NTSTATUS EVhd_SaveData(ParserInstance *parser, PVOID pData, ULONG32 *pSize)
{
    TRACE_FUNCTION_IN();
    ULONG32 dwFinalSize = 0;
    NTSTATUS status = STATUS_SUCCESS;
    KIRQL oldIrql = 0;
    if (!parser || !pData || !pSize)
    {
        status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }
	status = EVhd_GetFinalSaveSize(parser->QoS.dwDiskSaveSize, sizeof(PARSER_STATE), &dwFinalSize);
    if (!NT_SUCCESS(status))
    {
        goto Cleanup;
    }
    if (dwFinalSize <= *pSize)
    {
        status = STATUS_BUFFER_TOO_SMALL;
        goto Cleanup;
    }
	if (!parser->bSuspendable)
	{
		status = STATUS_INVALID_DEVICE_REQUEST;
        goto Cleanup;
	}
	PPARSER_STATE pState = pData;
	status = parser->QoS.pfnSaveData(parser->QoS.pIoInterface, pState + 1, parser->QoS.dwDiskSaveSize);
	if (NT_SUCCESS(status))
	{
		KeAcquireSpinLock(&parser->SpinLock, &oldIrql);
		*pState = parser->State;
		KeReleaseSpinLock(&parser->SpinLock, oldIrql);
	}
Cleanup:
    TRACE_FUNCTION_OUT_STATUS(status);
	return status;

}

NTSTATUS EVhd_BeginRestore(ParserInstance *parser, PPARSER_SAVE_STATE_INFO pSaveInfo)
{
	UNREFERENCED_PARAMETER(parser);
    TRACE_FUNCTION_IN();
	if (!pSaveInfo || pSaveInfo->dwVersion != 1)
		return STATUS_INVALID_PARAMETER;
    TRACE_FUNCTION_OUT();
	return STATUS_SUCCESS;
}

NTSTATUS EVhd_RestoreData(ParserInstance *parser, PVOID pData, ULONG32 size)
{
    TRACE_FUNCTION_IN();
	ULONG32 dwFinalSize = 0;
	NTSTATUS status = STATUS_SUCCESS;
	KIRQL oldIrql = 0;
    if (!parser || !pData)
    {
        status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }
	status = EVhd_GetFinalSaveSize(parser->QoS.dwDiskSaveSize, sizeof(PARSER_STATE), &dwFinalSize);
    if (!NT_SUCCESS(status))
    {
        goto Cleanup;
    }
    if (dwFinalSize <= size)
    {
        goto Cleanup;
    }
	if (!parser->bSuspendable)
	{
		status = STATUS_INVALID_DEVICE_REQUEST;
        goto Cleanup;
	}
	status = parser->QoS.pfnRestoreData(parser->QoS.pIoInterface, (PUCHAR)pData + sizeof(PARSER_STATE),
		parser->QoS.dwDiskSaveSize);
	if (NT_SUCCESS(status))
	{
		oldIrql = KeAcquireSpinLockRaiseToDpc(&parser->SpinLock);
		parser->State = *(PPARSER_STATE)pData;
		KeReleaseSpinLock(&parser->SpinLock, oldIrql);
	}
Cleanup:
    TRACE_FUNCTION_OUT_STATUS(status);
	return status;
}

NTSTATUS EVhd_SetCacheState(ParserInstance *parser, BOOLEAN newState)
{
	if (!parser)
		return STATUS_INVALID_PARAMETER;
	parser->State.bCache = newState;
	return STATUS_SUCCESS;
}
