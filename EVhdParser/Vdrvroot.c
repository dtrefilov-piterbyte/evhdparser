#include "Vdrvroot.h" 
#include <initguid.h>
#include "Guids.h"
#include "Ioctl.h"
#include "utils.h"

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
		DEBUG("Failed to find installed Hyper-V Virtual Drive Enumerator\n");
		goto cleanup_failure;
	}

	RtlInitUnicodeString(&DriveEnumeratorSymbolicLink, SymbolicLinkList);

	ObjectAttributes.Length = sizeof(OBJECT_ATTRIBUTES);
	ObjectAttributes.ObjectName = &DriveEnumeratorSymbolicLink;
	ObjectAttributes.Attributes = OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE;

	status = IoGetDeviceObjectPointer(&DriveEnumeratorSymbolicLink, GENERIC_ALL, &pFileObject, &pDeviceObject);
	if (!NT_SUCCESS(status))
	{
		DEBUG("IoGetDeviceObjectPointer %S failed with error = 0x%0x\n", DriveEnumeratorSymbolicLink.Buffer, status);
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
		DEBUG("SynchronouseCall failed with error 0x%0x\n", status);
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

NTSTATUS OpenVhdmpDevice(HANDLE *pFileHandle, ULONG32 OpenFlags, PFILE_OBJECT *ppFileObject, PCUNICODE_STRING diskPath, const ResiliencyInfo *pResiliency)
{
	NTSTATUS status = STATUS_SUCCESS;
	HANDLE FileHandle = NULL;
	OBJECT_ATTRIBUTES ObjectAttributes = { 0 };
	IO_STATUS_BLOCK StatusBlock = { 0 };
	UNICODE_STRING VhdmpPath = { 0 };
	PFILE_OBJECT pFileObject = NULL;
	OpenDiskEa ea = { 0 };

	status = FindShimDevice(&VhdmpPath, diskPath);
	if (!NT_SUCCESS(status))
	{
		DEBUG("FindShimDevice failed\n");
		goto cleanup_failure;
	}

	// hardcoded
	ea.dwVersion = 0;
	ea.typeFlags = 0x80;
	ea.typeLength = 7;
	ea.typeFlags2 = 0x48;
	strncpy(ea.szType, "VIRTDSK", sizeof(ea.szType));
	ea.DevInterfaceClassGuid = GUID_DEVINTERFACE_SURFACE_VIRTUAL_DRIVE;
	ea.DiskFormat = EDiskFormat_Vhd;
	ea.ParserProviderId = VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT;
	ea.StorageType = 'H';
#if NTDDI_VERSION == NTDDI_WINBLUE
	ea.OpenFlags = OpenFlags & 1 ? 0x80000000 : (OpenFlags & 2 ? 1 : 0);
#elif NTDDI_VERSION == NTDDI_WIN8
	ea.OpenFlags = OpenFlags ? 0 : 1;
#endif
	ea.VirtualDiskAccessMask = 0;
	ea.RWDepth = 0;
	ea.OpenRequestVersion = 2;
#if NTDDI_VERSION == NTDDI_WINBLUE
	ea.GetInfoOnly = OpenFlags & 1;
	ea.ReadOnly = 0;
#endif

	if (pResiliency)
	{
		// Dunno, hardcoded	   
		ea.dwVersion = 'X';
		memmove(&ea.VmInfo, pResiliency, sizeof(ResiliencyInfo));
	}

	// 2012 R2 only?
	// A bit of old good MS code
	if (VhdmpPath.Length > 6 &&
		'i' == (VhdmpPath.Buffer[VhdmpPath.Length / sizeof(WCHAR) - 3] | 0x20) &&	// lowercase
		's' == (VhdmpPath.Buffer[VhdmpPath.Length / sizeof(WCHAR) - 2] | 0x20) &&
		'o' == (VhdmpPath.Buffer[VhdmpPath.Length / sizeof(WCHAR) - 1] | 0x20))
	{
		VhdmpPath.Length -= 6;
		RtlAppendUnicodeToString(&VhdmpPath, L"EMPTY");

		ea.DiskFormat = EDiskFormat_Iso;
		ea.ReadOnly = TRUE;
	}

	if (VhdmpPath.Length > 8 &&
		'v' == (VhdmpPath.Buffer[VhdmpPath.Length / sizeof(WCHAR) - 4] | 0x20) &&
		'h' == (VhdmpPath.Buffer[VhdmpPath.Length / sizeof(WCHAR) - 3] | 0x20) &&
		'd' == (VhdmpPath.Buffer[VhdmpPath.Length / sizeof(WCHAR) - 2] | 0x20) &&
		'x' == (VhdmpPath.Buffer[VhdmpPath.Length / sizeof(WCHAR) - 1] | 0x20))
	{
		ea.DiskFormat = EDiskFormat_Vhdx;
	}

	ObjectAttributes.Length = sizeof(OBJECT_ATTRIBUTES);
	ObjectAttributes.ObjectName = &VhdmpPath;
	ObjectAttributes.Attributes = OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE;

	status = ZwCreateFile(&FileHandle, GENERIC_READ | SYNCHRONIZE, &ObjectAttributes, &StatusBlock, 0, FILE_READ_ATTRIBUTES, 0, 0, FILE_NON_DIRECTORY_FILE, &ea, sizeof(ea));
	if (!NT_SUCCESS(status))
	{
		DEBUG("ZwCreateFile %S failed with error 0x%0x\n", VhdmpPath.Buffer, status);
		goto cleanup_failure;
	}

	status = ObReferenceObjectByHandle(FileHandle, 0, *IoFileObjectType, KernelMode, (PVOID*)&pFileObject, NULL);
	if (!NT_SUCCESS(status))
	{
		DEBUG("ObReferenceObjectByHandle failed with error 0x%0X\n", status);
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
