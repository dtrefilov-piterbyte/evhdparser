#include "stdafx.h"
#include "Log.h"
#include "driver.h"
#include "Vstor.h"
#include "parser.h"
#include "utils.h"
#include "Ioctl.h"
#include "Vdrvroot.h"
#include "Dispatch.h"
#include "Extension.h"

static HANDLE g_shimFileHandle = NULL;

/** Driver unload routine */
void EVhdDriverUnload(PDRIVER_OBJECT pDriverObject)
{
    UNREFERENCED_PARAMETER(pDriverObject);
	if (g_shimFileHandle)
	{
		ZwClose(g_shimFileHandle);
		g_shimFileHandle = NULL;
    }
    Log_Cleanup();
    DPT_Cleanup();
}

static NTSTATUS EVhdDriverLoad(ULONG32 *pResult)
{
	UNICODE_STRING szShimName = { 0 }, szRootPath = { 0 };
	NTSTATUS status = STATUS_SUCCESS;
	OBJECT_ATTRIBUTES ObjectAttributes = { 0 };
	IO_STATUS_BLOCK StatusBlock = { 0 };
	PFILE_OBJECT pFileObject = NULL;
	ULONG32 dwRequest = 0xC0;

	RtlInitUnicodeString(&szRootPath, L"");

	status = FindShimDevice(&szShimName, &szRootPath);
	if (!NT_SUCCESS(status))
	{
        LOG_FUNCTION(LL_FATAL, LOG_CTG_GENERAL, "FindShimDevice failed with error 0x%08X", status);
		goto cleanup_failure;
	}
		  
	ObjectAttributes.Length = sizeof(OBJECT_ATTRIBUTES);
	ObjectAttributes.ObjectName = &szShimName;
	ObjectAttributes.Attributes = OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE;

	status = ZwCreateFile(&g_shimFileHandle, GENERIC_READ | SYNCHRONIZE, &ObjectAttributes, &StatusBlock, 0,
		FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, FILE_NON_DIRECTORY_FILE, NULL, 0);
	if (!NT_SUCCESS(status))
	{
        LOG_FUNCTION(LL_FATAL, LOG_CTG_GENERAL, "ZwCreateFile %S failed with error 0x%08x\n", szShimName.Buffer, status);
		goto cleanup_failure;
	}

	status = ObReferenceObjectByHandle(g_shimFileHandle, 0, *IoFileObjectType, KernelMode, (PVOID*)&pFileObject, NULL);
	if (!NT_SUCCESS(status))
	{
        LOG_FUNCTION(LL_FATAL, LOG_CTG_GENERAL, "ObReferenceObjectByHandle failed with error 0x%08X\n", status);
		goto cleanup_failure;
	}

	status = SynchronouseCall(pFileObject, IOCTL_STORAGE_REGISTER_BALANCER, &dwRequest, sizeof(ULONG32), pResult, sizeof(ULONG32));
cleanup_failure:
	if (pFileObject) ObDereferenceObject(pFileObject);
	if (szShimName.Buffer) ExFreePool(szShimName.Buffer);

	return status;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pRegistryPath)
{
	NTSTATUS status = STATUS_SUCCESS;
	VstorParserInfo ParserInfo = { 0 };
    PDEVICE_OBJECT pDeviceObject = NULL;
    EVHD_EXT_CAPABILITIES Caps;

    pDriverObject->DriverUnload = EVhdDriverUnload;

    status = Ext_Initialize(&Caps);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("Failed to initialize extension: %X\n", status);
        return status;
    }

    status = DPT_Initialize(pDriverObject, pRegistryPath, &pDeviceObject);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("DPT_Initialize failed with error: 0x%08X\n", status);
        return status;
    }

    status = Log_Initialize(pDeviceObject, pRegistryPath);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("Log_Initialize failed with error: 0x%08X\n", status);
        return status;
    }

	ParserInfo.qwVersion = 0;
	ParserInfo.qwUnk1 = 0;
	ParserInfo.qwUnk2 = 1;
	ParserInfo.qwUnk3 = 1;
	ParserInfo.pDriverObject = pDriverObject;
	ParserInfo.pfnParserInit = EVhd_Init;
	ParserInfo.pfnParserCleanup = EVhd_Cleanup;
	ParserInfo.pfnParserGetGeometry = EVhd_GetGeometry;
	ParserInfo.pfnParserGetCapabilities = EVhd_GetCapabilities;
	ParserInfo.pfnParserMount = EVhd_Mount;
	ParserInfo.pfnParserExecuteSrb = EVhd_ExecuteSrb;
	ParserInfo.pfnParserBeginSave = EVhd_BeginSave;
	ParserInfo.pfnParserSaveData = EVhd_SaveData;
	ParserInfo.pfnParserBeginRestore = EVhd_BeginRestore;
	ParserInfo.pfnParserRestoreData = EVhd_RestoreData;
	ParserInfo.pfnParserSetCacheState = EVhd_SetCacheState;
	status = EVhdDriverLoad(&ParserInfo.dwBalancerId);
	if (!NT_SUCCESS(status))
	{
        DbgPrint("EVhdDriverLoad failed with error: 0x%08X\n", status);
		return status;
	}

	status = RegisterParser(&ParserInfo);
	if (!NT_SUCCESS(status))
	{
        DbgPrint("RegisterParser failed with error: 0x%08X\n", status);
		return status;
	}

	return status;
}
