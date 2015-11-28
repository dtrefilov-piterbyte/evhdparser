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

typedef NTSTATUS(*RecoveryStatusCompletionRoutine)(void *, ULONG32);

typedef NTSTATUS(*MetaOperationCompletionRoutine)(void *);

typedef enum {
	EMetaOperation_Snapshot,
	EMetaOperation_Resize,
	EMetaOperation_Complete,
	EMetaOperation_Extract
} EMetaOperation;

typedef struct {
	struct _ParserInstance *pParser;
	MetaOperationCompletionRoutine pfnCompletionRoutine;
	void *pCompletionInterface;
	void *SystemBuffer;
	PIRP pIrp;
} MetaOperation;

/* Forward declaration */
struct _ScsiPacket;
struct _ScsiPacketRequest;
struct _ScsiPacketInnerRequest;

typedef NTSTATUS(*StartIo_t)(PVOID, struct _ScsiPacket *, struct _ScsiPacketInnerRequest *, PMDL, BOOLEAN, PVOID);
typedef NTSTATUS(*SaveData_t)(PVOID, PVOID, ULONG32);
typedef NTSTATUS(*RestoreData_t)(PVOID, PVOID, ULONG32);

typedef struct _IoInfo
{
	void			*pIoInterface;
	StartIo_t		pfnStartIo;
	SaveData_t		pfnSaveData;
	RestoreData_t	pfnRestoreData;
} IoInfo;

typedef struct _MountInfo {
	INT				dwInnerBufferSize;
	BOOLEAN			bUnk;		// = FALSE;
	BOOLEAN			bFastPause;
	BOOLEAN			bFastClose;
} MountInfo;

typedef struct _RecoveryStatusInfo
{
	PIRP							pIrp;
	RecoveryStatusCompletionRoutine	pfnRecoveryStatusCallback;
	PVOID							pRecoverySubscriberInterface;
} RecoveryStatusInfo;


typedef NTSTATUS(*CompleteScsiRequest_t)(struct _ScsiPacket *, NTSTATUS);
typedef NTSTATUS(*SendNotification_t)(void *, INT);
typedef NTSTATUS(*SendMediaNotification_t)(void *);

typedef struct{
	INT dwVersion;
	INT dwFlags;
	UCHAR Unused[12];
	USHORT wFlags;
	USHORT wFlags2;
	GUID unkGuid;	// TODO:
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

typedef struct _ScsiPacketRequest {
	USHORT			wSize;					// 0x34
	UCHAR			SrbStatus;
	UCHAR			ScsiStatus;
	UCHAR			bUnk;
	UCHAR			ScsiPathId;
	UCHAR			ScsiTargetId;
	UCHAR			ScsiLun;
	UCHAR			CdbLength;				// 0x14
	UCHAR			SenseInfoBufferLength;	// 0x10
	UCHAR			bDataIn;				// 1 - read, 0 - write
	UCHAR			bFlags;
	ULONG32			DataTransferLength;
	union {
		SCSI_CDB_6	Cdb6;
		SCSI_CDB_10	Cdb10;
		SCSI_CDB_12	Cdb12;
		SCSI_CDB_16	Cdb16;
		SCSI_CDB_24 Cdb24;
	}				Sense;
	ULONG32			SrbFlags;
	ULONG32			f_2C;
	ULONG32			f_30;
} ScsiPacketRequest;

typedef struct _ScsiPacketInnerRequest {
	SCSI_REQUEST_BLOCK		Srb;
	struct _ParserInstance	*pParser;
} ScsiPacketInnerRequest;

typedef struct _ScsiPacket {
	ScsiPacketInnerRequest	*pInner;
	ScsiPacketRequest		*pRequest;		// Input buffer for IOCTL_VIRTUAL_DISK_SCSI_REQUEST
	PMDL					pMdl;
	union {
		SCSI_CDB_6	Cdb6;
		SCSI_CDB_10	Cdb10;
		SCSI_CDB_12	Cdb12;
		SCSI_CDB_16	Cdb16;
	}						Sense;
	BOOLEAN					bUseInternalSenseBuffer;
	BOOLEAN					bUnkFlag;
} ScsiPacket;

typedef enum _EDiskInfoType {
	EDiskInfo_Geometry = 1,
	EDiskInfo_Format = 0x101,
	EDiskInfo_Fragmentation = 0x102,
	EDiskInfo_ParentNameList = 0x103,
	EDiskInfo_PreloadDiskMetadata = 0x104
} EDiskInfoType;

typedef struct _DiskInfo_Format {
	INT				DiskType;		// 2 - Static, 3 - Dynamic, 4 - Differencing, etc...
	EDiskFormat		DiskFormat;
	INT				dwBlockSize;
	GUID			LinkageId;
	BOOLEAN			f_1C;
	BOOLEAN			bIsInUse;
	BOOLEAN			bIsFullyAllocated;
	UCHAR			_align;
	LONG64			qwDiskSize;
	GUID			DiskIdentifier;	// Page 83 data
} DiskInfo_Format;

typedef struct _DiskInfo_Size {
	ULONG64			qwDiskSize;
	INT				dwSectorSize;
	INT				dwNumSectors;
} DiskInfo_Geometry;

typedef NTSTATUS(*ParserOpenDisk_t)(PCUNICODE_STRING, ULONG32, GUID *, void *, struct _ParserInstance **);
typedef VOID(*ParserCloseDisk_t)(struct _ParserInstance *);
typedef NTSTATUS(*ParserCallback_t)(struct _ParserInstance *);
typedef NTSTATUS(*ParserMountDisk_t)(struct _ParserInstance *, UCHAR, PGUID, MountInfo *);
typedef NTSTATUS(*ParserDismountDisk_t)(struct _ParserInstance *);
typedef NTSTATUS(*ParserQueryMountStatusDisk_t)(struct _ParserInstance *, ULONG32);
typedef NTSTATUS(*ParserExecuteScsiRequestDisk_t)(struct _ParserInstance *, ScsiPacket *);
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
typedef NTSTATUS(*ParserPrepareMetaOperation_t)(struct _ParserInstance *, void *, MetaOperationCompletionRoutine, void *, MetaOperation **);
typedef NTSTATUS(*MetaOperationCallback_t)(MetaOperation *);
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
