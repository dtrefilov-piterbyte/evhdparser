#pragma once
//#include <wdm.h>
#include <ntifs.h>
#include "Vstor.h"
#include "cipher.h"

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
	IoInfo			Io;
	BOOLEAN			bQosRegistered;
	QosInfo			Qos;
	ULONG32			dwDiskSaveSize;
	INT				dwInnerBufferSize;
	PIRP			pIrp;
	ULONG_PTR		IoLock;
	PVOID			pCipherCtx;
	CipherEngine	*pCipherEngine;
} ParserInstance;

/** Forward declaration of parser handler */
NTSTATUS EVhdOpenDisk(PCUNICODE_STRING diskPath, ULONG32 OpenFlags, GUID *pVmId, PVOID vstorInterface, ParserInstance **outParser);
VOID EVhdCloseDisk(ParserInstance *parser);
NTSTATUS EVhdMountDisk(ParserInstance *parser, UCHAR flags1, MountInfo *mountInfo);
NTSTATUS EVhdDismountDisk(ParserInstance *parser);
NTSTATUS EVhdQueryMountStatusDisk(ParserInstance *parser);
NTSTATUS EVhdExecuteScsiRequestDisk(ParserInstance *parser, ScsiPacket *pPacket);
NTSTATUS EVhdQueryInformationDisk(ParserInstance *parser, EDiskInfoType type, INT unused1, INT unused2, PVOID pBuffer, INT *pBufferSize);
NTSTATUS EVhdQuerySaveVersionDisk(ParserInstance *parser, INT *pVersion);
NTSTATUS EVhdSaveDisk(ParserInstance *parser, PVOID data, ULONG32 size, ULONG32 *dataStored);
NTSTATUS EVhdRestoreDisk(ParserInstance *parser, INT revision, PVOID data, ULONG32 size);
NTSTATUS EVhdSetBehaviourDisk(ParserInstance *parser, INT behaviour);
NTSTATUS EVhdSetQosConfigurationDisk(ParserInstance *parser, PVOID pConfig);
NTSTATUS EVhdGetQosInformationDisk(ParserInstance *parser, PVOID pInfo);
