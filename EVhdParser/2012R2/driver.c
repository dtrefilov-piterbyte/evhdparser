#include "driver.h"
#include "Ioctl.h"
#include "Vstor.h"
#include <initguid.h>
#include "parser.h"

#if 0
// {860ECCBC-6E7D-4A17-B181-81D64AF02170}
DEFINE_GUID(GUID_EVHD_PARSER_ID,
	0x860eccbc, 0x6e7d, 0x4a17, 0xb1, 0x81, 0x81, 0xd6, 0x4a, 0xf0, 0x21, 0x70);
#else
// pretend to replace original vhdparser
// {f916c826-f0f5-4cd9-be68-4fd638cf9a53}
DEFINE_GUID(GUID_EVHD_PARSER_ID,
	0xf916c826, 0xf0f5, 0x4cd9, 0xbe, 0x68, 0x4f, 0xd6, 0x38, 0xcf, 0x9a, 0x53);
#endif

static DEVICE_OBJECT WPP_MAIN_CB = { 0 };
static PDEVICE_OBJECT WPP_GLOBAL_Control = NULL;

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
	WppCleanupKm();
}

/** Default major function dispatcher */
static NTSTATUS DispatchPassThrough(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);

/** Create major function dispatcher */
static NTSTATUS DispatchCreate(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);

/** Close major function dispatcher */
static NTSTATUS DispatchClose(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);


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

	ParserInfo.dwSize						= sizeof(VstorParserInfo);
	ParserInfo.dwVersion					= 1;
	ParserInfo.ParserId						= GUID_EVHD_PARSER_ID;
	ParserInfo.pDriverObject				= pDriverObject;
	ParserInfo.pfnOpenDisk					= EVhdOpenDisk;
	ParserInfo.pfnCloseDisk					= EVhdCloseDisk;
	ParserInfo.pfnMountDisk					= EVhdMountDisk;
	ParserInfo.pfnDismountDisk				= EVhdDismountDisk;
	ParserInfo.pfnQueryMountStatusDisk		= EVhdQueryMountStatusDisk;
	ParserInfo.pfnExecuteScsiRequestDisk	= EVhdExecuteScsiRequestDisk;
	ParserInfo.pfnQueryInformationDisk		= EVhdQueryInformationDisk;
	ParserInfo.pfnQuerySaveVersionDisk		= EVhdQuerySaveVersionDisk;
	ParserInfo.pfnSaveDisk					= EVhdSaveDisk;
	ParserInfo.pfnRestoreDisk				= EVhdRestoreDisk;
	ParserInfo.pfnSetBehaviourDisk			= EVhdSetBehaviourDisk;
	ParserInfo.pfnSetQosConfigurationDisk	= EVhdSetQosConfigurationDisk;
	ParserInfo.pfnGetQosInformationDisk		= EVhdGetQosInformationDisk;

	status = VstorRegisterParser(&ParserInfo);
	if (!NT_SUCCESS(status))
	{
		DbgPrint("VstorRegisterParser failed with error: %d\n", status);
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
