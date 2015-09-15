#pragma once 
#include "Vstor.h"

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
	ULONG64								qwUnk1;
	ULONG64								qwUnk2;
	USHORT								wUnk3;
	BOOLEAN								bCacheState;
	VstorSaveData_t						pfnVstorSrbSave;
	VstorRestoreData_t					pfnVstorSrbRestore;
	VstorPrepare_t						pfnVstorSrbPrepare;
	QoSInfo								QoS;
} ParserInstance;

NTSTATUS EVhdInit(SrbCallbackInfo *openInfo, PCUNICODE_STRING diskPath, ULONG32 OpenFlags, void **pInOutParam);
VOID EVhdCleanup(ParserInstance *parser);
VOID EVhdGetGeometry(ParserInstance *parser, ULONG32 *pSectorSize, ULONG64 *pDiskSize, ULONG32 *pNumSectors);
VOID EVhdGetCapabilities(ParserInstance *parser, ParserCapabilities *pCapabilities);
NTSTATUS EVhdMount(ParserInstance *parser, BOOLEAN bMountDismount, BOOLEAN registerIoFlag);
NTSTATUS EVhdExecuteSrb(SrbPacket *pPacket);
NTSTATUS EVhdBeginSave(ParserInstance *parser, SaveInfo *pSaveInfo);
NTSTATUS EVhdSaveData(ParserInstance *parser, SaveDataHeader *pData, ULONG32 *pSize);
NTSTATUS EVhdBeginRestore(ParserInstance *parser, const SaveInfo *pSaveInfo);
NTSTATUS EVhdRestoreData(ParserInstance *parser, const SaveDataHeader *pData, ULONG32 size);
NTSTATUS EVhdSetCacheState(ParserInstance *parser, BOOLEAN newState);
