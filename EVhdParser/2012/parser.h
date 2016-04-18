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
	BOOLEAN								bSuspendable;
	BOOLEAN								bQoSRegistered;
	BOOLEAN								bIoRegistered;
	BOOLEAN								bMounted;
	ULONG32								dwSectorSize;
	ULONG32								dwNumSectors;
	ULONG64								qwDiskSize;
	KSPIN_LOCK							SpinLock;
	PARSER_STATE						State;	// Unused, will be removed in 2012 R2
	VstorSaveData_t						pfnVstorSrbSave;
	VstorRestoreData_t					pfnVstorSrbRestore;
	VstorPrepare_t						pfnVstorSrbPrepare;
	QoSInfo								QoS;
} ParserInstance;

NTSTATUS EVhd_Init(SrbCallbackInfo *openInfo, PCUNICODE_STRING diskPath, ULONG32 OpenFlags, void **pInOutParam);
VOID EVhd_Cleanup(ParserInstance *parser);
VOID EVhd_GetGeometry(ParserInstance *parser, ULONG32 *pSectorSize, ULONG64 *pDiskSize, ULONG32 *pNumSectors);
VOID EVhd_GetCapabilities(ParserInstance *parser, PPARSER_CAPABILITIES pCapabilities);
NTSTATUS EVhd_Mount(ParserInstance *parser, BOOLEAN bMountDismount, BOOLEAN registerIoFlag);
NTSTATUS EVhd_ExecuteSrb(SCSI_PACKET *pPacket);
NTSTATUS EVhd_BeginSave(ParserInstance *parser, PPARSER_SAVE_STATE_INFO pSaveInfo);
NTSTATUS EVhd_SaveData(ParserInstance *parser, PVOID pData, ULONG32 *pSize);
NTSTATUS EVhd_BeginRestore(ParserInstance *parser, PPARSER_SAVE_STATE_INFO pSaveInfo);
NTSTATUS EVhd_RestoreData(ParserInstance *parser, PVOID pData, ULONG32 size);
NTSTATUS EVhd_SetCacheState(ParserInstance *parser, BOOLEAN newState);
