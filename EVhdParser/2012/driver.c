#include "driver.h"
#include <initguid.h>
#include "Vstor.h"
#include "parser.h"
#include "utils.h"
#include "Ioctl.h"
#include "Vdrvroot.h"

static DEVICE_OBJECT WPP_MAIN_CB = { 0 };
static PDEVICE_OBJECT WPP_GLOBAL_Control = NULL;

static HANDLE g_shimFileHandle = NULL;

static void WppInitKm()
{
	if (WPP_GLOBAL_Control != &WPP_MAIN_CB)
	{
		WPP_GLOBAL_Control = &WPP_MAIN_CB;
		//IoWMIRegistrationControl(&WPP_MAIN_CB, WMIREG_ACTION_REGISTER);
	}
}

static void WppCleanupKm()
{
	if (WPP_GLOBAL_Control == &WPP_MAIN_CB)
	{
		//IoWMIRegistrationControl(&WPP_MAIN_CB, WMIREG_ACTION_DEREGISTER);
		WPP_GLOBAL_Control = NULL;
	}
}

/** Driver unload routine */
void EVhdDriverUnload(PDRIVER_OBJECT pDriverObject)
{
	UNREFERENCED_PARAMETER(pDriverObject);
	if (g_shimFileHandle)
	{
		ZwClose(g_shimFileHandle);
		g_shimFileHandle = NULL;
	}

	WppCleanupKm();
}

/** Default major function dispatcher */
static NTSTATUS DispatchPassThrough(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);

/** Create major function dispatcher */
static NTSTATUS DispatchCreate(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);

/** Close major function dispatcher */
static NTSTATUS DispatchClose(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);

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
		DEBUG("FindShimDevice failed with error 0x%08X", status);
		goto cleanup_failure;
	}
		  
	ObjectAttributes.Length = sizeof(OBJECT_ATTRIBUTES);
	ObjectAttributes.ObjectName = &szShimName;
	ObjectAttributes.Attributes = OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE;

	status = ZwCreateFile(&g_shimFileHandle, GENERIC_READ | SYNCHRONIZE, &ObjectAttributes, &StatusBlock, 0,
		FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, FILE_NON_DIRECTORY_FILE, NULL, 0);
	if (!NT_SUCCESS(status))
	{
		DEBUG("ZwCreateFile %S failed with error 0x%08x\n", szShimName.Buffer, status);
		goto cleanup_failure;
	}

	status = ObReferenceObjectByHandle(g_shimFileHandle, 0, *IoFileObjectType, KernelMode, (PVOID*)&pFileObject, NULL);
	if (!NT_SUCCESS(status))
	{
		DEBUG("ObReferenceObjectByHandle failed with error 0x%08X\n", status);
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
	ULONG ulIndex;
	NTSTATUS status = STATUS_SUCCESS;
	VstorParserInfo ParserInfo = { 0 };

	UNREFERENCED_PARAMETER(pRegistryPath);

	WPP_MAIN_CB.Type = 0;
	WPP_MAIN_CB.NextDevice = NULL;
	WPP_MAIN_CB.CurrentIrp = NULL;
	WPP_MAIN_CB.DriverObject = pDriverObject;
	WPP_MAIN_CB.ReferenceCount = 1;

	WppInitKm();

	for (ulIndex = 0; ulIndex < IRP_MJ_MAXIMUM_FUNCTION; ++ulIndex)
	{
		pDriverObject->MajorFunction[ulIndex] = DispatchPassThrough;
	}

	pDriverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreate;
	pDriverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchClose;


	pDriverObject->DriverUnload = EVhdDriverUnload;

	ParserInfo.qwVersion = 0;
	ParserInfo.qwUnk1 = 0;
	ParserInfo.qwUnk2 = 1;
	ParserInfo.qwUnk3 = 1;
	ParserInfo.pDriverObject = pDriverObject;
	ParserInfo.pfnParserInit = EVhdInit;
	ParserInfo.pfnParserCleanup = EVhdCleanup;
	ParserInfo.pfnParserGetGeometry = EVhdGetGeometry;
	ParserInfo.pfnParserGetCapabilities = EVhdGetCapabilities;
	ParserInfo.pfnParserMount = EVhdMount;
	ParserInfo.pfnParserExecuteSrb = EVhdExecuteSrb;
	ParserInfo.pfnParserBeginSave = EVhdBeginSave;
	ParserInfo.pfnParserSaveData = EVhdSaveData;
	ParserInfo.pfnParserBeginRestore = EVhdBeginRestore;
	ParserInfo.pfnParserRestoreData = EVhdRestoreData;
	ParserInfo.pfnParserSetCacheState = EVhdSetCacheState;
	status = EVhdDriverLoad(&ParserInfo.dwBalancerId);
	if (!NT_SUCCESS(status))
	{
		DEBUG("EVhdDriverLoad failed with error: 0x%08X\n", status);
		return status;
	}

	status = RegisterParser(&ParserInfo);
	if (!NT_SUCCESS(status))
	{
		DEBUG("RegisterParser failed with error: 0x%08X\n", status);
		return status;
	}

	return status;
}

static NTSTATUS DispatchPassThrough(PDEVICE_OBJECT pDeviceObject, PIRP pIrp)
{
	UNREFERENCED_PARAMETER(pDeviceObject);

	//PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pDeviceObject->DeviceExtension;
	pIrp->IoStatus.Status = STATUS_SUCCESS;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

static NTSTATUS DispatchCreate(PDEVICE_OBJECT pDeviceObject, PIRP pIrp)
{
	UNREFERENCED_PARAMETER(pDeviceObject);

	//PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pDeviceObject->DeviceExtension;
	DbgPrint("DispatchCreate called\n");

	pIrp->IoStatus.Status = STATUS_SUCCESS;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

static NTSTATUS DispatchClose(PDEVICE_OBJECT pDeviceObject, PIRP pIrp)
{
	UNREFERENCED_PARAMETER(pDeviceObject);

	//PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pDeviceObject->DeviceExtension;
	DbgPrint("DispatchClose called\n");

	pIrp->IoStatus.Status = STATUS_SUCCESS;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}
