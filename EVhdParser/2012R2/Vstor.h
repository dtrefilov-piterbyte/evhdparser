#pragma once   
#include "ScsiOp.h"
#include "Vdrvroot.h"

#pragma pack(push, 1)

#pragma warning(push)													
#pragma warning(disable : 4214) // nonstandard extension: bitfield types other than int
#pragma warning(disable : 4201) // nonstandard extension: nameless struct/union

#if WINVEREX >= 0x10000000

typedef enum {
    SnapshotTypeNone = 0,
    SnapshotTypeVM = 1,
    SnapshotTypeCDP = 3,
    SnapshotTypeWritable = 4

} ESnapshotType;

typedef enum {
    SnapshotStageNone,  // Initial
    SnapshotStageInitialize,
    SnapshotStageBlockIO,
    SnapshotStageSwitchObjectStore,
    SnapshotStageUnblockIO,
    SnapshotStageFinalize
} ESnapshotStage;

typedef struct {
    ESnapshotType   SnapshotType;
    ULONG32		    Padding;
    ULONG64		    qwUncommitedSize;	// Current size of the volatile backing store
} CTParameters;

typedef struct {
    ULONG32		SnapshotType;
    ULONG32		dwInnerBufferSize;
    GUID		CdpSnapshotId;		// CDP stands for continuos data protection
    ULONG64		qwMaximumDiffSize;	// maximum size of the volatile backing store
    UCHAR		SkipSnapshoting;
    UCHAR		_align[3];
    ULONG32		FeatureFlags;
} CTStartParam;

typedef struct {
    ULONG32		SnapshotType;
    ULONG32		dwInnerBufferSize;
    GUID		OfflineCdpId;
    ULONG32		CTState;			// ESnapshotStage
    ULONG32		FeatureFlags;
    GUID		CdpSnapshotId;
} CTSwitchLogParam;

typedef NTSTATUS(*QoSStatusCompletionRoutine)(NTSTATUS, void *, void *);
typedef NTSTATUS(*RecoveryStatusCompletionRoutine)(void *, NTSTATUS);
typedef NTSTATUS(*MetaOperationCompletionRoutine)(void *);

typedef enum {
    EMetaOperation_Snapshot,
    EMetaOperation_Resize,
    EMetaOperation_Optimize,
    EMetaOperation_ExtractVHD,
    EMetaOperation_ConvertToVHDS
} EMetaOperation;

typedef struct {
    EMetaOperation	Type;
    ULONG			Padding;    // must be set to 0
    IO_STATUS_BLOCK	Status;
    UCHAR			Reserved[16];

    // User defined request
} MetaOperationBuffer;

typedef struct {
    struct _ParserInstance *pParser;
    MetaOperationCompletionRoutine pfnCompletionRoutine;
    void *pInterface;
    MetaOperationBuffer *pBuffer;
    PIRP pIrp;
} MetaOperation;

typedef struct {
    GUID OfflineCdpAppId;
    ULONG dwUnk3;
    ULONG FeatureFlags;
    GUID CdpSnapshotId;
} META_OPERATION_CDP_PARAMETERS;

typedef struct {
    ESnapshotType SnapshotType;
    ULONG Flags;
    ESnapshotStage Stage1;
    ULONG Stage2;
    ULONG Stage3;
    ULONG Stage4;
    ULONG Stage5;
    ULONG Stage6;
    GUID SnapshotId;
    ULONG ParametersPayloadSize;
    META_OPERATION_CDP_PARAMETERS CdpParameters;
} META_OPERATION_CREATE;

typedef struct {
    GUID SourceSnapshotId;
    GUID SourceLimitSnaphotId;
    ULONG dwSize;
    ULONG dwInnerSize;
} VhdSetExtractInnerRequest;

typedef struct {
    ESnapshotType SnapshotType;
    ULONG Flags;
    VhdSetExtractInnerRequest Inner;
} META_OPERATION_EXTRACT;

#else

typedef struct
{
	NTSTATUS (*pfnSetQosConfiguration)(PVOID, PVOID);
	NTSTATUS (*pfnGetQosInformation)(PVOID, PVOID);
	void *pQosInterface;
} PARSER_QOS_INFO;

#endif

/* Forward declaration */
struct _SCSI_PACKET;
struct _STORVSP_REQUEST;

typedef NTSTATUS(*StartIo_t)(PVOID, struct _SCSI_PACKET *, struct _STORVSP_REQUEST *, PMDL, BOOLEAN, PVOID);
typedef NTSTATUS (*SaveData_t)(PVOID, PVOID, ULONG32);
typedef NTSTATUS (*RestoreData_t)(PVOID, PVOID, ULONG32);

typedef NTSTATUS(*CompleteScsiRequest_t)(struct _SCSI_PACKET *, NTSTATUS);
typedef NTSTATUS(*SendNotification_t)(void *, INT);
typedef NTSTATUS(*SendMediaNotification_t)(void *);

typedef struct _PARSER_IO_INFO
{
	void			*pIoInterface;
	StartIo_t		pfnStartIo;
	SaveData_t		pfnSaveData;
	RestoreData_t	pfnRestoreData;
} PARSER_IO_INFO;

typedef struct _PARSER_MOUNT_INFO {
	INT				dwInnerBufferSize;
	BOOLEAN			bUnk;
    BOOLEAN			bFastPause;
    BOOLEAN			bFastClose;
} PARSER_MOUNT_INFO;

typedef struct{
    INT dwVersion;
    INT dwFlags;
    UCHAR Unused[12];
    USHORT wFlags;
    USHORT wMountFlags; // 1 - read only, 4 - ignore sync requests

#if WINVEREX >= 0x10000000
    GUID SnapshotId;
#endif

    CompleteScsiRequest_t pfnCompleteScsiRequest;
    SendMediaNotification_t pfnSendMediaNotification;
    SendNotification_t pfnSendNotification;
    void *pVstorInterface;
} REGISTER_IO_REQUEST;

typedef struct {
    INT dwDiskSaveSize;
    INT dwExtensionBufferSize;
    PARSER_IO_INFO Io;
} REGISTER_IO_RESPONSE;

typedef enum _EDiskInfoType {
	EDiskInfo_Geometry				= 1,
	EDiskInfo_Format				= 0x101,
	EDiskInfo_Fragmentation			= 0x102,
	EDiskInfo_ParentNameList		= 0x103,
	EDiskInfo_PreloadDiskMetadata	= 0x104
} EDiskInfoType;

typedef struct _DISK_INFO_FORMAT {
	INT				DiskType;		// 2 - Static, 3 - Dynamic, 4 - Differencing, etc...
	EDiskFormat		DiskFormat;
	INT				dwBlockSize;
	GUID			LinkageId;
	BOOLEAN			IsRemote;
	BOOLEAN			bIsInUse;
	BOOLEAN			bIsFullyAllocated;
	UCHAR			_align;
	LONG64			qwDiskSize;
	GUID			DiskIdentifier;	// Page 83 data
} DISK_INFO_FORMAT;

typedef struct _DISK_INFO_GEOMETRY {
	ULONG64			qwDiskSize;
	INT				dwSectorSize;
	INT				dwNumSectors;
} DISK_INFO_GEOMETRY;

enum STORVCS_REQUEST_TYPE {
    WRITE_TYPE,
    READ_TYPE,
    UNKNOWN_TYPE,
};

// see vmscsi_win8_extension in storvsc_drv.c
typedef struct _STORVSC_WIN8_EXTENSION {
    ULONG32         SrbFlags;
    ULONG32         Timeout;
    ULONG           QueueSortEy;
} STORVSC_WIN8_EXTENSION;

// see vmscsi_request in storvsc_drv.c
typedef struct _STORVSC_REQUEST {
	USHORT			wSize;					// 0x34
	UCHAR			SrbStatus;
	UCHAR			ScsiStatus;
	UCHAR			ScsiPortNumber;
	UCHAR			ScsiPathId;
	UCHAR			ScsiTargetId;
	UCHAR			ScsiLun;
	UCHAR			CdbLength;				// 0x14
	UCHAR			SenseInfoBufferLength;	// 0x10
	UCHAR			bDataIn;
	UCHAR			bReserved;
	ULONG32			DataTransferLength;
	union {
		SCSI_CDB_6	Cdb6;
		SCSI_CDB_10	Cdb10;
		SCSI_CDB_12	Cdb12;
		SCSI_CDB_16	Cdb16;
		SCSI_CDB_24 Cdb24;
	}				Sense;
    STORVSC_WIN8_EXTENSION Extension;
} STORVSC_REQUEST;

typedef struct _STORVSP_REQUEST {
	SCSI_REQUEST_BLOCK		Srb;
	PVOID                   pContext;
} STORVSP_REQUEST;

typedef struct _SCSI_PACKET {
    STORVSP_REQUEST     	*pVspRequest;
    STORVSC_REQUEST 		*pVscRequest;		// Input buffer for IOCTL_VIRTUAL_DISK_SCSI_REQUEST
	PMDL					pMdl;
	union {
		SCSI_CDB_6	Cdb6;
		SCSI_CDB_10	Cdb10;
		SCSI_CDB_12	Cdb12;
		SCSI_CDB_16	Cdb16;
	}						Sense;
	BOOLEAN					bUseInternalSenseBuffer;
	BOOLEAN					bUnkFlag;
} SCSI_PACKET;

typedef NTSTATUS(*ParserOpenDisk_t)(PCUNICODE_STRING, ULONG32, GUID *, PVOID, PVOID *);
typedef VOID(*ParserCloseDisk_t)(PVOID);
typedef NTSTATUS(*ParserCallback_t)(PVOID);
#if WINVEREX < 0x10000000
typedef NTSTATUS(*ParserMountDisk_t)(PVOID, UCHAR, PARSER_MOUNT_INFO *);
#else
typedef NTSTATUS(*ParserMountDisk_t)(PVOID, UCHAR, PGUID, PARSER_MOUNT_INFO *);
#endif
typedef NTSTATUS(*ParserDismountDisk_t)(PVOID);
typedef NTSTATUS(*ParserQueryMountStatusDisk_t)(PVOID);
typedef NTSTATUS(*ParserExecuteScsiRequestDisk_t)(PVOID, PVOID);
typedef NTSTATUS(*ParserQueryInformationDisk_t)(PVOID, EDiskInfoType, INT, INT, PVOID, INT *);
typedef NTSTATUS(*ParserQuerySaveVersionDisk_t)(PVOID, INT *);
typedef NTSTATUS(*ParserSaveDisk_t)(PVOID, PVOID, ULONG32, ULONG32 *);
typedef NTSTATUS(*ParserRestoreDisk_t)(PVOID, INT, PVOID, ULONG32);
typedef NTSTATUS(*ParserSetBehaviourDisk_t)(PVOID, INT);

#if NTDDI_VERSION >= NTDDI_WINBLUE
#if WINVEREX < 0x10000000

typedef NTSTATUS(*ParserSetQosConfigurationDisk_t)(PVOID, PVOID);
typedef NTSTATUS(*ParserGetQosInformationDisk_t)(PVOID, PVOID);

#else

typedef NTSTATUS(*ParserSetQosPolicyDisk_t)(PVOID, PVOID, ULONG32);
typedef NTSTATUS(*ParserGetQosStatusDisk_t)(PVOID, PVOID);

typedef NTSTATUS(*ParserChangeTrackingGetParameters_t)(PVOID, CTParameters *);
typedef NTSTATUS(*ParserChangeTrackingStart_t)(PVOID, CTStartParam *);
typedef NTSTATUS(*ParserChangeTrackingStop_t)(PVOID, const ULONG32 *, ULONG32 *);
typedef NTSTATUS(*ParserChangeTrackingSwitchLogs_t)(PVOID, CTSwitchLogParam *);
typedef NTSTATUS(*ParserNotifyRecoveryStatus_t)(PVOID, RecoveryStatusCompletionRoutine, PVOID);
typedef NTSTATUS(*ParserGetRecoveryStatus_t)(PVOID, ULONG32 *);
typedef NTSTATUS(*ParserPrepareMetaOperation_t)(PVOID, MetaOperationBuffer *, MetaOperationCompletionRoutine, PVOID, MetaOperation **);
typedef NTSTATUS(*MetaOperationCallback_t)(MetaOperation *);
typedef NTSTATUS(*ParserDeleteSnapshot_t)(PVOID, PVOID);
typedef NTSTATUS(*ParserQueryChanges_t)(PVOID, PVOID, ULONG32);
#endif
#endif

typedef struct {
	ULONG32							                dwSize;
	INT								                dwVersion;
	GUID							                ParserId;
	PDRIVER_OBJECT					                pDriverObject;
	ParserOpenDisk_t				                pfnOpenDisk;
	ParserCloseDisk_t				                pfnCloseDisk;
	ParserCallback_t				                pfnCancelIo;						// Unused
	ParserMountDisk_t				                pfnMountDisk;
	ParserDismountDisk_t			                pfnDismountDisk;
	ParserQueryMountStatusDisk_t	                pfnQueryMountStatusDisk;
	ParserExecuteScsiRequestDisk_t	                pfnExecuteScsiRequestDisk;
	ParserQueryInformationDisk_t	                pfnQueryInformationDisk;
	ParserQuerySaveVersionDisk_t	                pfnQuerySaveVersionDisk;		// IOCTL_VIRTUAL_DISK_QUERY_SAVE_VERSION
	ParserSaveDisk_t				                pfnSaveDisk;					// IOCTL_VIRTUAL_DISK_SAVE
	ParserRestoreDisk_t				                pfnRestoreDisk;					// IOCTL_VIRTUAL_DISK_RESTORE
	ParserSetBehaviourDisk_t		                pfnSetBehaviourDisk;			// IOCTL_VIRTUAL_DISK_SET_FEATURES
	ParserCallback_t				                pfnUnknown;					    // IOCTL_VIRTUAL_DISK_UNK_0072
#if NTDDI_VERSION >= NTDDI_WINBLUE
#if WINVEREX < 0x10000000

	ParserSetQosConfigurationDisk_t	pfnSetQosConfigurationDisk;
    ParserGetQosInformationDisk_t	pfnGetQosInformationDisk;

#else

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

#endif
#endif
} VSTOR_PARSER_INFO;

#pragma warning(pop)
#pragma pack(pop)

NTSTATUS VstorRegisterParser(VSTOR_PARSER_INFO *);
NTSTATUS VstorCompleteScsiRequest(PVOID);
NTSTATUS VstorSendNotification(PVOID, INT);
NTSTATUS VstorSendMediaNotification(PVOID);
