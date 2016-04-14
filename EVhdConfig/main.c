#include <stdio.h>
#include <Windows.h>
#include <tchar.h>
#include <initguid.h>
#include "../EVhdParser/Control.h"
#include <virtdisk.h>

UCHAR rgbTest128Key[16] = {
	162, 101, 19, 209, 154, 134, 198, 11, 40, 242, 103, 43, 26, 9, 159, 59
};

DWORD SyncrhonousDeviceIoControl(HANDLE hDevice, DWORD dwControlCode, LPVOID lpInBuffer,
	DWORD nInBufferSize, LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned)
{
	DWORD dwResult = ERROR_SUCCESS;
	HANDLE hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!hEvent)
		return GetLastError();
	OVERLAPPED Overlapped = { 0 };
	Overlapped.hEvent = hEvent;
	
	BOOL bOk = DeviceIoControl(hDevice, dwControlCode, lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize,
		lpBytesReturned, &Overlapped);
	if (!bOk)
	{
		dwResult = GetLastError();
		if (ERROR_IO_PENDING == dwResult)
		{
			if (!GetOverlappedResult(hDevice, &Overlapped, lpBytesReturned, TRUE))
				dwResult = GetLastError();
			else
				dwResult = ERROR_SUCCESS;
		}
	}

	CloseHandle(hEvent);

	return dwResult;
}

static void PrintError(DWORD dwError, LPCSTR lpszMessage)
{
	LPSTR pBuffer = NULL;
	FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, dwError, LANG_SYSTEM_DEFAULT, (LPSTR)&pBuffer, 0, NULL);
	if (NULL != pBuffer)
	{
		printf("%s: %s\n", lpszMessage, pBuffer);
		LocalFree((HLOCAL)pBuffer);
	}
}

static DWORD GetCanonicalPath(const TCHAR *lpszOriginalFileName, TCHAR **lppszFinalName)
{
	DWORD dwError = ERROR_SUCCESS;
	HANDLE hFile = NULL;
	TCHAR *lpBuffer = NULL;
	DWORD dwBufferSize = 0;

	if (!lpszOriginalFileName || !lppszFinalName)
	{
		return dwError = ERROR_INVALID_PARAMETER;
	}
	
	hFile = CreateFile(lpszOriginalFileName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (!hFile)
	{
		dwError = GetLastError();
		goto out;
	}

	lpBuffer = VirtualAlloc(NULL, dwBufferSize = MAX_PATH, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!lpBuffer)
	{
		dwError = GetLastError();
		goto out;
	}

	if (0 == GetFinalPathNameByHandle(hFile, lpBuffer, dwBufferSize, FILE_NAME_NORMALIZED))
	{
		dwError = GetLastError();
		VirtualFree(lpBuffer, 0, MEM_RELEASE);
		goto out;
	}

	*lppszFinalName = lpBuffer;

out:
	if (hFile)
		CloseHandle(hFile);

	return dwError;
}

static DWORD GetVhdId(LPTSTR lpszVhdPath, GUID *lpDiskId)
{
	DWORD dwError = ERROR_SUCCESS;
	VIRTUAL_STORAGE_TYPE VirtualStorageType = {
		.DeviceId = VIRTUAL_STORAGE_TYPE_DEVICE_UNKNOWN,
		.VendorId = VIRTUAL_STORAGE_TYPE_VENDOR_UNKNOWN
	};
	GET_VIRTUAL_DISK_INFO DiskInfo = {
		.Version = GET_VIRTUAL_DISK_INFO_VIRTUAL_DISK_ID
	};
	ULONG diskInfoSize = sizeof(DiskInfo);
	HANDLE hVhd = NULL;

	// Also loads vhdparser for the specified vendor
	dwError = OpenVirtualDisk(&VirtualStorageType, lpszVhdPath, VIRTUAL_DISK_ACCESS_GET_INFO, OPEN_VIRTUAL_DISK_FLAG_NONE,
		NULL, &hVhd);

	if (ERROR_SUCCESS != dwError)
	{
		return dwError;
	}

	dwError = GetVirtualDiskInformation(hVhd, &diskInfoSize, &DiskInfo, NULL);
	if (ERROR_SUCCESS == dwError)
	{
		*lpDiskId = DiskInfo.VirtualDiskId;
	}
	
	if (hVhd)
		CloseHandle(hVhd);

	return dwError;
}

void PrintUsage()
{
	printf("Usage: EVhdConfig <path to vhd>\n");
}

int _tmain(int argc, _TCHAR* argv[])
{
	DWORD dwError = ERROR_SUCCESS;
	GUID vhdId = GUID_NULL;

	if (argc != 2)
	{
		PrintUsage();
		return 1;
	}

	dwError = GetVhdId(argv[1], &vhdId);
	if (ERROR_SUCCESS != dwError)
	{
		PrintError(dwError, "Could not get VHD identifier");
		return 1;
	}

	HANDLE hDevice = CreateFile(L"\\\\.\\EVhdParser", GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING,
		FILE_FLAG_OVERLAPPED, NULL);
	if (hDevice != INVALID_HANDLE_VALUE)
	{
		EVHD_SET_CIPHER_CONFIG_REQUEST request = {
			.DiskId = vhdId,
			.Algorithm = ECipherAlgo_AES128
		};
		request.Opts.Aes128.OperationMode = OperationMode_ECB;
        memcpy(request.Opts.Aes128.Key, rgbTest128Key, sizeof(request.Opts.Aes128.Key));

        //EVHD_SET_CIPHER_CONFIG_REQUEST request = {
        //    .DiskId = vhdId,
        //    .Algorithm = ECipherAlgo_Xor
        //};
        //request.Opts.Xor.XorMixingValue = 0xCCCCCCCC;

		if (ERROR_SUCCESS != (dwError = SyncrhonousDeviceIoControl(hDevice,
			IOCTL_VIRTUAL_DISK_SET_CIPHER, &request, sizeof(request), NULL, 0, NULL)))
		{
			PrintError(dwError, "Failed to set device cipher");
		}

		CloseHandle(hDevice);
	}
	else
	{
		dwError = GetLastError();
		PrintError(dwError, "Failed to open shim driver device");
	}

	return 0;
}
