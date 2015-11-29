#pragma once
//#include <wdm.h>
#include <ntifs.h>
#include "Vstor.h"
#include "cipher.h"

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
	IoInfo			Io;
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

	USHORT			wMountFlags;

	RecoveryStatusInfo	RecoveryStatusInfo;

} ParserInstance;

/** Forward declaration of parser handler functions */
NTSTATUS EVhdOpenDisk(PCUNICODE_STRING diskPath, ULONG32 OpenFlags, GUID *pVmId, PVOID vstorInterface, __out ParserInstance **outParser);
VOID EVhdCloseDisk(ParserInstance *parser);
NTSTATUS EVhdMountDisk(ParserInstance *parser, UCHAR flags, PGUID pUnkGuid, __out MountInfo *mountInfo);
NTSTATUS EVhdDismountDisk(ParserInstance *parser);
NTSTATUS EVhdQueryMountStatusDisk(ParserInstance *parser, ULONG32 param);
NTSTATUS EVhdExecuteScsiRequestDisk(ParserInstance *parser, ScsiPacket *pPacket);
NTSTATUS EVhdQueryInformationDisk(ParserInstance *parser, EDiskInfoType type, INT unused1, INT unused2, PVOID pBuffer, INT *pBufferSize);
NTSTATUS EVhdQuerySaveVersionDisk(ParserInstance *parser, INT *pVersion);
NTSTATUS EVhdSaveDisk(ParserInstance *parser, PVOID data, ULONG32 size, ULONG32 *dataStored);
NTSTATUS EVhdRestoreDisk(ParserInstance *parser, INT revision, PVOID data, ULONG32 size);
NTSTATUS EVhdSetBehaviourDisk(ParserInstance *parser, INT behaviour, BOOLEAN *enableCache, INT param /* = 1 */);
NTSTATUS EVhdSetQosPolicyDisk(ParserInstance *parser, PVOID pInputBuffer, ULONG32 dwSize);
NTSTATUS EVhdGetQosStatusDisk(ParserInstance *parser, PVOID pSystemBuffer, ULONG32 dwSize, QoSStatusCompletionRoutine pfnCompletionCb, PVOID pInterface);
NTSTATUS EVhdChangeTrackingGetParameters(ParserInstance *parser, CTParameters *pParams);
NTSTATUS EVhdChangeTrackingStart(ParserInstance *parser, CTStartParam *pParams);
NTSTATUS EVhdChangeTrackingStop(ParserInstance *parser, const ULONG32 *pInput, ULONG32 *pOutput);
NTSTATUS EVhdChangeTrackingSwitchLogs(ParserInstance *parser, CTSwitchLogParam *pParams, ULONG32 *pOutput);
NTSTATUS EVhdNotifyRecoveryStatus(ParserInstance *parser, RecoveryStatusCompletionRoutine pfnCompletionCb, void *pInterface);
NTSTATUS EVhdGetRecoveryStatus(ParserInstance *parser, ULONG32 *pStatus);
NTSTATUS EVhdPrepareMetaOperation(ParserInstance *parser, void *pMetaOperationBuffer, MetaOperationCompletionRoutine pfnCompletionCb, void *pInterface, MetaOperation **ppOperation);
NTSTATUS EVhdStartMetaOperation(MetaOperation *operation);
NTSTATUS EVhdCancelMetaOperation(MetaOperation *operation);
NTSTATUS EVhdQueryMetaOperationProgress(MetaOperation *operation);
NTSTATUS EVhdCleanupMetaOperation(MetaOperation *operation);
NTSTATUS EVhdParserDeleteSnapshot(ParserInstance *parser, void *pInputBuffer /* TODO:sizeof=0x18 */);
NTSTATUS EVhdParserQueryChanges(ParserInstance *parser, void *pInputBuffer, ULONG32 dwInputBufferLength);
