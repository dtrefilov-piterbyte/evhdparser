#pragma once   
#include "ScsiOp.h"
#include "Vdrvroot.h"

#pragma pack(push, 1)

#pragma warning(push)													
#pragma warning(disable : 4214) // nonstandard extension: bitfield types other than int
#pragma warning(disable : 4201) // nonstandard extension: nameless struct/union

typedef struct
{
	NTSTATUS (*pfnSetQosConfiguration)(void *, void *);
	NTSTATUS (*pfnGetQosInformation)(void *, void *);
	void *pQosInterface;
} PARSER_QOS_INFO;

/* Forward declaration */
struct _SCSI_REQUEST;
struct _STORVSP_REQUEST;

typedef NTSTATUS(*StartIo_t)(PVOID, struct _SCSI_REQUEST *, struct _STORVSP_REQUEST *, PMDL, BOOLEAN, PVOID);
typedef NTSTATUS (*SaveData_t)(PVOID, PVOID, ULONG32);
typedef NTSTATUS (*RestoreData_t)(PVOID, PVOID, ULONG32);

typedef NTSTATUS(*CompleteScsiRequest_t)(struct _SCSI_REQUEST *, NTSTATUS);
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
} PARSER_MOUNT_INFO;

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
	BOOLEAN			f_1C;
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

typedef struct _STORVSC_REQUEST {
	USHORT			wSize;					// 0x34
	UCHAR			SrbStatus;
	UCHAR			ScsiStatus;
	UCHAR			bUnk;
	UCHAR			ScsiPathId;
	UCHAR			ScsiTargetId;
	UCHAR			ScsiLun;
	UCHAR			CdbLength;				// 0x14
	UCHAR			SenseInfoBufferLength;	// 0x10
	UCHAR			bType;				// STORVCS_REQUEST_TYPE
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
} STORVSC_REQUEST;

typedef struct _STORVSP_REQUEST {
	SCSI_REQUEST_BLOCK		Srb;
	PVOID                   pContext;
} STORVSP_REQUEST;

typedef struct _SCSI_REQUEST {
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
} SCSI_REQUEST;

typedef NTSTATUS(*ParserOpenDisk_t)(PCUNICODE_STRING, ULONG32, GUID *, PVOID, PVOID *);
typedef VOID(*ParserCloseDisk_t)(PVOID);
typedef NTSTATUS(*ParserCallback_t)(PVOID);
typedef NTSTATUS(*ParserMountDisk_t)(PVOID, UCHAR, PARSER_MOUNT_INFO *);
typedef NTSTATUS(*ParserDismountDisk_t)(PVOID);
typedef NTSTATUS(*ParserQueryMountStatusDisk_t)(PVOID);
typedef NTSTATUS(*ParserExecuteScsiRequestDisk_t)(PVOID, PVOID);
typedef NTSTATUS(*ParserQueryInformationDisk_t)(PVOID, EDiskInfoType, INT, INT, PVOID, INT *);
typedef NTSTATUS(*ParserQuerySaveVersionDisk_t)(PVOID, INT *);
typedef NTSTATUS(*ParserSaveDisk_t)(PVOID, PVOID, ULONG32, ULONG32 *);
typedef NTSTATUS(*ParserRestoreDisk_t)(PVOID, INT, PVOID, ULONG32);
typedef NTSTATUS(*ParserSetBehaviourDisk_t)(PVOID, INT);
typedef NTSTATUS(*ParserSetQosConfigurationDisk_t)(PVOID, PVOID);
typedef NTSTATUS(*ParserGetQosInformationDisk_t)(PVOID, PVOID);

typedef struct {
	ULONG32							dwSize;
	INT								dwVersion;
	GUID							ParserId;
	PDRIVER_OBJECT					pDriverObject;
	ParserOpenDisk_t				pfnOpenDisk;
	ParserCloseDisk_t				pfnCloseDisk;
	ParserCallback_t				pfnCancelIo;						// Unused
	ParserMountDisk_t				pfnMountDisk;
	ParserDismountDisk_t			pfnDismountDisk;
	ParserQueryMountStatusDisk_t	pfnQueryMountStatusDisk;
	ParserExecuteScsiRequestDisk_t	pfnExecuteScsiRequestDisk;
	ParserQueryInformationDisk_t	pfnQueryInformationDisk;
	ParserQuerySaveVersionDisk_t	pfnQuerySaveVersionDisk;		// IOCTL_VIRTUAL_DISK_QUERY_SAVE_VERSION
	ParserSaveDisk_t				pfnSaveDisk;					// IOCTL_VIRTUAL_DISK_SAVE
	ParserRestoreDisk_t				pfnRestoreDisk;					// IOCTL_VIRTUAL_DISK_RESTORE
	ParserSetBehaviourDisk_t		pfnSetBehaviourDisk;			// IOCTL_VIRTUAL_DISK_SET_FEATURES
	ParserCallback_t				pfnUnknown1;					// IOCTL_VIRTUAL_DISK_UNK_0072
	ParserSetQosConfigurationDisk_t	pfnSetQosConfigurationDisk;
	ParserGetQosInformationDisk_t	pfnGetQosInformationDisk;
} VstorParserInfo;

#pragma warning(pop)
#pragma pack(pop)

NTSTATUS VstorRegisterParser(VstorParserInfo *);
NTSTATUS VstorCompleteScsiRequest(struct _SCSI_REQUEST *);
NTSTATUS VstorSendNotification(void *, INT);
NTSTATUS VstorSendMediaNotification(void *);
