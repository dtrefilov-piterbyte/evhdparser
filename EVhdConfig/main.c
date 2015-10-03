#include <stdio.h>
#include <Windows.h>
#include <initguid.h>
#include "../EVhdParser/Control.h"

// TODO: hardcoded test disk identifier
DEFINE_GUID(GUID_TEST_VHD_DISK_ID,
	0x1f3afa79, 0x7f5b, 0x4e8b, 0xbc, 0x99, 0xf0, 0x65, 0xc8, 0x1e, 0x96, 0xf6);

UCHAR rgbTest256Key[32] = {
	162, 101, 19, 209, 154, 134, 198, 11, 40, 242, 103, 43, 26, 9, 159, 59,
	207, 158, 169, 51, 159, 155, 93, 122, 252, 74, 104, 90, 192, 0, 60, 38
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

int main(int argc, char **argv)
{
	HANDLE hDevice = CreateFile(L"\\\\.\\EVhdParser", GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING,
		FILE_FLAG_OVERLAPPED, NULL);
	if (hDevice != INVALID_HANDLE_VALUE)
	{
		EVhdVirtualDiskCipherConfigRequest request = {
			.DiskId = GUID_TEST_VHD_DISK_ID,
			.Algorithm = ECipherAlgo_Gost89
		};
		request.Opts.Gost89.ChainingMode = ChainingMode_CFB;
		request.Opts.Gost89.SBlock = ESBlock_tc26_gost28147_param_Z;
		memcpy(request.Opts.Gost89.Key, rgbTest256Key, sizeof(request.Opts.Gost89.Key));

		if (ERROR_SUCCESS != (SyncrhonousDeviceIoControl(hDevice,
			IOCTL_VIRTUAL_DISK_SET_CIPHER, &request, sizeof(request), NULL, 0, NULL)))
		{
			printf("Failed to set device cipher\n");
		}

		CloseHandle(hDevice);
	}
	else
	{
		printf("Failed to open shim driver device\n");
	}

	return 0;
}
