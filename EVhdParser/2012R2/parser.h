#pragma once
//#include <wdm.h>
#include <ntifs.h>
#include "Vstor.h"
#include "cipher.h"

typedef struct _PARSER_STATE {
	ULONG64		qwUnk1;
	ULONG64		qwUnk2;
	USHORT		wUnk3;
	UCHAR		bCache;
	UCHAR		Reserved[13];
} PARSER_STATE, *PPARSER_STATE;

typedef struct _ParserInstance {
	PFILE_OBJECT	pVhdmpFileObject;
	HANDLE			FileHandle;
	UCHAR			ScsiPathId;
	UCHAR			ScsiTargetId;
	UCHAR			ScsiLun;
	BOOLEAN			bIoRegistered;
	BOOLEAN			bMounted;
	BOOLEAN			bFastPause;
	INT				dwNumSectors;
	PVOID			pVstorInterface;
	PARSER_IO_INFO	Io;
	BOOLEAN			bQosRegistered;
	PARSER_QOS_INFO	Qos;
	ULONG32			dwDiskSaveSize;
	INT				dwInnerBufferSize;
	PIRP			pIrp;
	ULONG_PTR		IoLock;
	PVOID			pCipherCtx;
	CipherEngine	*pCipherEngine;
} ParserInstance;

/** Forward declaration of parser handler */
NTSTATUS EVhd_OpenDisk(PCUNICODE_STRING diskPath, ULONG32 OpenFlags, GUID *pVmId, PVOID vstorInterface, PVOID *pOutContext);
VOID EVhd_CloseDisk(PVOID pContext);
NTSTATUS EVhd_MountDisk(PVOID pContext, UCHAR flags1, PARSER_MOUNT_INFO *mountInfo);
NTSTATUS EVhd_DismountDisk(PVOID pContext);
NTSTATUS EVhd_QueryMountStatusDisk(PVOID pContext);
NTSTATUS EVhd_ExecuteScsiRequestDisk(PVOID pContext, SCSI_REQUEST *pRequest);
NTSTATUS EVhd_QueryInformationDisk(PVOID pContext, EDiskInfoType type, INT unused1, INT unused2, PVOID pBuffer, INT *pBufferSize);
NTSTATUS EVhd_QuerySaveVersionDisk(PVOID pContext, INT *pVersion);
NTSTATUS EVhd_SaveDisk(PVOID pContext, PVOID data, ULONG32 size, ULONG32 *dataStored);
NTSTATUS EVhd_RestoreDisk(PVOID pContext, INT revision, PVOID data, ULONG32 size);
NTSTATUS EVhd_SetBehaviourDisk(PVOID pContext, INT behaviour);
NTSTATUS EVhd_SetQosConfigurationDisk(PVOID pContext, PVOID pConfig);
NTSTATUS EVhd_GetQosInformationDisk(PVOID pContext, PVOID pInfo);
