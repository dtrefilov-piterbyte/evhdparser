#pragma once
//#include <wdm.h>
#include <ntifs.h>
#include "Vstor.h"
#include "cipher.h"

typedef struct _ParserInstance {
	HANDLE			FileHandle;
	PFILE_OBJECT	pVhdmpFileObject;
	UCHAR			PathId;
	UCHAR			TargetId;
	UCHAR			LunId;
	BOOLEAN			bMounted;
	BOOLEAN			bIoRegistered;
	BOOLEAN			bFastPause;
	BOOLEAN			bFastClose;
	IoInfo			IoInfo;
	ULONG32			dwDiskSaveSize;
	ULONG32			dwInnerBufferSize;
	
	PIRP			pDirectIoIrp;
	EX_PUSH_LOCK	DirectIoPushLock;

	PIRP			pQoSStatusIrp;
	PVOID			pQoSStatusBuffer;

	BOOLEAN			bResiliencyEnabled;
	EX_RUNDOWN_REF	RecoveryRundownProtection;
	PIRP			pRecoveryStatusIrp;
	RecoveryStatusCompletionRoutine	pfnRecoveryStatusCallback;

	RecoveryStatusInfo	RecoveryStatusInfo;

} ParserInstance;

/** Forward declaration of parser handler functions */
NTSTATUS EVhdOpenDisk(PCUNICODE_STRING diskPath, ULONG32 OpenFlags, GUID *pVmId, PVOID vstorInterface, __out ParserInstance **outParser);
VOID EVhdCloseDisk(ParserInstance *parser);
NTSTATUS EVhdMountDisk(ParserInstance *parser, UCHAR flags, PGUID pUnkGuid, __out MountInfo *mountInfo);
NTSTATUS EVhdDismountDisk(ParserInstance *parser);
NTSTATUS EVhdQueryMountStatusDisk(ParserInstance *parser, /* TODO */ ULONG32 unk);
NTSTATUS EVhdExecuteScsiRequestDisk(ParserInstance *parser, ScsiPacket *pPacket);
NTSTATUS EVhdQueryInformationDisk(ParserInstance *parser, EDiskInfoType type, INT unused1, INT unused2, PVOID pBuffer, INT *pBufferSize);
NTSTATUS EVhdQuerySaveVersionDisk(ParserInstance *parser, INT *pVersion);
NTSTATUS EVhdSaveDisk(ParserInstance *parser, PVOID data, ULONG32 size, ULONG32 *dataStored);
NTSTATUS EVhdRestoreDisk(ParserInstance *parser, INT revision, PVOID data, ULONG32 size);
NTSTATUS EVhdSetBehaviourDisk(ParserInstance *parser, INT behaviour);
NTSTATUS EVhdSetQosConfigurationDisk(ParserInstance *parser, PVOID pConfig);
NTSTATUS EVhdGetQosInformationDisk(ParserInstance *parser, PVOID pInfo);
