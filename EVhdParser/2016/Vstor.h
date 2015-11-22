#pragma once   
#include "ScsiOp.h"
#include "Vdrvroot.h"

#pragma pack(push, 1)

#pragma warning(push)													
#pragma warning(disable : 4214) // nonstandard extension: bitfield types other than int
#pragma warning(disable : 4201) // nonstandard extension: nameless struct/union

typedef struct {
	ULONG32		BytesTransferred;
	ULONG32		_align;
	void		*pBuffer;
} CTParameters;

typedef struct {
	ULONG32		dwUnk1;
	ULONG32		dwSize;
	GUID		unk2;
	ULONG64		unk3;
	UCHAR		unk4;
	UCHAR		_align[3];
	ULONG32		unk5;
} CTEnableParam;

typedef struct {
	ULONG32		dwUnk1;
	ULONG32		dwSize;
	GUID		unk2;
	ULONG64		unk3;
	GUID		unk4;
} CTSwitchLogParam;

typedef enum {
	EMetaOperation_Snapshot,
	EMetaOperation_Resize,
	EMetaOperation_Complete,
	EMetaOperation_Extract
} EMetaOperation;

typedef NTSTATUS(*RecoveryStatusCompletionRoutine)(void *, ULONG32);

typedef NTSTATUS(*ParserOpenDisk_t)(PCUNICODE_STRING, ULONG32, GUID *, void *, struct _ParserInstance **);
typedef VOID(*ParserCloseDisk_t)(struct _ParserInstance *);
typedef NTSTATUS(*ParserCallback_t)(struct _ParserInstance *);
typedef NTSTATUS(*ParserMountDisk_t)(struct _ParserInstance *, UCHAR, void *);
typedef NTSTATUS(*ParserDismountDisk_t)(struct _ParserInstance *);
typedef NTSTATUS(*ParserQueryMountStatusDisk_t)(struct _ParserInstance *);
typedef NTSTATUS(*ParserExecuteScsiRequestDisk_t)(struct _ParserInstance *, void *);
typedef NTSTATUS(*ParserQueryInformationDisk_t)(struct _ParserInstance *, int, INT, INT, PVOID, INT *);
typedef NTSTATUS(*ParserQuerySaveVersionDisk_t)(struct _ParserInstance *, INT *);
typedef NTSTATUS(*ParserSaveDisk_t)(struct _ParserInstance *, PVOID, ULONG32, ULONG32 *);
typedef NTSTATUS(*ParserRestoreDisk_t)(struct _ParserInstance *, INT, PVOID, ULONG32);
typedef NTSTATUS(*ParserSetBehaviourDisk_t)(struct _ParserInstance *, INT);
typedef NTSTATUS(*ParserSetQosPolicyDisk_t)(struct _ParserInstance *, void *, ULONG32);
typedef NTSTATUS(*ParserGetQosStatusDisk_t)(struct _ParserInstance *, void *);
typedef NTSTATUS(*ParserChangeTrackingGetParameters_t)(struct _ParserInstance *, CTParameters *);
typedef NTSTATUS(*ParserChangeTrackingStart_t)(struct _ParserInstance *, CTEnableParam *);
typedef NTSTATUS(*ParserChangeTrackingStop_t)(struct _ParserInstance *, const ULONG32 *, ULONG32 *);
typedef NTSTATUS(*ParserChangeTrackingSwitchLogs_t)(struct _ParserInstance *, CTSwitchLogParam *);
typedef NTSTATUS(*ParserNotifyRecoveryStatus_t)(struct _ParserInstance *, RecoveryStatusCompletionRoutine, void *);
typedef NTSTATUS(*ParserGetRecoveryStatus_t)(struct _ParserInstance *, ULONG32 *);
typedef NTSTATUS(*ParserPrepareMetaOperation_t)(struct _ParserInstance *, void *, ULONG64, void *, void **);
typedef NTSTATUS(*MetaOperationCallback_t)(void *);
typedef NTSTATUS(*ParserDeleteSnapshot_t)(struct _ParserInstance *, void *);
typedef NTSTATUS(*ParserQueryChanges_t)(struct _ParserInstance *, void *, ULONG32);

typedef struct {
	ULONG32											dwSize;
	INT												dwVersion;
	GUID											ParserId;
	PDRIVER_OBJECT									pDriverObject;
	ParserOpenDisk_t								pfnOpenDisk;
	ParserCloseDisk_t								pfnCloseDisk;
	ParserCallback_t								pfnCancelIo;
	ParserMountDisk_t								pfnMountDisk;
	ParserDismountDisk_t							pfnDismountDisk;
	ParserQueryMountStatusDisk_t					pfnQueryMountStatusDisk;
	ParserExecuteScsiRequestDisk_t					pfnExecuteScsiRequestDisk;
	ParserQueryInformationDisk_t					pfnQueryInformationDisk;
	ParserQuerySaveVersionDisk_t					pfnQuerySaveVersionDisk;		// IOCTL_VIRTUAL_DISK_QUERY_SAVE_VERSION
	ParserSaveDisk_t								pfnSaveDisk;					// IOCTL_VIRTUAL_DISK_SAVE
	ParserRestoreDisk_t								pfnRestoreDisk;					// IOCTL_VIRTUAL_DISK_RESTORE
	ParserSetBehaviourDisk_t						pfnSetBehaviourDisk;			// IOCTL_VIRTUAL_DISK_SET_FEATURES
	ParserCallback_t								pfnUnknown;						// unused

	ParserSetQosPolicyDisk_t						pfnSetQoSPolicyDisk;
	ParserGetQosStatusDisk_t						pfnGetQoSStatusDisk;

	ParserCallback_t								pfnChangeTrackingSetParameters;	// unsupported
	ParserChangeTrackingGetParameters_t				pfnChangeTrackingGetParameters;
	ParserChangeTrackingStart_t						pfnChangeTrackingStart;
	ParserChangeTrackingStop_t						pfnChangeTrackingStop;
	ParserChangeTrackingSwitchLogs_t				pfnChangeTrackingSwitchLogs;

	ParserCallback_t								pfnEnableResiliency;
	ParserNotifyRecoveryStatus_t					pfnNotifyRecoveryStatus;
	ParserGetRecoveryStatus_t						pfnGetRecoveryStatus;
	ParserCallback_t								pfnUnknown1;

	ParserPrepareMetaOperation_t					pfnPrepareMetaOperation;
	MetaOperationCallback_t							pfnStartMetaOperation;
	MetaOperationCallback_t							pfnCancelMetaOperation;
	MetaOperationCallback_t							pfnQueryMetaOperationProgress;
	MetaOperationCallback_t							pfnCleanupMetaOperation;
	ParserDeleteSnapshot_t							pfnDeleteSnapshot;
	ParserQueryChanges_t							pfnQueryChanges;

	ParserCallback_t								pfnUnknown2;					// Technical Preview 2 or higher

} VstorParserInfo;

#pragma warning(pop)
#pragma pack(pop)

NTSTATUS VstorRegisterParser(VstorParserInfo *);
NTSTATUS VstorCompleteScsiRequest(void *);
NTSTATUS VstorSendNotification(void *, INT);
NTSTATUS VstorSendMediaNotification(void *);
