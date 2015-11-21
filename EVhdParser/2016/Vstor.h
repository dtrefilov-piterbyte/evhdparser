#pragma once   
#include "ScsiOp.h"
#include "Vdrvroot.h"

#pragma pack(push, 1)

#pragma warning(push)													
#pragma warning(disable : 4214) // nonstandard extension: bitfield types other than int
#pragma warning(disable : 4201) // nonstandard extension: nameless struct/union

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
typedef NTSTATUS(*ParserSetQosConfigurationDisk_t)(struct _ParserInstance *, void *);
typedef NTSTATUS(*ParserGetQosInformationDisk_t)(struct _ParserInstance *, void *);

typedef struct {
	ULONG32							dwSize;
	INT								dwVersion;
	GUID							ParserId;
	PDRIVER_OBJECT					pDriverObject;
	ParserOpenDisk_t				pfnOpenDisk;
	ParserCloseDisk_t				pfnCloseDisk;
	ParserCallback_t				pfnUnk2D1A28;
	ParserMountDisk_t				pfnMountDisk;
	ParserDismountDisk_t			pfnDismountDisk;
	ParserQueryMountStatusDisk_t	pfnQueryMountStatusDisk;
	ParserExecuteScsiRequestDisk_t	pfnExecuteScsiRequestDisk;
	ParserQueryInformationDisk_t	pfnQueryInformationDisk;
	ParserQuerySaveVersionDisk_t	pfnQuerySaveVersionDisk;		// IOCTL_VIRTUAL_DISK_QUERY_SAVE_VERSION
	ParserSaveDisk_t				pfnSaveDisk;					// IOCTL_VIRTUAL_DISK_SAVE
	ParserRestoreDisk_t				pfnRestoreDisk;					// IOCTL_VIRTUAL_DISK_RESTORE
	ParserSetBehaviourDisk_t		pfnSetBehaviourDisk;			// IOCTL_VIRTUAL_DISK_SET_FEATURES
	ParserCallback_t				pfnUnknown;						// unused

	ParserSetQosConfigurationDisk_t	pfnSetQoSPolicyDisk;
	ParserGetQosInformationDisk_t	pfnGetQoSStatusDisk;

	ParserCallback_t				pfnChangeTrackingSetParameters;
	ParserCallback_t				pfnChangeTrackingGetParameters;
	ParserCallback_t				pfnChangeTrackingStart;
	ParserCallback_t				pfnChangeTrackingStop;
	ParserCallback_t				pfnChangeTrackingSwitchLogs;

	ParserCallback_t				pfnUnk2D1A24;
	ParserCallback_t				pfnUnk2D1A1C;
	ParserCallback_t				pfnUnk2D1A20;
	ParserCallback_t				pfnUnknown1;

	ParserCallback_t				pfnPrepareMetaOperation;
	ParserCallback_t				pfnStartMetaOperation;
	ParserCallback_t				pfnCancelMetaOperation;
	ParserCallback_t				pfnQueryMetaOperationProgress;
	ParserCallback_t				pfnCleanupMetaOpeation;
	ParserCallback_t				pfnDeleteSnapshot;
	ParserCallback_t				pfnQueryChanges;

	ParserCallback_t				pfnUnknown2;					// is present Windows Server 2016, absent on Windows 10

} VstorParserInfo;

#pragma warning(pop)
#pragma pack(pop)

NTSTATUS VstorRegisterParser(VstorParserInfo *);
NTSTATUS VstorCompleteScsiRequest(void *);
NTSTATUS VstorSendNotification(void *, INT);
NTSTATUS VstorSendMediaNotification(void *);
