#include "stdafx.h"
#include "Vdrvroot.h" 
#include <initguid.h>
#include "Guids.h"
#include "Ioctl.h"
#include "utils.h"
#include <stddef.h>
#include "Log.h"

NTSTATUS FindShimDevice(PUNICODE_STRING pShimName, PCUNICODE_STRING pDiskPath)
{
	NTSTATUS status = STATUS_SUCCESS;
	OBJECT_ATTRIBUTES ObjectAttributes = { 0 };
	UNICODE_STRING DriveEnumeratorSymbolicLink = { 0 };
	PFILE_OBJECT pFileObject = NULL;
	PDEVICE_OBJECT pDeviceObject = NULL;
	PZZWSTR SymbolicLinkList = NULL;
	FindShimRequest inputBuffer = { 0 };
	FindShimResponse outputBuffer = { 0 };
	SIZE_T ShimNameLength = 0;

	if (!pShimName)
	{
		status = STATUS_INVALID_PARAMETER;
		goto cleanup_failure;
	}
	else
		pShimName->Buffer = NULL;

	status = IoGetDeviceInterfaces(&GUID_DEVINTERFACE_SURFACE_VIRTUAL_DRIVE, NULL, 0, &SymbolicLinkList);
	if (!NT_SUCCESS(status))
	{
        LOG_FUNCTION(LL_FATAL, LOG_CTG_GENERAL, "Failed to find installed Hyper-V Virtual Drive Enumerator\n");
		goto cleanup_failure;
	}

	RtlInitUnicodeString(&DriveEnumeratorSymbolicLink, SymbolicLinkList);

	ObjectAttributes.Length = sizeof(OBJECT_ATTRIBUTES);
	ObjectAttributes.ObjectName = &DriveEnumeratorSymbolicLink;
	ObjectAttributes.Attributes = OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE;

	status = IoGetDeviceObjectPointer(&DriveEnumeratorSymbolicLink, GENERIC_ALL, &pFileObject, &pDeviceObject);
	if (!NT_SUCCESS(status))
	{
        LOG_FUNCTION(LL_FATAL, LOG_CTG_GENERAL, "IoGetDeviceObjectPointer %S failed with error = 0x%0x\n", DriveEnumeratorSymbolicLink.Buffer, status);
		goto cleanup_failure;
	}


	inputBuffer.dwVersion = 1;
	inputBuffer.dwDiskType = 2;
	inputBuffer.DiskVendorId = VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT;
	inputBuffer.Reserved1 = 0;
	inputBuffer.Reserved2 = 0;

	status = SynchronouseCall(pFileObject, IOCTL_STORAGE_VHD_FIND_SHIM, &inputBuffer, sizeof(FindShimRequest),
		&outputBuffer, sizeof(FindShimResponse));
	if (!NT_SUCCESS(status))
	{
        LOG_FUNCTION(LL_FATAL, LOG_CTG_GENERAL, "SynchronouseCall failed with error 0x%0x\n", status);
		goto cleanup_failure;
	}

	/* Convert UNC path from '\\.\' to '\??\' */
	if (outputBuffer.NameSizeInBytes > 8 &&
		outputBuffer.szShimName[0] == L'\\' &&
		outputBuffer.szShimName[1] == L'\\' &&
		outputBuffer.szShimName[2] == L'.' &&
		outputBuffer.szShimName[3] == L'\\')
	{
		outputBuffer.szShimName[1] = L'?';
		outputBuffer.szShimName[2] = L'?';
	}

	ShimNameLength = outputBuffer.NameSizeInBytes;
	if (pDiskPath)
		ShimNameLength += pDiskPath->Length + 4 * sizeof(WCHAR);

	if (ShimNameLength > MAXUSHORT)
	{
		status = STATUS_INTEGER_OVERFLOW;
		goto cleanup_failure;
	}

	pShimName->MaximumLength = (USHORT)ShimNameLength;
	pShimName->Length = 0;
	pShimName->Buffer = ExAllocatePool(NonPagedPool, ShimNameLength + 1);
	if (!pShimName->Buffer)
	{
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto cleanup_failure;
	}
	pShimName->Length = (USHORT)outputBuffer.NameSizeInBytes - sizeof(WCHAR);
	memmove(pShimName->Buffer, outputBuffer.szShimName, outputBuffer.NameSizeInBytes);
	if (pDiskPath)
	{
		if (pShimName->Buffer[pShimName->Length / sizeof(WCHAR) - 1] != '\\')
			RtlAppendUnicodeToString(pShimName, L"\\");
		RtlAppendUnicodeStringToString(pShimName, pDiskPath);
	}

	goto cleanup;

cleanup_failure:
	if (pShimName && pShimName->Buffer)
		ExFreePool(pShimName->Buffer);
cleanup:
	if (SymbolicLinkList) ExFreePool(SymbolicLinkList);
	if (pFileObject) ObDereferenceObject(pFileObject);

	return status;
}

NTSTATUS GetIsDifferencing(HANDLE FileHandle, __out BOOLEAN *pResult)
{
	NTSTATUS status = STATUS_SUCCESS;
	PFILE_OBJECT pFileObject = NULL;
	EDiskInfoType Request = EDiskInfoType_Type;
	DiskInfoResponse Response = { 0 };
	Request = EDiskInfoType_Type;

	status = ObReferenceObjectByHandle(FileHandle, 0, *IoFileObjectType, KernelMode, (PVOID*)&pFileObject, NULL);
	if (!NT_SUCCESS(status))
	{
        LOG_FUNCTION(LL_FATAL, LOG_CTG_GENERAL, "ObReferenceObjectByHandle() failed. 0x%0X\n", status);
		goto cleanup;
	}

	status = SynchronouseCall(pFileObject, IOCTL_STORAGE_VHD_GET_INFORMATION, &Request, sizeof(Request),
		&Response, sizeof(DiskInfoResponse));
	if (!NT_SUCCESS(status))
	{
        LOG_FUNCTION(LL_FATAL, LOG_CTG_GENERAL, "Failed to retreive disk type. 0x%0X\n", status);
		goto cleanup;
	}
	*pResult = Response.vals[0].dwLow == 4;
cleanup:
	if (pFileObject) ObDereferenceObject(pFileObject);

	return status;
}


// TODO: OpenFlags are from set OPEN_VIRTUAL_DISK_FLAG?
NTSTATUS OpenVhdmpDevice(HANDLE *pFileHandle, ULONG32 OpenFlags, PFILE_OBJECT *ppFileObject, PCUNICODE_STRING diskPath,
	const ResiliencyInfoEa *pResiliency)
{
	NTSTATUS status = STATUS_SUCCESS;
	HANDLE FileHandle = NULL;
	OBJECT_ATTRIBUTES ObjectAttributes = { 0 };
	IO_STATUS_BLOCK StatusBlock = { 0 };
	UNICODE_STRING VhdmpPath = { 0 };
	PFILE_OBJECT pFileObject = NULL;
	OpenDiskEa ea = { 0 };
	BOOLEAN getInfoOnly = OpenFlags & 1;

	status = FindShimDevice(&VhdmpPath, diskPath);
	if (!NT_SUCCESS(status))
	{
        LOG_FUNCTION(LL_FATAL, LOG_CTG_GENERAL, "FindShimDevice failed\n");
		goto cleanup_failure;
	}

	ea.NextEntryOffset = 0;
	ea.Flags = FILE_NEED_EA;
	ea.EaNameLength = sizeof(ea.szType) - 1;
	ea.EaValueLength = sizeof(VirtDiskInfo);
	strncpy(ea.szType, OPEN_FILE_VIRT_DISK_INFO_EA_NAME, sizeof(ea.szType));
	ea.VirtDisk.DevInterfaceClassGuid = GUID_DEVINTERFACE_SURFACE_VIRTUAL_DRIVE;
	ea.VirtDisk.DiskFormat = EDiskFormat_Vhd;
	ea.VirtDisk.ParserProviderId = VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT;
	ea.VirtDisk.dwSize = sizeof(VirtDiskInfo);
#if NTDDI_VERSION >= NTDDI_WINBLUE
	ea.VirtDisk.OpenFlags = 0;
	if (!getInfoOnly)
	{
		ea.VirtDisk.OpenFlags |= 0x80000000;    // OPEN_MODE_SHARED
#if WINVEREX >= 0x10000000
		if (OpenFlags & 0x20)
			ea.VirtDisk.OpenFlags |= 0x8;
		else if (OpenFlags & 0x10)
			ea.VirtDisk.OpenFlags |= 0x20;
#endif
	}
	else if (0 == (OpenFlags & 2))
	{
		ea.VirtDisk.OpenFlags = 1;
	}

#elif NTDDI_VERSION == NTDDI_WIN8
	ea.VirtDisk.OpenFlags = OpenFlags ? 0 : 1;
	(void)getInfoOnly;
#endif
	ea.VirtDisk.VirtualDiskAccessMask = 0;
	ea.VirtDisk.OpenRequestVersion = 2;
	ea.VirtDisk.ReadOnly = FALSE;
#if NTDDI_VERSION >= NTDDI_WINBLUE
	if (getInfoOnly)
	{
		ea.VirtDisk.GetInfoOnly = getInfoOnly;
		ea.VirtDisk.RWDepth = FALSE;
	}
	else
	{
		ea.VirtDisk.GetInfoOnly = FALSE;
#if WINVEREX >= 0x10000000
		ea.VirtDisk.RWDepth = OpenFlags & 0x8 ? FALSE : TRUE;
#endif
	}

#endif

	if (pResiliency)
	{
		ea.NextEntryOffset = offsetof(OpenDiskEa, VmInfo);
		memmove(&ea.VmInfo, pResiliency, sizeof(ResiliencyInfoEa));
	}

	if (VhdmpPath.Length > 6 &&
		'i' == (VhdmpPath.Buffer[VhdmpPath.Length / sizeof(WCHAR) - 3] | 0x20) &&	// lowercase
		's' == (VhdmpPath.Buffer[VhdmpPath.Length / sizeof(WCHAR) - 2] | 0x20) &&
		'o' == (VhdmpPath.Buffer[VhdmpPath.Length / sizeof(WCHAR) - 1] | 0x20))
	{
		VhdmpPath.Length -= 6;
		RtlAppendUnicodeToString(&VhdmpPath, L"EMPTY");

		ea.VirtDisk.DiskFormat = EDiskFormat_Iso;
		ea.VirtDisk.ReadOnly = TRUE;
	}

	if (VhdmpPath.Length > 8 &&
		'v' == (VhdmpPath.Buffer[VhdmpPath.Length / sizeof(WCHAR) - 4] | 0x20) &&
		'h' == (VhdmpPath.Buffer[VhdmpPath.Length / sizeof(WCHAR) - 3] | 0x20) &&
		'd' == (VhdmpPath.Buffer[VhdmpPath.Length / sizeof(WCHAR) - 2] | 0x20) &&
		'x' == (VhdmpPath.Buffer[VhdmpPath.Length / sizeof(WCHAR) - 1] | 0x20))
	{
		ea.VirtDisk.DiskFormat = EDiskFormat_Vhdx;
	}

#if WINVEREX >= 0x10000000
	if (VhdmpPath.Length > 8 &&
		'v' == (VhdmpPath.Buffer[VhdmpPath.Length / sizeof(WCHAR) - 4] | 0x20) &&
		'h' == (VhdmpPath.Buffer[VhdmpPath.Length / sizeof(WCHAR) - 3] | 0x20) &&
		'd' == (VhdmpPath.Buffer[VhdmpPath.Length / sizeof(WCHAR) - 2] | 0x20) &&
		's' == (VhdmpPath.Buffer[VhdmpPath.Length / sizeof(WCHAR) - 1] | 0x20))
	{
		ea.VirtDisk.DiskFormat = EDiskFormat_Vhds;
	}
#endif

	ObjectAttributes.Length = sizeof(OBJECT_ATTRIBUTES);
	ObjectAttributes.ObjectName = &VhdmpPath;
	ObjectAttributes.Attributes = OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE;

	status = IoCreateFile(&FileHandle, GENERIC_READ | SYNCHRONIZE, &ObjectAttributes, &StatusBlock, 0,
		FILE_READ_ATTRIBUTES, FILE_SHARE_READ, FILE_OPEN, FILE_NON_DIRECTORY_FILE, &ea, sizeof(ea),
		CreateFileTypeNone, NULL, IO_FORCE_ACCESS_CHECK | IO_NO_PARAMETER_CHECKING);
	if (!NT_SUCCESS(status))
	{
        LOG_FUNCTION(LL_FATAL, LOG_CTG_GENERAL, "IoCreateFile %S failed with error 0x%0x\n", VhdmpPath.Buffer, status);
		goto cleanup_failure;
	}

#if WINVEREX >= 0x10000000
	if (OpenFlags & 0x40)
	{
		for (;;)
		{
			BOOLEAN IsDifferencing = FALSE;
			status = GetIsDifferencing(FileHandle, &IsDifferencing);
			if (!NT_SUCCESS(status))
			{
                LOG_FUNCTION(LL_FATAL, LOG_CTG_GENERAL, "GetIsDifferencing failed with error 0x%0x\n", status);
				goto cleanup_failure;
			}
			if (!IsDifferencing)
				break;
			status = ZwClose(FileHandle);
			if (!NT_SUCCESS(status))
			{
                LOG_FUNCTION(LL_FATAL, LOG_CTG_GENERAL, "ZwClose failed with error 0x%0x\n", status);
				goto cleanup_failure;
			}

			status = IoCreateFile(&FileHandle, GENERIC_READ | SYNCHRONIZE, &ObjectAttributes, &StatusBlock, 0,
				FILE_READ_ATTRIBUTES, FILE_SHARE_READ, FILE_OPEN, FILE_NON_DIRECTORY_FILE, &ea, sizeof(ea),
				CreateFileTypeNone, NULL, IO_FORCE_ACCESS_CHECK | IO_NO_PARAMETER_CHECKING);
			if (!NT_SUCCESS(status))
			{
                LOG_FUNCTION(LL_FATAL, LOG_CTG_GENERAL, "IoCreateFile %S failed with error 0x%0x\n", VhdmpPath.Buffer, status);
				goto cleanup_failure;
			}
		}
	}
#endif

	status = ObReferenceObjectByHandle(FileHandle, 0, *IoFileObjectType, KernelMode, (PVOID*)&pFileObject, NULL);
	if (!NT_SUCCESS(status))
	{
        LOG_FUNCTION(LL_FATAL, LOG_CTG_GENERAL, "ObReferenceObjectByHandle failed with error 0x%0X\n", status);
		goto cleanup_failure;
	}

	*pFileHandle = FileHandle;
	*ppFileObject = pFileObject;

	goto cleanup;

cleanup_failure:
	if (FileHandle) ZwClose(FileHandle);
	if (pFileObject) ObDereferenceObject(pFileObject);
cleanup:
	if (VhdmpPath.Buffer)	ExFreePool(VhdmpPath.Buffer);

	return status;
}
