#pragma once 
#include "Vstor.h"

typedef struct _PARSER_STATE {
	ULONG64		qwUnk1;
	ULONG64		qwUnk2;
	USHORT		wUnk3;
	UCHAR		bCache;
	UCHAR		Reserved[13];
} PARSER_STATE, *PPARSER_STATE;

typedef struct _ParserInstance {
	HANDLE								FileHandle;
	PFILE_OBJECT						pVhdmpFileObject;
	UCHAR								ScsiPathId;
	UCHAR								ScsiTargetId;
	UCHAR								ScsiLun;
	BOOLEAN								bSynchronouseIo;
	BOOLEAN								bQoSRegistered;
	BOOLEAN								bIoRegistered;
	BOOLEAN								bMounted;
	ULONG32								dwSectorSize;
	ULONG32								dwNumSectors;
	ULONG64								qwDiskSize;
	KSPIN_LOCK							SpinLock;
	PARSER_STATE						State;	// Unused, will be removed 2012 R2
	VstorSaveData_t						pfnVstorSrbSave;
	VstorRestoreData_t					pfnVstorSrbRestore;
	VstorPrepare_t						pfnVstorSrbPrepare;
	QoSInfo								QoS;
} ParserInstance;

NTSTATUS EVhdInit(SrbCallbackInfo *openInfo, PCUNICODE_STRING diskPath, ULONG32 OpenFlags, void **pInOutParam);
VOID EVhdCleanup(ParserInstance *parser);
VOID EVhdGetGeometry(ParserInstance *parser, ULONG32 *pSectorSize, ULONG64 *pDiskSize, ULONG32 *pNumSectors);
VOID EVhdGetCapabilities(ParserInstance *parser, PPARSER_CAPABILITIES pCapabilities);
NTSTATUS EVhdMount(ParserInstance *parser, BOOLEAN bMountDismount, BOOLEAN registerIoFlag);
NTSTATUS EVhdExecuteSrb(SCSI_PACKET *pPacket);
NTSTATUS EVhdBeginSave(ParserInstance *parser, PPARSER_SAVE_STATE_INFO pSaveInfo);
NTSTATUS EVhdSaveData(ParserInstance *parser, PVOID pData, ULONG32 *pSize);
NTSTATUS EVhdBeginRestore(ParserInstance *parser, PPARSER_SAVE_STATE_INFO pSaveInfo);
NTSTATUS EVhdRestoreData(ParserInstance *parser, PVOID pData, ULONG32 size);
NTSTATUS EVhdSetCacheState(ParserInstance *parser, BOOLEAN newState);
