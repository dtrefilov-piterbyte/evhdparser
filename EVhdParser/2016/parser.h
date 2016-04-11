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
	HANDLE			FileHandle;
	PFILE_OBJECT	pVhdmpFileObject;
	UCHAR			ScsiPathId;
	UCHAR			ScsiTargetId;
	UCHAR			ScsiLun;
	BOOLEAN			bMounted;
	BOOLEAN			bIoRegistered;
	BOOLEAN			bFastPause;
	BOOLEAN			bFastClose;
	PVOID			pVstorInterface;
    PARSER_IO_INFO	Io;
	ULONG32			dwDiskSaveSize;
	ULONG32			dwInnerBufferSize;
	
	PIRP			pDirectIoIrp;
	EX_PUSH_LOCK	DirectIoPushLock;

	PIRP			pQoSStatusIrp;
	PVOID			pQoSStatusBuffer;
	QoSStatusCompletionRoutine pfnQoSStatusCallback;
	PVOID			pQoSStatusInterface;

	BOOLEAN			bResiliencyEnabled;
	EX_RUNDOWN_REF	RecoveryRundownProtection;
	PIRP			pRecoveryStatusIrp;
	RecoveryStatusCompletionRoutine	pfnRecoveryStatusCallback;
	PVOID			pRecoveryStatusInterface;

	USHORT			wMountFlags;

} ParserInstance;

/** Forward declaration of parser handler functions */
NTSTATUS EVhdOpenDisk(PCUNICODE_STRING diskPath, ULONG32 OpenFlags, GUID *pVmId, PVOID vstorInterface, __out PVOID *pOutContext);
VOID EVhdCloseDisk(PVOID pContext);
NTSTATUS EVhdMountDisk(PVOID pContext, UCHAR flags, PGUID pUnkGuid, __out PARSER_MOUNT_INFO *mountInfo);
NTSTATUS EVhdDismountDisk(PVOID pContext);
NTSTATUS EVhdQueryMountStatusDisk(PVOID pContext, ULONG32 param);
NTSTATUS EVhdExecuteScsiRequestDisk(PVOID pContext, SCSI_PACKET *pRequest);
NTSTATUS EVhdQueryInformationDisk(PVOID pContext, EDiskInfoType type, INT unused1, INT unused2, PVOID pBuffer, INT *pBufferSize);
NTSTATUS EVhdQuerySaveVersionDisk(PVOID pContext, INT *pVersion);
NTSTATUS EVhdSaveDisk(PVOID pContext, PVOID data, ULONG32 size, ULONG32 *dataStored);
NTSTATUS EVhdRestoreDisk(PVOID pContext, INT revision, PVOID data, ULONG32 size);
NTSTATUS EVhdSetBehaviourDisk(PVOID pContext, INT behaviour, BOOLEAN *enableCache, INT param /* = 1 */);
NTSTATUS EVhdSetQosPolicyDisk(PVOID pContext, PVOID pInputBuffer, ULONG32 dwSize);
NTSTATUS EVhdGetQosStatusDisk(PVOID pContext, PVOID pSystemBuffer, ULONG32 dwSize, QoSStatusCompletionRoutine pfnCompletionCb, PVOID pInterface);
NTSTATUS EVhdChangeTrackingSetParameters(PVOID pContext);
NTSTATUS EVhdChangeTrackingGetParameters(PVOID pContext, CTParameters *pParams);
NTSTATUS EVhdChangeTrackingStart(PVOID pContext, CTStartParam *pParams);
NTSTATUS EVhdChangeTrackingStop(PVOID pContext, const ULONG32 *pInput, ULONG32 *pOutput);
NTSTATUS EVhdChangeTrackingSwitchLogs(PVOID pContext, CTSwitchLogParam *pParams, ULONG32 *pOutput);
NTSTATUS EVhdEnableResiliency(PVOID pContext);
NTSTATUS EVhdNotifyRecoveryStatus(PVOID pContext, RecoveryStatusCompletionRoutine pfnCompletionCb, PVOID pInterface);
NTSTATUS EVhdGetRecoveryStatus(PVOID pContext, ULONG32 *pStatus);
NTSTATUS EVhdPrepareMetaOperation(PVOID pContext, MetaOperationBuffer *pMetaOperationBuffer, MetaOperationCompletionRoutine pfnCompletionCb, PVOID pInterface, MetaOperation **ppOperation);
NTSTATUS EVhdStartMetaOperation(MetaOperation *operation);
NTSTATUS EVhdCancelMetaOperation(MetaOperation *operation);
NTSTATUS EVhdQueryMetaOperationProgress(MetaOperation *operation, PVOID pProgress);
NTSTATUS EVhdCleanupMetaOperation(MetaOperation *operation);
NTSTATUS EVhdParserDeleteSnapshot(PVOID pContext, void *pInputBuffer);
NTSTATUS EVhdParserQueryChanges(PVOID pContext, void *pSystemBuffer, ULONG32 dwInputBufferLength, ULONG32 dwOutputBufferLength);
