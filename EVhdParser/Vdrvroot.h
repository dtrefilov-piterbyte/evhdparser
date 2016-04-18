#pragma once
#include <ntifs.h>

#define OPEN_FILE_RESILIENCY_INFO_EA_NAME "ClusteredApplicationInstance"
#define OPEN_FILE_VIRT_DISK_INFO_EA_NAME "VIRTDSK"

#pragma pack(push, 1)
typedef struct {
	ULONG32 dwVersion;
	ULONG32 dwDiskType;
	GUID DiskVendorId;
	ULONG32 Reserved1;
	ULONG32 Reserved2;
} FIND_SHIM_REQUEST;

typedef struct {
	ULONG32 eParserType;
	GUID ParserProviderId;
	ULONG32 Unk1;
	ULONG32 NameSizeInBytes;
	WCHAR szShimName[0x100];
} FIND_SHIM_RESPONSE;

typedef struct
{
	ULONG32			NextEntryOffset;
	UCHAR			Flags;
	UCHAR			EaNameLength;
	USHORT			EaValueLength;
	CHAR			EaName[sizeof(OPEN_FILE_RESILIENCY_INFO_EA_NAME)];			// 29
	GUID			EaValue;		// VmId
	UCHAR			_align[3];
} RESILIENCY_INFO_EA;

typedef enum
{
	EDiskFormat_Unknown,
	EDiskFormat_Iso,
	EDiskFormat_Vhd,
	EDiskFormat_Vhdx,
	EDiskFormat_Vhds	// VHD set
} EDiskFormat;

typedef struct {
	GUID			DevInterfaceClassGuid;
	EDiskFormat		DiskFormat;
	GUID			ParserProviderId;
	ULONG32			dwSize;
	ULONG32			OpenFlags;
	ULONG32			VirtualDiskAccessMask;
	ULONG32			RWDepth;
	ULONG32			OpenRequestVersion;
	ULONG32			GetInfoOnly;
	ULONG32			ReadOnly;
	ULONG32			Reserved1;
	ULONG32			Reserved2;
#if WINVEREX >= 0x10000000
	ULONG32			Reserved3;
	ULONG32			Reserved4;
	ULONG32			Reserved5;
#endif
} VIRT_DISK_INFO;

typedef struct {
	ULONG32				NextEntryOffset;
	UCHAR				Flags;
	UCHAR				EaNameLength;
	USHORT				EaValueLength;
	CHAR				szType[sizeof(OPEN_FILE_VIRT_DISK_INFO_EA_NAME)];	// 8
	VIRT_DISK_INFO		VirtDisk;
	RESILIENCY_INFO_EA	VmInfo;
} OPEN_DISK_EA;

NTSTATUS FindShimDevice(PUNICODE_STRING pShimName, PCUNICODE_STRING pDiskPath);
NTSTATUS OpenVhdmpDevice(HANDLE *pFileHandle, ULONG32 OpenFlags, PFILE_OBJECT *ppFileObject, PCUNICODE_STRING diskPath, const RESILIENCY_INFO_EA *pResiliency);

#pragma pack(pop)
