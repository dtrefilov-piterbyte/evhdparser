#include "stdafx.h"
#include "driver.h"
#include "Ioctl.h"
#include "Vstor.h"
#include <initguid.h>
#include "parser.h"
#include "Control.h"
#include "Log.h"
#include "Dispatch.h"

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

/** Driver unload routine */
void EVhdDriverUnload(PDRIVER_OBJECT pDriverObject)
{
	UNREFERENCED_PARAMETER(pDriverObject);
    DPT_Cleanup();
	CipherCleanup();
    Log_Cleanup();
}

/** Default major function dispatcher */
static NTSTATUS DispatchPassThrough(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);

/** Create major function dispatcher */
static NTSTATUS DispatchCreate(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);

/** Close major function dispatcher */
static NTSTATUS DispatchClose(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);

/** Device control major function dispatcher */
static NTSTATUS DispatchControl(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);


NTSTATUS DriverEntry(PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pRegistryPath)
{
	NTSTATUS status = STATUS_SUCCESS;
	VSTOR_PARSER_INFO ParserInfo = { 0 };
	RTL_OSVERSIONINFOW VersionInfo = { 0 };
    PDEVICE_OBJECT pDeviceObject = NULL;

	VersionInfo.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOW);

	UNREFERENCED_PARAMETER(pRegistryPath);
	status = RtlGetVersion(&VersionInfo);
	if (!NT_SUCCESS(status))
	{
		DbgPrint("Failed to get windows version information: %X\n", status);
		return status;
	}

	// This version of driver only supports Windows Server 2012 R2
	if (VersionInfo.dwMajorVersion != 6 || VersionInfo.dwMinorVersion != 3)
	{
		DbgPrint("Running on an unsupported platform: %d.%d.%d\n", VersionInfo.dwMajorVersion,
			VersionInfo.dwMinorVersion, VersionInfo.dwBuildNumber);
		return status = STATUS_NOT_SUPPORTED;
	}

    pDriverObject->DriverUnload = EVhdDriverUnload;

	status = CipherInit();
	if (!NT_SUCCESS(status))
	{
		DbgPrint("Failed to initialize cipher: %X\n", status);
		return status;
	}

    status = DPT_Initialize(pDriverObject, pRegistryPath, &pDeviceObject);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("Failed to initialize dispatch subsystem: %X\n", status);
        return status;
    }

	ParserInfo.dwSize						= sizeof(VSTOR_PARSER_INFO);
	ParserInfo.dwVersion					= 1;
	ParserInfo.ParserId						= GUID_EVHD_PARSER_ID;
	ParserInfo.pDriverObject				= pDriverObject;
	ParserInfo.pfnOpenDisk					= EVhd_OpenDisk;
	ParserInfo.pfnCloseDisk					= EVhd_CloseDisk;
	ParserInfo.pfnMountDisk					= EVhd_MountDisk;
	ParserInfo.pfnDismountDisk				= EVhd_DismountDisk;
	ParserInfo.pfnQueryMountStatusDisk		= EVhd_QueryMountStatusDisk;
	ParserInfo.pfnExecuteScsiRequestDisk	= EVhd_ExecuteScsiRequestDisk;
	ParserInfo.pfnQueryInformationDisk		= EVhd_QueryInformationDisk;
	ParserInfo.pfnQuerySaveVersionDisk		= EVhd_QuerySaveVersionDisk;
	ParserInfo.pfnSaveDisk					= EVhd_SaveDisk;
	ParserInfo.pfnRestoreDisk				= EVhd_RestoreDisk;
	ParserInfo.pfnSetBehaviourDisk			= EVhd_SetBehaviourDisk;
	ParserInfo.pfnSetQosConfigurationDisk	= EVhd_SetQosConfigurationDisk;
	ParserInfo.pfnGetQosInformationDisk		= EVhd_GetQosInformationDisk;


    status = Log_Initialize(pDeviceObject, pRegistryPath);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("Log_Initialize failed with error: %X\n", status);
        return status;
    }

	status = VstorRegisterParser(&ParserInfo);
	if (!NT_SUCCESS(status))
	{
		LOG_FUNCTION(LL_FATAL, LOG_CTG_GENERAL, "VstorRegisterParser failed with error: %X\n", status);
		return status;
	}

	return status;
}
