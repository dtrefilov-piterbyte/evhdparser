#include "parser.h"
#include "Ioctl.h"
#include "utils.h"
#include <initguid.h>
#include <ntddscsi.h>
#include "Vdrvroot.h"
#include "Guids.h"
#include "Gost89Cipher.h"


static const ULONG EvhdPoolTag = 'VVpp';

static NTSTATUS EvhdInitCipher(ParserInstance *parser, PCUNICODE_STRING diskPath)
{
	UNREFERENCED_PARAMETER(diskPath);
	NTSTATUS status = STATUS_SUCCESS;
	MetaInfoResponse Response = { 0 };
	EMetaInfoType Request = EMetaInfoType_ParserInfo;
	EDiskFormat DiskFormat = EDiskFormat_Unknown;
	status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
		&Response, sizeof(MetaInfoResponse));
	if (!NT_SUCCESS(status))
	{
		DEBUG("Failed to retrieve parser info. 0x%0X\n", status);
		return status;
	}
	DiskFormat = Response.vals[0].dwLow;

	// IsoParser is not supported
	if (EDiskFormat_Vhd != DiskFormat && EDiskFormat_Vhdx != DiskFormat)
	{
		return status;
	}

	Request = EMetaInfoType_Page83Data;
	status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
		&Response, sizeof(MetaInfoResponse));
	if (!NT_SUCCESS(status))
	{
		DEBUG("Failed to retrieve virtual disk identifier. 0x%0X\n", status);
		return status;
	}
	status = CipherEngineGet(&Response.guid, &parser->pCipherEngine, &parser->pCipherCtx);
	return status;
}

static NTSTATUS EvhdFinalizeCipher(ParserInstance *parser)
{
	NTSTATUS status = STATUS_SUCCESS;
	if (parser->pCipherCtx)
	{
		status = parser->pCipherEngine->pfnDestroy(parser->pCipherCtx);
		if (NT_SUCCESS(status))
			parser->pCipherCtx = NULL;
	}
	return status;
}

static PMDL AllocateInnerMdl(PMDL pSourceMdl)
{
	PHYSICAL_ADDRESS LowAddress, HighAddress, SkipBytes;
	LowAddress.QuadPart = 0;
	HighAddress.QuadPart = 0xFFFFFFFFFFFFFFFF;
	SkipBytes.QuadPart = 0;

	PMDL pNewMdl = MmAllocatePagesForMdlEx(LowAddress, HighAddress, SkipBytes, pSourceMdl->ByteCount, MmCached, MM_DONT_ZERO_ALLOCATION);
	pNewMdl->Next = pSourceMdl;

	return pNewMdl;
}

static PMDL FreeInnerMdl(PMDL pMdl)
{
	PMDL pSourceMdl = pMdl->Next;
	pMdl->Next = NULL;
	MmFreePagesFromMdl(pMdl);
	return pSourceMdl;
}

static NTSTATUS EvhdCryptBlocks(ParserInstance *parser, PMDL pSourceMdl, PMDL pTargetMdl, SIZE_T size, BOOLEAN Encrypt)
{
	NTSTATUS status = STATUS_SUCCESS;
	BOOLEAN mappedSourceMdl = FALSE, mappedTargetMdl = FALSE;
	PVOID pSource = NULL, pTarget = NULL;
	// TODO: use sector size from vhdmp
	CONST SIZE_T SectorSize = 512;
	SIZE_T SectorOffset = 0;

	if (!parser || !pSourceMdl || !pTargetMdl)
		return STATUS_INVALID_PARAMETER;
	// have no cipher
	if (!parser->pCipherCtx)
		return STATUS_SUCCESS;

	mappedSourceMdl = 0 == (pSourceMdl->MdlFlags & (MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL));

	pSource = mappedSourceMdl ? MmMapLockedPagesSpecifyCache(pSourceMdl, KernelMode, MmCached, NULL, FALSE, NormalPagePriority)
		: pSourceMdl->MappedSystemVa;
	if (!pSource)
		return STATUS_INSUFFICIENT_RESOURCES;

	if (pSourceMdl != pTargetMdl)
	{
		mappedTargetMdl = 0 == (pTargetMdl->MdlFlags & (MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL));

		pTarget = mappedTargetMdl ? MmMapLockedPagesSpecifyCache(pTargetMdl, KernelMode, MmCached, NULL, FALSE, NormalPagePriority)
			: pTargetMdl->MappedSystemVa;
		if (!pTarget)
			return STATUS_INSUFFICIENT_RESOURCES;
	}
	else
	{
		pTarget = pSource;
	}

	ASSERT(0 == size % SectorSize);

	DEBUG("VHD: %s 0x%X bytes\n", Encrypt ? "Encrypting" : "Decrypting", size);

	for (SectorOffset = 0; SectorOffset < size; SectorOffset += SectorSize)
	{
		PUCHAR pSourceSector = (PUCHAR)pSource + SectorOffset;
		PUCHAR pTargetSector = (PUCHAR)pTarget + SectorOffset;
		status = (Encrypt ? parser->pCipherEngine->pfnEncrypt : parser->pCipherEngine->pfnDecrypt)(parser->pCipherCtx, 
			pSourceSector, pTargetSector, SectorSize);
		if (!NT_SUCCESS(status))
			break;
	}

	if (mappedSourceMdl && pSourceMdl)
		MmUnmapLockedPages(pSource, pSourceMdl);
	if (mappedTargetMdl && pTargetMdl)
		MmUnmapLockedPages(pTarget, pTargetMdl);
	return status;
}

static NTSTATUS EvhdInitialize(HANDLE hFileHandle, PFILE_OBJECT pFileObject, ParserInstance *parser)
{
	NTSTATUS status = STATUS_SUCCESS;
	MetaInfoResponse resp = { 0 };
	EMetaInfoType req = EMetaInfoType_NumSectors;

	parser->pVhdmpFileObject = pFileObject;
	parser->FileHandle = hFileHandle;
	parser->pIrp = IoAllocateIrp(IoGetRelatedDeviceObject(parser->pVhdmpFileObject)->StackSize, FALSE);
	if (!parser->pIrp)
	{
		DEBUG("IoAllocateIrp failed\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &req, sizeof(req),
		&resp, sizeof(MetaInfoResponse));

	if (!NT_SUCCESS(status))
	{
		DEBUG("SynchronouseCall IOCTL_STORAGE_VHD_GET_INFORMATION failed with error 0x%0x\n", status);
	}
	else
		parser->dwNumSectors = resp.vals[0].dwLow;

	return status;
}

static VOID EvhdFinalize(ParserInstance *parser)
{
	EvhdFinalizeCipher(parser);

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

static NTSTATUS EvhdRegisterQosInterface(ParserInstance *parser)
{
	NTSTATUS status = STATUS_SUCCESS;

	if (!parser->bQosRegistered)
	{
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_REGISTER_QOS_INTERFACE, NULL, 0,
			&parser->Qos, sizeof(QosInfo));
		parser->bQosRegistered = TRUE;
	}

	return status;
}

static NTSTATUS EvhdUnregisterQosInterface(ParserInstance *parser)
{
	NTSTATUS status = STATUS_SUCCESS;

	if (parser->bQosRegistered)
	{
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_UNREGISTER_QOS_INTERFACE, &parser->Qos, sizeof(QosInfo),
			&parser->Qos, sizeof(QosInfo));
		parser->bQosRegistered = FALSE;
	}
	return status;
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
		USHORT wNumSectors = RtlUshortByteSwap(*(USHORT *)&(pPacket->pInner->Srb.Cdb[7]));
		ULONG dwStartingSector = RtlUlongByteSwap(*(ULONG *)&(pPacket->pInner->Srb.Cdb[2]));

		switch (pPacket->pInner->Srb.Cdb[0])
		{
		case SCSI_OP_CODE_INQUIRY:
			pPacket->pRequest->bFlags |= 1;
			break;
		case SCSI_OP_CODE_READ_6:
		case SCSI_OP_CODE_READ_10:
		case SCSI_OP_CODE_READ_12:
		case SCSI_OP_CODE_READ_16:
			if (pPacket->pInner->pParser->pCipherCtx)
			{
				DEBUG("VHD[%X]: Read request complete: %X blocks starting from %X\n",
					PsGetCurrentThreadId(), wNumSectors, dwStartingSector);
				EvhdCryptBlocks(pPacket->pInner->pParser, pPacket->pMdl, pPacket->pMdl, pPacket->pInner->Srb.DataTransferLength, FALSE);
			}
			break;
		case SCSI_OP_CODE_WRITE_6:
		case SCSI_OP_CODE_WRITE_10:
		case SCSI_OP_CODE_WRITE_12:
		case SCSI_OP_CODE_WRITE_16:
			if (pPacket->pInner->pParser->pCipherCtx)
			{
				DEBUG("VHD[%X]: Write request complete: %X blocks starting from %X\n",
					PsGetCurrentThreadId(), wNumSectors, dwStartingSector);

				pPacket->pMdl = FreeInnerMdl(pPacket->pMdl);
			}
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


static NTSTATUS EvhdRegisterIo(ParserInstance *parser, BOOLEAN flag1, BOOLEAN flag2)
{
	NTSTATUS status = STATUS_SUCCESS;

#pragma pack(push, 1)
	typedef struct{
		INT dwVersion;
		INT dwFlags;
		UCHAR Unused[12];
		USHORT wFlags;
		USHORT _align;
		CompleteScsiRequest_t pfnCompleteScsiRequest;
		SendMediaNotification_t pfnSendMediaNotification;
		SendNotification_t pfnSendNotification;
		void *pVstorInterface;
	} RegisterIoRequest;

	typedef struct {
		INT dwDiskSaveSize;
		INT dwExtensionBufferSize;
		IoInfo Io;
	} RegisterIoResponse;

#pragma pack(pop)

	if (!parser->bIoRegistered)
	{
		RegisterIoRequest request = { 0 };
		RegisterIoResponse response = { 0 };
		SCSI_ADDRESS scsiAddressResponse = { 0 };

		request.dwVersion = 1;
		request.dwFlags = flag1 ? 9 : 8;
		request.wFlags = 0x2;
		if (flag2) request.wFlags |= 0x1;
		request.pfnCompleteScsiRequest = &EvhdCompleteScsiRequest;
		request.pfnSendMediaNotification = &EvhdSendMediaNotification;
		request.pfnSendNotification = &EvhdSendNotification;
		request.pVstorInterface = parser->pVstorInterface;

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

NTSTATUS EvhdRemoveVhd(ParserInstance *parser, PVOID systemBuffer);

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

		EvhdRemoveVhd(parser, &RemoveVhdRequest);

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

/** Create virtual disk parser */
NTSTATUS EVhdOpenDisk(PCUNICODE_STRING diskPath, ULONG32 OpenFlags, GUID *pVmId, void *vstorInterface, ParserInstance **ppOutParser)
{
	NTSTATUS status = STATUS_SUCCESS;
	PFILE_OBJECT pFileObject = NULL;
	HANDLE FileHandle = NULL;
	ParserInstance *parser = NULL;
	ResiliencyInfo vmInfo = { 0 };

	if (pVmId)
	{
		vmInfo.ResiliencyFlags = 0;
		vmInfo.ResiliencyFlags2 = 0x10;
		vmInfo.ResiliencyLength = 0x1C;
		strncpy(vmInfo.ResiliencyName, "ClusteredApplicationInstance", sizeof(vmInfo.ResiliencyName));
		vmInfo.ResiliencyId = *pVmId;
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

	status = EvhdRegisterQosInterface(parser);

	if (!NT_SUCCESS(status))
		goto failure_cleanup;

	//status = EvhdQueryFastPauseValue(parser);
	//
	//if (!NT_SUCCESS(status))
	//	goto failure_cleanup;

	parser->pVstorInterface = vstorInterface;

	status = EvhdInitCipher(parser, diskPath);
	if (!NT_SUCCESS(status))
	{
		DEBUG("EvhdInitCipher failed with error 0x%08X\n", status);
		goto failure_cleanup;
	}

	*ppOutParser = parser;

	goto cleanup;

failure_cleanup:
	if (FileHandle)
		ZwClose(FileHandle);
	if (parser)
	{
		EVhdCloseDisk(parser);
	}

cleanup:
	return status;
}

/** Destroy virtual disk parser */
VOID EVhdCloseDisk(ParserInstance *parser)
{
	if (parser->pVhdmpFileObject && parser->bQosRegistered && parser->Qos.pQosInterface)
		EvhdUnregisterQosInterface(parser);
	if (parser->bMounted)
		EvhdUnregisterIo(parser);

	EvhdFinalize(parser);
	ExFreePoolWithTag(parser, EvhdPoolTag);
}

/** Initiate virtual disk IO */
NTSTATUS EVhdMountDisk(ParserInstance *parser, UCHAR flags1, MountInfo *mountInfo)
{
	NTSTATUS status = STATUS_SUCCESS;

	// WTF?
	status = EvhdRegisterIo(parser, flags1 & 1 ? TRUE : FALSE, ((INT_PTR)mountInfo >> 1 & 1) ? TRUE : FALSE);
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

	return status;
}

/** Finalize virtual disk IO */
NTSTATUS EVhdDismountDisk(ParserInstance *parser)
{
	NTSTATUS status = STATUS_SUCCESS;
	status = EvhdUnregisterIo(parser);
	parser->bMounted = FALSE;
	return status;
}

/** Validate mounted disk */
NTSTATUS EVhdQueryMountStatusDisk(ParserInstance *parser)
{
	NTSTATUS status = STATUS_SUCCESS;
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
	status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VALIDATE_VHD, &Request, sizeof(Request), NULL, 0);
	return status;
}

/** Scsi request filter function */
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
	if (parser->pCipherCtx) DEBUG("VHD[%X]: Scsi request: %02X, %02X, %04X\n", PsGetCurrentThreadId(), opCode, pRequest->bDataIn, pRequest->DataTransferLength);
	USHORT wBlocks = RtlUshortByteSwap(*(USHORT *)&(pPacket->pInner->Srb.Cdb[7]));
	ULONG dwBlockOffset = RtlUlongByteSwap(*(ULONG *)&(pPacket->pInner->Srb.Cdb[2]));
	switch (opCode)
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
		break;
	case SCSI_OP_CODE_WRITE_6:
	case SCSI_OP_CODE_WRITE_10:
	case SCSI_OP_CODE_WRITE_12:
	case SCSI_OP_CODE_WRITE_16:
		if (parser->pCipherCtx)
		{
			DEBUG("VHD[%X]: Write request: %X blocks starting from %X\n", PsGetCurrentThreadId(), wBlocks, dwBlockOffset);

			pPacket->pMdl = AllocateInnerMdl(pMdl);

			status = EvhdCryptBlocks(parser, pMdl, pPacket->pMdl, pRequest->DataTransferLength, TRUE);

			pMdl = pPacket->pMdl;

			if (pMdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA)
				MmUnmapLockedPages(pMdl->MappedSystemVa, pMdl);
		}
		break;
	case SCSI_OP_CODE_READ_6:
	case SCSI_OP_CODE_READ_10:
	case SCSI_OP_CODE_READ_12:
	case SCSI_OP_CODE_READ_16:
		if (parser->pCipherCtx)
		{
			DEBUG("VHD[%X]: Read request: %X blocks starting from %X\n", PsGetCurrentThreadId(), wBlocks, dwBlockOffset);
		}
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

/** Query virtual disk info */
NTSTATUS EVhdQueryInformationDisk(ParserInstance *parser, EDiskInfoType type, INT unused1, INT unused2, PVOID pBuffer, INT *pBufferSize)
{
	NTSTATUS status = STATUS_SUCCESS;
	EMetaInfoType Request = EMetaInfoType_Geometry;
	MetaInfoResponse Response = { 0 };

	UNREFERENCED_PARAMETER(unused1);
	UNREFERENCED_PARAMETER(unused2);

	if (EDiskInfo_Format == type)
	{
		ASSERT(0x38 == sizeof(DiskInfo_Format));
		if (*pBufferSize < sizeof(DiskInfo_Format))
			return status = STATUS_BUFFER_TOO_SMALL;
		DiskInfo_Format *pRes = (DiskInfo_Format *)pBuffer;
		memset(pBuffer, 0, sizeof(DiskInfo_Format));

		Request = EMetaInfoType_Type;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
			&Response, sizeof(MetaInfoResponse));
		if (!NT_SUCCESS(status))
		{
			DEBUG("Failed to retrieve disk type. 0x%0X\n", status);
			return status;
		}
		pRes->DiskType = Response.vals[0].dwLow;

		Request = EMetaInfoType_ParserInfo;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
			&Response, sizeof(MetaInfoResponse));
		if (!NT_SUCCESS(status))
		{
			DEBUG("Failed to retrieve parser info. 0x%0X\n", status);
			return status;
		}
		pRes->DiskFormat = Response.vals[0].dwLow;

		Request = EMetaInfoType_Geometry;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
			&Response, sizeof(MetaInfoResponse));
		if (!NT_SUCCESS(status))
		{
			DEBUG("Failed to retrieve size info. 0x%0X\n", status);
			return status;
		}
		pRes->dwBlockSize = Response.vals[2].dwLow;
		pRes->qwDiskSize = Response.vals[1].qword;

		Request = EMetaInfoType_LinkageId;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
			&Response, sizeof(MetaInfoResponse));
		if (!NT_SUCCESS(status))
		{
			DEBUG("Failed to retrieve linkage identifier. 0x%0X\n", status);
			return status;
		}
		pRes->LinkageId = Response.guid;

		Request = EMetaInfoType_InUseFlag;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
			&Response, sizeof(MetaInfoResponse));
		if (!NT_SUCCESS(status))
		{
			DEBUG("Failed to retrieve in use flag. 0x%0X\n", status);
			return status;
		}
		pRes->bIsInUse = (BOOLEAN)Response.vals[0].dwLow;

		Request = EMetaInfoType_IsFullyAllocated;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
			&Response, sizeof(MetaInfoResponse));
		if (!NT_SUCCESS(status))
		{
			DEBUG("Failed to retrieve fully allocated flag. 0x%0X\n", status);
			return status;
		}
		pRes->bIsFullyAllocated = (BOOLEAN)Response.vals[0].dwLow;

		Request = EMetaInfoType_Unk9;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
			&Response, sizeof(MetaInfoResponse));
		if (!NT_SUCCESS(status))
		{
			DEBUG("Failed to retrieve unk9 flag. 0x%0X\n", status);
			return status;
		}
		pRes->f_1C = (BOOLEAN)Response.vals[0].dwLow;

		Request = EMetaInfoType_Page83Data;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
			&Response, sizeof(MetaInfoResponse));
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
		if (*pBufferSize < sizeof(INT))
			return status = STATUS_BUFFER_TOO_SMALL;
		INT *pRes = (INT *)pBuffer;
		Request = EMetaInfoType_FragmentationPercentageR2;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
			&Response, sizeof(MetaInfoResponse));
		if (!NT_SUCCESS(status))
		{
			DEBUG("Failed to retrieve type info. 0x%0X\n", status);
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
		Request = EMetaInfoType_ParentNameList;
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
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_PRELOAD_DISK_METADATA, NULL, 0,
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
		Request = EMetaInfoType_Geometry;
		status = SynchronouseCall(parser->pVhdmpFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
			&Response, sizeof(MetaInfoResponse));
		if (!NT_SUCCESS(status))
		{
			DEBUG("Failed to retrieve size info. 0x%0X\n", status);
			return status;
		}
		pRes->dwSectorSize = Response.vals[2].dwHigh;
		pRes->qwDiskSize = Response.vals[0].qword;
		pRes->dwNumSectors = parser->dwNumSectors;
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
	return STATUS_SUCCESS;
}

/** Pause VM */
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

/** Resume VM */
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

NTSTATUS EVhdSetBehaviourDisk(ParserInstance *parser, INT behaviour)
{
	INT IoControl;
	if (behaviour == 1)
		IoControl = IOCTL_STORAGE_VHD_ISO_EJECT_MEDIA;
	else if (behaviour == 2)
		IoControl = IOCTL_STORAGE_VHD_ISO_INSERT_MEDIA;
	else
		return STATUS_INVALID_DEVICE_REQUEST;

	return SynchronouseCall(parser->pVhdmpFileObject, IoControl, NULL, 0, NULL, 0);
}

/** Set Qos interface configuration */
NTSTATUS EVhdSetQosConfigurationDisk(ParserInstance *parser, PVOID pConfig)
{
	if (parser->Qos.pfnSetQosConfiguration)
		return parser->Qos.pfnSetQosConfiguration(parser->Qos.pQosInterface, pConfig);
	else
		return STATUS_INVALID_DEVICE_REQUEST;
}

/** Unimplemented on R2 */
NTSTATUS EVhdGetQosInformationDisk(ParserInstance *parser, PVOID pInfo)
{
	if (parser->Qos.pfnGetQosInformation)
		return parser->Qos.pfnGetQosInformation(parser->Qos.pQosInterface, pInfo);
	else
		return STATUS_INVALID_DEVICE_REQUEST;
}
