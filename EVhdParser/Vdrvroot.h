#pragma once
#include <ntifs.h>

#pragma pack(push, 1)
typedef struct {
	ULONG32 dwVersion;
	ULONG32 dwDiskType;
	GUID DiskVendorId;
	ULONG32 Reserved1;
	ULONG32 Reserved2;
} FindShimRequest;

typedef struct {
	ULONG32 eParserType;
	GUID ParserProviderId;
	ULONG32 Unk1;
	ULONG32 NameSizeInBytes;
	WCHAR szShimName[0x100];
} FindShimResponse;

typedef struct
{
	ULONG32			NextEntryOffset;
	UCHAR			Flags;
	UCHAR			EaNameLength;
	USHORT			EaValueLength;
	CHAR			EaName[29];			// "ClusteredApplicationInstance"
	GUID			EaValue;		// VmId
	UCHAR			_align[3];
} ResiliencyInfoEa;

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
	ULONG32			StorageType;
	ULONG32			OpenFlags;
	ULONG32			VirtualDiskAccessMask;
	ULONG32			RWDepth;
	ULONG32			OpenRequestVersion;
	ULONG32			GetInfoOnly;
	ULONG32			ReadOnly;
	ULONG64			Reserved;
} VirtDiskEa;

typedef struct {
	ULONG32				NextEntryOffset;
	UCHAR				Flags;
	UCHAR				EaNameLength;
	USHORT				EaValueLength;
	CHAR				szType[8];	// "VIRTDSK"
	VirtDiskEa			VirtDisk;
	ResiliencyInfoEa	VmInfo;
} OpenDiskEa;

NTSTATUS FindShimDevice(PUNICODE_STRING pShimName, PCUNICODE_STRING pDiskPath);
NTSTATUS OpenVhdmpDevice(HANDLE *pFileHandle, ULONG32 OpenFlags, PFILE_OBJECT *ppFileObject, PCUNICODE_STRING diskPath, const ResiliencyInfoEa *pResiliency);

#pragma pack(pop)
