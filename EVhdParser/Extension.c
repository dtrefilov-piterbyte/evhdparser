#include "stdafx.h"
#include "Vdrvroot.h"
#include "Extension.h"
#include "Log.h"
#include "cipher.h"
#include "ScsiOp.h"
#include "Dispatch.h"

#define EXTLOG(level, format, ...) LOG_FUNCTION(level, LOG_CTG_EXTENSION, format, __VA_ARGS__)

const ULONG32 ExtAllocationTag = 'SExt';

static ULONG ExtWaitCipherConfigTimeoutInMs = 5000;

typedef struct {
    CipherEngine *pCipherEngine;
    PVOID pCipherContext;
    GUID DiskId;
    GUID ApplicationId;
} EXTENSION_CONTEXT, *PEXTENSION_CONTEXT;

PMDL Ext_AllocateInnerMdl(PMDL pSourceMdl)
{
    PHYSICAL_ADDRESS LowAddress, HighAddress, SkipBytes;
    LowAddress.QuadPart = 0;
    HighAddress.QuadPart = 0xFFFFFFFFFFFFFFFF;
    SkipBytes.QuadPart = 0;

    PMDL pNewMdl = MmAllocatePagesForMdlEx(LowAddress, HighAddress, SkipBytes, pSourceMdl->ByteCount, MmCached, MM_DONT_ZERO_ALLOCATION);
    pNewMdl->Next = pSourceMdl;

    return pNewMdl;
}

PMDL Ext_FreeInnerMdl(PMDL pMdl)
{
    PMDL pSourceMdl = pMdl->Next;
    pMdl->Next = NULL;
    MmFreePagesFromMdl(pMdl);
    return pSourceMdl;
}

NTSTATUS Ext_CryptBlocks(PEXTENSION_CONTEXT ExtContext, PMDL pSourceMdl, PMDL pTargetMdl, SIZE_T size, SIZE_T sector, BOOLEAN Encrypt)
{
    NTSTATUS status = STATUS_SUCCESS;
    PVOID pSource = NULL, pTarget = NULL;
    CONST SIZE_T SectorSize = 512;
    SIZE_T SectorOffset = 0;

    if (!ExtContext || !pSourceMdl || !pTargetMdl)
        return STATUS_INVALID_PARAMETER;
    if (!ExtContext->pCipherContext)
        return STATUS_SUCCESS;

    pSource = MmGetSystemAddressForMdlSafe(pSourceMdl, NormalPagePriority);
    pTarget = MmGetSystemAddressForMdlSafe(pTargetMdl, NormalPagePriority);

    if (!pSource || !pTarget)
        return STATUS_INSUFFICIENT_RESOURCES;

    LOG_ASSERT(0 == size % SectorSize);

    EXTLOG(LL_VERBOSE, "VHD: %s 0x%X bytes\n", Encrypt ? "Encrypting" : "Decrypting", size);

    for (SectorOffset = 0; SectorOffset < size; SectorOffset += SectorSize)
    {
        PUCHAR pSourceSector = (PUCHAR)pSource + SectorOffset;
        PUCHAR pTargetSector = (PUCHAR)pTarget + SectorOffset;
        status = (Encrypt ? ExtContext->pCipherEngine->pfnEncrypt : ExtContext->pCipherEngine->pfnDecrypt)(
            ExtContext->pCipherContext, pSourceSector, pTargetSector, SectorSize, sector++);
        if (!NT_SUCCESS(status))
            break;
    }

    if (pSourceMdl && 0 != (pSourceMdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA))
        MmUnmapLockedPages(pSource, pSourceMdl);
    if (pTargetMdl && 0 != (pTargetMdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA))
        MmUnmapLockedPages(pTarget, pTargetMdl);
    return status;
}

NTSTATUS Ext_Initialize(_Out_ PEVHD_EXT_CAPABILITIES pCaps)
{
    NTSTATUS Status = STATUS_SUCCESS;
    TRACE_FUNCTION_IN();
    pCaps->StateSize = 0;
    Status = CipherInit();
    TRACE_FUNCTION_OUT_STATUS(Status);
    return Status;
}

NTSTATUS Ext_Cleanup()
{
    NTSTATUS Status = STATUS_SUCCESS;
    TRACE_FUNCTION_IN();
    Status = CipherCleanup();
    TRACE_FUNCTION_OUT_STATUS(Status);
    return Status;
}

NTSTATUS Ext_Create(_In_ PCUNICODE_STRING DiskPath,
    _In_ PGUID ApplicationId,
    _In_ EDiskFormat DiskFormat,
    _In_ PGUID DiskId,
    _Outptr_opt_result_maybenull_ PVOID *DiskContext)
{
    UNREFERENCED_PARAMETER(DiskFormat);
    NTSTATUS Status = STATUS_SUCCESS;
    TRACE_FUNCTION_IN();
    EXTLOG(LL_INFO, "Disk opened %S, " GUID_FORMAT, DiskPath->Buffer, GUID_PARAMETERS(*DiskId));

    if (DiskFormat != EDiskFormat_Vhd && DiskFormat != EDiskFormat_Vhdx)
        return Status;

    PEXTENSION_CONTEXT Context = ExAllocatePoolWithTag(NonPagedPool, sizeof(EXTENSION_CONTEXT), ExtAllocationTag);
    if (!Context) {
        EXTLOG(LL_FATAL, "Could not allocate memory for context");
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    else
    {
        memset(Context, 0, sizeof(EXTENSION_CONTEXT));
        Context->DiskId = *DiskId;
        if (ApplicationId)
            Context->ApplicationId = *ApplicationId;
        *DiskContext = Context;
    }
    
    TRACE_FUNCTION_OUT_STATUS(Status);
    return Status;
}

NTSTATUS Ext_Delete(_In_ PVOID ExtContext)
{
    NTSTATUS Status = STATUS_SUCCESS;
    TRACE_FUNCTION_IN();
    Ext_Dismount(ExtContext);
    ExFreePoolWithTag(ExtContext, ExtAllocationTag);
    TRACE_FUNCTION_OUT_STATUS(Status);
    return Status;
}

NTSTATUS Ext_Mount(_In_ PVOID ExtContext)
{
    PEXTENSION_CONTEXT Context = ExtContext;
    TRACE_FUNCTION_IN();
    NTSTATUS Status = STATUS_SUCCESS;

    PARSER_MESSAGE Request;
    PARSER_RESPONSE_MESSAGE Response;

    Request.Type = MessageTypeQueryCipherConfig;
    Request.Message.QueryCipherConfig.DiskId = Context->DiskId;
    Request.Message.QueryCipherConfig.ApplicationId = Context->ApplicationId;

    if (DPT_SynchronouseRequest(&Request, &Response, ExtWaitCipherConfigTimeoutInMs))
    {
        if (Response.Type == MessageTypeResponseCipherConfig)
        {
            Status = CipherCreate(Response.Message.CipherConfig.Algorithm, &Response.Message.CipherConfig.Opts,
                &Context->pCipherEngine, &Context->pCipherContext);
        }
        else
            Status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        Status = CipherEngineGet(&Context->DiskId, &Context->pCipherEngine, &Context->pCipherContext);
    }
    if (!NT_SUCCESS(Status)) {
        EXTLOG(LL_FATAL, "Could not create encryption context");
    }
    TRACE_FUNCTION_OUT_STATUS(Status);
    return Status;
}

NTSTATUS Ext_Dismount(_In_ PVOID ExtContext)
{
    TRACE_FUNCTION_IN();
    NTSTATUS Status = STATUS_SUCCESS;
    PEXTENSION_CONTEXT Context = ExtContext;
    if (Context->pCipherEngine) {
        Context->pCipherEngine->pfnDestroy(Context->pCipherContext);
        Context->pCipherContext = NULL;
        Context->pCipherEngine = NULL;
    }
    TRACE_FUNCTION_OUT_STATUS(Status);
    return Status;
}

NTSTATUS Ext_Pause(_In_ PVOID ExtContext, _In_ PVOID SaveBuffer, _Inout_ SIZE_T *SaveBufferSize)
{
    UNREFERENCED_PARAMETER(ExtContext);
    UNREFERENCED_PARAMETER(SaveBuffer);
    UNREFERENCED_PARAMETER(SaveBufferSize);
    TRACE_FUNCTION_IN();
    NTSTATUS Status = STATUS_SUCCESS;
    TRACE_FUNCTION_OUT_STATUS(Status);
    return Status;
}

NTSTATUS Ext_Restore(_In_ PVOID ExtContext, PVOID RestoreBuffer, SIZE_T RestoreBufferSize)
{
    UNREFERENCED_PARAMETER(ExtContext);
    UNREFERENCED_PARAMETER(RestoreBuffer);
    UNREFERENCED_PARAMETER(RestoreBufferSize);
    TRACE_FUNCTION_IN();
    NTSTATUS Status = STATUS_SUCCESS;
    TRACE_FUNCTION_OUT_STATUS(Status);
    return Status;
}

NTSTATUS Ext_StartScsiRequest(_In_ PVOID ExtContext, _In_ PEVHD_EXT_SCSI_PACKET pExtPacket)
{
    UNREFERENCED_PARAMETER(ExtContext);
    UNREFERENCED_PARAMETER(pExtPacket);
    NTSTATUS Status = STATUS_SUCCESS;
    UCHAR opCode = pExtPacket->Srb->Cdb[0];
    PMDL pMdl = pExtPacket->pMdl;

    PEXTENSION_CONTEXT Context = ExtContext;
    USHORT wSectors = RtlUshortByteSwap(*(USHORT *)&(pExtPacket->Srb->Cdb[7]));
    ULONG dwSectorOffset = RtlUlongByteSwap(*(ULONG *)&(pExtPacket->Srb->Cdb[2]));
    switch (opCode)
    {
    case SCSI_OP_CODE_WRITE_6:
    case SCSI_OP_CODE_WRITE_10:
    case SCSI_OP_CODE_WRITE_12:
    case SCSI_OP_CODE_WRITE_16:
        if (Context->pCipherEngine)
        {
            EXTLOG(LL_VERBOSE, "Write request: %X blocks starting from %X\n", wSectors, dwSectorOffset);

            pExtPacket->pMdl = Ext_AllocateInnerMdl(pMdl);

            Status = Ext_CryptBlocks(Context, pMdl, pExtPacket->pMdl, pExtPacket->Srb->DataTransferLength, dwSectorOffset, TRUE);

            pMdl = pExtPacket->pMdl;

            if (pMdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA)
                MmUnmapLockedPages(pMdl->MappedSystemVa, pMdl);
        }
        break;
    case SCSI_OP_CODE_READ_6:
    case SCSI_OP_CODE_READ_10:
    case SCSI_OP_CODE_READ_12:
    case SCSI_OP_CODE_READ_16:
        if (Context->pCipherEngine)
        {
            EXTLOG(LL_VERBOSE, "Read request: %X blocks starting from %X\n", wSectors, dwSectorOffset);
        }
        break;
    }
    return Status;
}

NTSTATUS Ext_CompleteScsiRequest(_In_ PVOID ExtContext, _In_ PEVHD_EXT_SCSI_PACKET pExtPacket, _In_ NTSTATUS Status)
{
    UCHAR opCode = pExtPacket->Srb->Cdb[0];
    PMDL pMdl = pExtPacket->pMdl;

    PEXTENSION_CONTEXT Context = ExtContext;
    USHORT wSectors = RtlUshortByteSwap(*(USHORT *)&(pExtPacket->Srb->Cdb[7]));
    ULONG dwSectorOffset = RtlUlongByteSwap(*(ULONG *)&(pExtPacket->Srb->Cdb[2]));

    switch (opCode)
    {
    case SCSI_OP_CODE_READ_6:
    case SCSI_OP_CODE_READ_10:
    case SCSI_OP_CODE_READ_12:
    case SCSI_OP_CODE_READ_16:
        if (Context->pCipherEngine)
        {
            EXTLOG(LL_VERBOSE, "Read request completed: %X blocks starting from %X\n",
                wSectors, dwSectorOffset);
            if (NT_SUCCESS(Status)) {
                Ext_CryptBlocks(Context, pMdl, pMdl, pExtPacket->Srb->DataTransferLength, dwSectorOffset, FALSE);
            }
        }
        break;
    case SCSI_OP_CODE_WRITE_6:
    case SCSI_OP_CODE_WRITE_10:
    case SCSI_OP_CODE_WRITE_12:
    case SCSI_OP_CODE_WRITE_16:
        if (Context->pCipherEngine)
        {
            EXTLOG(LL_VERBOSE, "Write request completed: %X blocks starting from %X\n",
                wSectors, dwSectorOffset);

            pExtPacket->pMdl = Ext_FreeInnerMdl(pExtPacket->pMdl);
        }
        break;
    }
    return Status;
}
