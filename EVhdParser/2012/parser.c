#include "parser.h"
#include <initguid.h>
#include "Guids.h"
#include "Ioctl.h"
#include "Vdrvroot.h"	 
#include "utils.h"

static const ULONG EvhdPoolTag = 0x70705656;	// 'VVpp'

static NTSTATUS EvhdInitialize(SrbCallbackInfo *callbackInfo, ULONG32 dwFlags, HANDLE hFileHandle, PFILE_OBJECT pFileObject, ParserInstance *parser)
{
	NTSTATUS status = STATUS_SUCCESS;
	MetaInfoResponse resp = { 0 };
	EDiskInfoType req = EDiskInfoType_Geometry;

	// Never occurs for any input data
	if (0 == (dwFlags & 0x80000))
		parser->bSynchronouseIo = TRUE;

	KeInitializeSpinLock(&parser->SpinLock);

	status = SynchronouseCall(pFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &req, sizeof(req),
		&resp, sizeof(MetaInfoResponse));

	if (!NT_SUCCESS(status))
	{
		DEBUG("SynchronouseCall IOCTL_STORAGE_VHD_GET_INFORMATION failed with error 0x%0x\n", status);
	}
	else
	{
		parser->dwSectorSize = resp.vals[2].dwHigh;
		parser->dwNumSectors = resp.vals[2].dwLow;
		parser->qwDiskSize = resp.vals[0].qword;

		parser->pfnVstorSrbSave = callbackInfo->pfnVstorSrbSave;
		parser->pfnVstorSrbRestore = callbackInfo->pfnVstorSrbRestore;
		parser->pfnVstorSrbPrepare = callbackInfo->pfnVstorSrbPrepare;

		if (!parser->bSynchronouseIo)
		{
			parser->pVhdmpFileObject = pFileObject;
			parser->FileHandle = hFileHandle;
			pFileObject = NULL;
		}
	}

	if (pFileObject)
		ObDereferenceObject(pFileObject);


	return status;
}

static VOID EvhdFinalize(ParserInstance *parser)
{
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

static NTSTATUS EvhdSrbPrepare(PVOID pOuterInterface, SrbPacket *pPacket,
	VstorSrbPrepareRequest *pVstorRequest, PVOID pfnCallDriver, PVOID arg1, PVOID arg2)
{
	memset(pVstorRequest, 0, sizeof(VstorSrbPrepareRequest));
	pVstorRequest->pLinkedUnk = pPacket->pLinkedUnknown;
	pVstorRequest->pOuterInterface = pOuterInterface;
	pVstorRequest->pfnCallDriver = pfnCallDriver;
	return pPacket->pParser->pfnVstorSrbPrepare(pVstorRequest, arg1, arg2);
}

static VOID EvhdPostProcessSrbPacket(SrbPacket *pPacket, NTSTATUS status);
static NTSTATUS EvhdSrbCompleteRequest(SrbPacket *pPacket, NTSTATUS status)
{
	EvhdPostProcessSrbPacket(pPacket, status);
	return pPacket->pfnCompleteSrbRequest(pPacket, status);
}

/** Used for synchronouse IO */
static NTSTATUS EvhdSrbSave(ParserInstance *parser, PVOID arg1, PVOID arg2)
{
	return parser->pfnVstorSrbSave(arg1, arg2);
}

/** Used for synchronouse IO */
static NTSTATUS EvhdSrbRestore(ParserInstance *parser, PVOID arg)
{
	return parser->pfnVstorSrbRestore(arg, __readcr8() >= 2);	// task priority class
}

static NTSTATUS EvhdRegisterQoS(ParserInstance *parser)
{
#pragma pack(push, 1)
	struct {
		ParserInstance			*pParser;
		VhdPrepare_t			pfnSrbPrepare;
		VhdCompleteRequest_t	pfnSrbCompleteRequest;
		VhdSaveData_t			pfnSrbSave;
		VhdRestoreData_t		pfnSrbRestore;
	} req = { 0 };
	struct {
		ULONG32					dwDiskSaveSize;
		ULONG32					dwUnk;
		PVOID					pIoInterface;
		SrbStartIo_t			pfnStartIo;
		SrbSaveData_t			pfnSaveData;
		SrbRestoreData_t		pfnRestoreData;
	} resp = { 0 };
#pragma pack(pop)
	NTSTATUS status = STATUS_SUCCESS;

	if (!parser->bQoSRegistered)
	{

		req.pParser = parser;
		req.pfnSrbPrepare = EvhdSrbPrepare;
		req.pfnSrbCompleteRequest = EvhdSrbCompleteRequest;
		req.pfnSrbSave = EvhdSrbSave;
		req.pfnSrbRestore = EvhdSrbRestore;

		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_REGISTER_QOS_INTERFACE, &req, sizeof(req), &resp, sizeof(resp));
		if (NT_SUCCESS(status))
		{
			parser->QoS.pIoInterface = resp.pIoInterface;
			parser->QoS.pfnStartIo = resp.pfnStartIo;
			parser->QoS.dwDiskSaveSize = resp.dwDiskSaveSize;
			parser->QoS.pfnSaveData = resp.pfnSaveData;
			parser->QoS.pfnRestoreData = resp.pfnRestoreData;
			parser->bQoSRegistered = TRUE;
		}
	}

	return status;
}

static NTSTATUS EvhdUnregisterQoS(ParserInstance *parser)
{
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
	return status;
}

static NTSTATUS EvhdRegisterIo(ParserInstance *parser, BOOLEAN bFlag)
{
#pragma pack(push, 1)
	typedef struct{
		INT dwVersion;
		INT dwFlags;
		UCHAR Unused[16];
	} RegisterIoRequest;
#pragma pack(pop)
	RegisterIoRequest req = { 0 };
	NTSTATUS status = STATUS_SUCCESS;
	SCSI_ADDRESS scsiAddressResponse = { 0 };

	status = EvhdRegisterQoS(parser);
	if (!NT_SUCCESS(status))
	{
		DEBUG("EvhdRegisterQoS failed with error 0x%08X\n", status);
		return status;
	}

	if (!parser->bIoRegistered)
	{

		req.dwVersion = 1;
		req.dwFlags = bFlag ? 0x9 : 0x8;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_REGISTER_IO, &req, sizeof(RegisterIoRequest), NULL, 0);

		if (!NT_SUCCESS(status))
		{
			DEBUG("IOCTL_STORAGE_REGISTER_IO failed with error 0x%08X\n", status);
			return status;
		}
		parser->bIoRegistered = TRUE;
	}

	status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_SCSI_GET_ADDRESS, NULL, 0, &scsiAddressResponse, sizeof(SCSI_ADDRESS));

	if (!NT_SUCCESS(status))
	{
		DEBUG("IOCTL_SCSI_GET_ADDRESS failed with error 0x%0X\n", status);
		return status;
	}

	parser->ScsiLun = scsiAddressResponse.Lun;
	parser->ScsiPathId = scsiAddressResponse.PathId;
	parser->ScsiTargetId = scsiAddressResponse.TargetId;

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

		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_REMOVE_VIRTUAL_DISK, &RemoveVhdRequest, sizeof(RemoveVhdRequest), NULL, 0);
		if (!NT_SUCCESS(status))
		{
			DEBUG("IOCTL_STORAGE_REMOVE_VIRTUAL_DISK failed with error 0x%0X\n", status);
			return status;
		}
		parser->bIoRegistered = FALSE;
	}
	status = EvhdUnregisterQoS(parser);
	return status;
}

static VOID EvhdPostProcessSrbPacket(SrbPacket *pPacket, NTSTATUS status)
{
	SrbPacketInnerRequest *pInner = pPacket->pInner;
	SrbPacketRequest *pRequest = pPacket->pRequest;

	pRequest->SrbStatus = pInner->Srb.SrbStatus;
	pRequest->ScsiStatus = pInner->Srb.ScsiStatus;
	pRequest->SenseInfoBufferLength = pInner->Srb.SenseInfoBufferLength;
	pRequest->DataTransferLength = pInner->Srb.DataTransferLength;
	if (NT_SUCCESS(pPacket->Status = status))
		pPacket->DataTransferLength = pInner->Srb.DataTransferLength;
	else
		pPacket->DataTransferLength = 0;
}

static NTSTATUS EvhdSrbInitializeInner(SrbPacket *pPacket)
{
	NTSTATUS status = STATUS_SUCCESS;
	SrbPacketInnerRequest *pInner = pPacket->pInner;
	SrbPacketRequest *pRequest = pPacket->pRequest;
	PMDL pMdl = pPacket->pMdl;
	memset(&pInner->Srb, 0, SCSI_REQUEST_BLOCK_SIZE);
	pInner->Srb.Length = SCSI_REQUEST_BLOCK_SIZE;
	pInner->Srb.SrbStatus = pRequest->SrbStatus;
	pInner->Srb.ScsiStatus = pRequest->ScsiStatus;
	pInner->Srb.PathId = pRequest->ScsiPathId;
	pInner->Srb.TargetId = pRequest->ScsiTargetId;
	pInner->Srb.Lun = pRequest->ScsiLun;
	pInner->Srb.CdbLength = pRequest->CdbLength;
	pInner->Srb.SenseInfoBufferLength = pRequest->SenseInfoBufferLength;
	pInner->Srb.SrbFlags = pRequest->bDataIn ? SRB_FLAGS_DATA_IN : SRB_FLAGS_DATA_OUT;
	pInner->Srb.DataTransferLength = pRequest->DataTransferLength;
	pInner->Srb.SrbFlags |= pRequest->SrbFlags & 8000;			  // Non-standard

	switch ((UCHAR)pRequest->Sense.Cdb6.OpCode)
	{
	default:
		if (pMdl)
		{
			pInner->Srb.DataBuffer = pMdl->MdlFlags & (MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL) ?
				pMdl->MappedSystemVa : MmMapLockedPagesSpecifyCache(pMdl, KernelMode, MmCached, NULL, FALSE, NormalPagePriority);
			if (!pInner->Srb.DataBuffer)
			{
				status = STATUS_INSUFFICIENT_RESOURCES;
				break;
			}
		}
		// fall through
	case SCSI_OP_CODE_READ_6:
	case SCSI_OP_CODE_READ_10:
	case SCSI_OP_CODE_READ_12:
	case SCSI_OP_CODE_READ_16:
	case SCSI_OP_CODE_WRITE_6:
	case SCSI_OP_CODE_WRITE_10:
	case SCSI_OP_CODE_WRITE_12:
	case SCSI_OP_CODE_WRITE_16:
		// align 0x10
		pInner->Srb.SrbExtension = (PVOID)(((INT_PTR)(pInner + 1) + 0xF) & ~0xF);	// variable-length extension right after the inner request block
		pInner->Srb.SenseInfoBuffer = &pRequest->Sense;
		memmove(pInner->Srb.Cdb, &pRequest->Sense, pRequest->CdbLength);
		break;
	}
	return status;
}

NTSTATUS EvhdGetFinalSaveSize(ULONG32 dwDiskSaveSize, ULONG32 dwHeaderSize, ULONG32 *pFinalSaveSize)
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


NTSTATUS EVhdInit(SrbCallbackInfo *callbackInfo, PCUNICODE_STRING diskPath, ULONG32 OpenFlags, void **pInOutParam)
{

	NTSTATUS status = STATUS_SUCCESS;
	PFILE_OBJECT pFileObject = NULL;
	HANDLE FileHandle = NULL;
	ParserInstance *parser = NULL;
	ResiliencyInfoEa *pResiliency = (ResiliencyInfoEa *)*pInOutParam;
	ULONG32 dwFlags = OpenFlags ? ((OpenFlags & 0x80000000) ? 0xD0000 : 0x3B0000) : 0x80000;

	status = OpenVhdmpDevice(&FileHandle, OpenFlags, &pFileObject, diskPath, pResiliency);
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

	status = EvhdInitialize(callbackInfo, dwFlags, FileHandle, pFileObject, parser);

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
		EVhdCleanup(parser);
	}

cleanup:
	return status;
}

VOID EVhdCleanup(ParserInstance *parser)
{
	EVhdMount(parser, FALSE, FALSE);
	EvhdFinalize(parser);
	ExFreePoolWithTag(parser, EvhdPoolTag);
}

VOID EVhdGetGeometry(ParserInstance *parser, ULONG32 *pSectorSize, ULONG64 *pDiskSize, ULONG32 *pNumSectors)
{
	if (parser)
	{
		if (pSectorSize) *pSectorSize = parser->dwSectorSize;
		if (pDiskSize) *pDiskSize = parser->qwDiskSize;
		if (pNumSectors) *pNumSectors = parser->dwNumSectors;
	}
}

VOID EVhdGetCapabilities(ParserInstance *parser, ParserCapabilities *pCapabilities)
{
	if (parser && pCapabilities)
	{
		pCapabilities->dwUnk1 = 7;
		pCapabilities->dwUnk2 = 0;
		pCapabilities->bMounted = parser->bMounted;
	}
}

NTSTATUS EVhdMount(ParserInstance *parser, BOOLEAN bMountDismount, BOOLEAN registerIoFlag)
{
	NTSTATUS status = STATUS_SUCCESS;

	if (parser->bMounted == bMountDismount)
	{
		DEBUG("EVhdMount invalid parser state");
		return STATUS_INVALID_DEVICE_STATE;
	}

	if (bMountDismount)
	{
		status = EvhdRegisterIo(parser, registerIoFlag);
		if (!NT_SUCCESS(status))
			EvhdUnregisterIo(parser);
		else
			parser->bMounted = TRUE;
	}
	else
	{
		status = EvhdUnregisterIo(parser);
		parser->bMounted = FALSE;
	}
	return status;
}

NTSTATUS EVhdExecuteSrb(SrbPacket *pPacket)
{
	NTSTATUS status = STATUS_SUCCESS;
	ParserInstance *parser = NULL;
	SrbPacketInnerRequest *pInner = NULL;
	SrbPacketRequest *pRequest = NULL;
	
	if (!pPacket)
		return STATUS_INVALID_PARAMETER;
	parser = pPacket->pParser;
	if (!parser)
		return STATUS_INVALID_PARAMETER;
	if (parser->bSynchronouseIo)
	{
		DEBUG("Requested asynchronouse Io for synchronouse parser instance");
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	if (!parser->QoS.pfnStartIo)
	{
		DEBUG("Backing store haven't provided a valid asynchronouse Io");
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	pInner = pPacket->pInner;
	if (!pInner)
		return STATUS_INVALID_PARAMETER;
	memset(pInner, 0, sizeof(SrbPacketInnerRequest));
	status = EvhdSrbInitializeInner(pPacket);
	if (NT_SUCCESS(status))
		status = parser->QoS.pfnStartIo(parser->QoS.pIoInterface, pPacket, pInner, pPacket->pMdl);
	else
		pRequest->SrbStatus = SRB_STATUS_INTERNAL_ERROR;

	if (STATUS_PENDING != status)
		EvhdPostProcessSrbPacket(pPacket, status);

	return status;
}

NTSTATUS EVhdBeginSave(ParserInstance *parser, SaveInfo *pSaveInfo)
{
	ULONG32 dwFinalSize = 0;
	NTSTATUS status = STATUS_SUCCESS;
	if (!pSaveInfo)
		return STATUS_INVALID_PARAMETER;
	status = EvhdGetFinalSaveSize(parser->QoS.dwDiskSaveSize, sizeof(SaveDataHeader), &dwFinalSize);
	if (!NT_SUCCESS(status))
		return status;
	pSaveInfo->dwVersion = 1;
	pSaveInfo->dwFinalSize = dwFinalSize;
	return status;
}

NTSTATUS EVhdSaveData(ParserInstance *parser, SaveDataHeader *pData, ULONG32 *pSize)
{
	ULONG32 dwFinalSize = 0;
	NTSTATUS status = STATUS_SUCCESS;
	KIRQL oldIrql = 0;
	if (!parser || !pData || !pSize)
		return STATUS_INVALID_PARAMETER;
	status = EvhdGetFinalSaveSize(parser->QoS.dwDiskSaveSize, sizeof(SaveDataHeader), &dwFinalSize);
	if (!NT_SUCCESS(status))
		return status;
	if (dwFinalSize <= *pSize)
		return STATUS_BUFFER_TOO_SMALL;
	if (!parser->bSynchronouseIo)
	{
		DEBUG("Requested asynchronouse Io for synchronouse parser instance");
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	status = parser->QoS.pfnSaveData(parser->QoS.pIoInterface, pData + 1, parser->QoS.dwDiskSaveSize);
	if (NT_SUCCESS(status))
	{
		oldIrql = KeAcquireSpinLockRaiseToDpc(&parser->SpinLock);
		pData->qwUnk1 = parser->qwUnk1;
		pData->qwUnk2 = parser->qwUnk2;
		pData->wUnk3 = parser->wUnk3;
		pData->bCacheState = parser->bCacheState;
		KeReleaseSpinLock(&parser->SpinLock, oldIrql);
	}
	return status;

}

NTSTATUS EVhdBeginRestore(ParserInstance *parser, const SaveInfo *pSaveInfo)
{
	UNREFERENCED_PARAMETER(parser);
	if (!pSaveInfo || pSaveInfo->dwVersion != 1)
		return STATUS_INVALID_PARAMETER;
	return STATUS_SUCCESS;
}

NTSTATUS EVhdRestoreData(ParserInstance *parser, const SaveDataHeader *pData, ULONG32 size)
{
	ULONG32 dwFinalSize = 0;
	NTSTATUS status = STATUS_SUCCESS;
	KIRQL oldIrql = 0;
	if (!parser || !pData)
		return STATUS_INVALID_PARAMETER;
	status = EvhdGetFinalSaveSize(parser->QoS.dwDiskSaveSize, sizeof(SaveDataHeader), &dwFinalSize);
	if (!NT_SUCCESS(status))
		return status;
	if (dwFinalSize <= size)
		return STATUS_INVALID_BUFFER_SIZE;
	if (!parser->bSynchronouseIo)
	{
		DEBUG("Requested asynchronouse Io for synchronouse parser instance");
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	status = parser->QoS.pfnRestoreData(parser->QoS.pIoInterface, pData + 1, parser->QoS.dwDiskSaveSize);
	if (NT_SUCCESS(status))
	{
		oldIrql = KeAcquireSpinLockRaiseToDpc(&parser->SpinLock);
		parser->qwUnk1 = pData->qwUnk1;
		parser->qwUnk2 = pData->qwUnk2;
		parser->wUnk3 = pData->wUnk3;
		parser->bCacheState = pData->bCacheState;
		KeReleaseSpinLock(&parser->SpinLock, oldIrql);
	}
	return status;
}

NTSTATUS EVhdSetCacheState(ParserInstance *parser, BOOLEAN newState)
{
	if (!parser)
		return STATUS_INVALID_PARAMETER;
	parser->bCacheState = newState;
	return STATUS_SUCCESS;
}
