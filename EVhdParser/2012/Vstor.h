#pragma once
#include "ScsiOp.h"

#pragma pack(push, 1)

struct _SrbPacketRequest;
struct _SrbPacketInnerRequest;
struct _SrbPacket;
struct _VstorSrbPrepareRequest;
struct _ParserInstance;

typedef NTSTATUS(*VstorPrepare_t)(struct _VstorSrbPrepareRequest *pVstorRequest, PVOID arg1, PVOID arg2);
typedef NTSTATUS(*VstorCompleteSrbRequest_t)(struct _SrbPacket *pPacket, NTSTATUS status);
typedef NTSTATUS(*VstorSaveData_t)(PVOID, PVOID);
typedef NTSTATUS(*VstorRestoreData_t)(PVOID, BOOLEAN);

typedef NTSTATUS(*VhdPrepare_t)(PVOID pOuterInterface, struct _SrbPacket *pPacket,
	struct _VstorSrbPrepareRequest *pVstorRequest, PVOID pfnCallDriver, PVOID arg1, PVOID arg2);
typedef NTSTATUS(*VhdCompleteRequest_t)(struct _SrbPacket *pPacket, NTSTATUS status);
typedef NTSTATUS(*VhdSaveData_t)(struct _ParserInstance *parser, PVOID arg1, PVOID arg2);
typedef NTSTATUS(*VhdRestoreData_t)(struct _ParserInstance *parser, PVOID arg);

typedef NTSTATUS(*SrbSaveData_t)(PVOID, PVOID, ULONG32);
typedef NTSTATUS(*SrbRestoreData_t)(PVOID, CONST VOID *, ULONG32);
typedef NTSTATUS(*SrbStartIo_t)(PVOID, struct _SrbPacket *, struct _SrbPacketInnerRequest *, PMDL);

typedef struct {
	VstorSaveData_t					pfnVstorSrbSave;
	VstorRestoreData_t				pfnVstorSrbRestore;
	VstorPrepare_t					pfnVstorSrbPrepare;
} SrbCallbackInfo;

typedef struct _PARSER_CAPABILITIES {
	ULONG32		dwUnk1;
	ULONG32		dwUnk2;
	ULONG32		dwUnk3;
	BOOLEAN		bMounted;
} PARSER_CAPABILITIES, *PPARSER_CAPABILITIES;

typedef struct _PARSER_SAVE_STATE_INFO {
	ULONG32		dwVersion;
	ULONG32		dwFinalSize;
} PARSER_SAVE_STATE_INFO, *PPARSER_SAVE_STATE_INFO;
			   
typedef struct _SrbPacketRequest {
	USHORT				wSize;
	UCHAR				SrbStatus;
	UCHAR				ScsiStatus;
	BOOLEAN				bUnk;
	UCHAR				ScsiPathId;
	UCHAR				ScsiTargetId;
	UCHAR				ScsiLun;
	UCHAR				CdbLength;
	UCHAR				SenseInfoBufferLength;
	BOOLEAN				bDataIn;
	UCHAR				bFlags;
	ULONG32				DataTransferLength;
	union {
		SCSI_CDB_6	Cdb6;
		SCSI_CDB_10	Cdb10;
		SCSI_CDB_12	Cdb12;
		SCSI_CDB_16	Cdb16;
		SCSI_CDB_24 Cdb24;
	}					Sense;
	ULONG32				SrbFlags;
} SrbPacketRequest;

typedef struct _SrbPacketInnerRequest {
	SCSI_REQUEST_BLOCK			Srb;
	PVOID						_;		// NOTE: keep this, needed for correct struct size
} SrbPacketInnerRequest;

typedef struct _SrbPacket {
	PVOID						pLinkedUnknown;
	struct _ParserInstance		*pParser;
	SrbPacketRequest			*pRequest;
	PMDL						pMdl;
	VstorCompleteSrbRequest_t	pfnCompleteSrbRequest;
	SrbPacketInnerRequest		*pInner;
	ULONG32						dwUnused;
	NTSTATUS					Status;
	ULONG64						DataTransferLength;
} SrbPacket;

typedef struct _VstorSrbPrepareRequest {
	UCHAR			_[0x18];
	PVOID			pLinkedUnk;
	PVOID			pOuterInterface;
	PVOID			pfnCallDriver;
	UCHAR			__[0x90];			// NOTE: keep this, needed for correct struct size
} VstorSrbPrepareRequest;

typedef NTSTATUS(*ParserInit_t)(SrbCallbackInfo *, PCUNICODE_STRING, ULONG32, void **);
typedef VOID(*ParserCleanup_t)(struct _ParserInstance *);
typedef VOID(*ParserGetGeometry_t)(struct _ParserInstance *, ULONG32 *, ULONG64 *, ULONG32 *);
typedef VOID(*ParserGetCapabilities_t)(struct _ParserInstance *, PPARSER_CAPABILITIES);
typedef NTSTATUS(*ParserMount_t)(struct _ParserInstance *, BOOLEAN, BOOLEAN);
typedef NTSTATUS(*ParserExecuteSrb_t)(SrbPacket *);
typedef NTSTATUS(*ParserBeginSave_t)(struct _ParserInstance *, PPARSER_SAVE_STATE_INFO);
typedef NTSTATUS(*ParserSaveData_t)(struct _ParserInstance *, PVOID, ULONG32 *);
typedef NTSTATUS(*ParserBeginRestore_t)(struct _ParserInstance *, PPARSER_SAVE_STATE_INFO);
typedef NTSTATUS(*ParserRestoreData_t)(struct _ParserInstance *, PVOID, ULONG32);
typedef NTSTATUS(*ParserSetCacheState_t)(struct _ParserInstance *, BOOLEAN);
typedef NTSTATUS(*ParserCustomCommand_t)(struct _ParserInstance *);

typedef struct {
	ULONG64							qwVersion;
	ULONG64							qwUnk1;
	ULONG64							qwUnk2;
	ULONG64							qwUnk3;
	PDRIVER_OBJECT					pDriverObject;
	ParserInit_t					pfnParserInit;
	ParserCleanup_t					pfnParserCleanup;
	ParserGetGeometry_t				pfnParserGetGeometry;
	ParserGetCapabilities_t			pfnParserGetCapabilities;
	ParserMount_t					pfnParserMount;
	ParserExecuteSrb_t				pfnParserExecuteSrb;
	ParserBeginSave_t				pfnParserBeginSave;
	ParserSaveData_t				pfnParserSaveData;
	ParserBeginRestore_t			pfnParserBeginRestore;
	ParserRestoreData_t				pfnParserRestoreData;
	ParserSetCacheState_t			pfnParserSetCacheState;
	ParserCustomCommand_t			pfnParserCustomCommand;
	ULONG32							dwBalancerId;
	ULONG32							_align;
} VstorParserInfo;
#pragma pack(pop)

NTSTATUS RegisterParser(VstorParserInfo *pParserInfo);

typedef struct {
	PVOID				pIoInterface;
	SrbStartIo_t		pfnStartIo;
	SrbSaveData_t		pfnSaveData;
	SrbRestoreData_t	pfnRestoreData;
	ULONG32				dwDiskSaveSize;
	ULONG32				_align;
} QoSInfo;
