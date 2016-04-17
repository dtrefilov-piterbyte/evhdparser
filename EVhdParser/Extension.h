#pragma once  
#include <srb.h>

typedef struct _EVHD_EXT_CAPABILITIES {
	SIZE_T StateSize;
} EVHD_EXT_CAPABILITIES, *PEVHD_EXT_CAPABILITIES;

typedef struct _EVHD_EXT_SCSI_PACKET {
	/* Raw SCSI request command buffer pointer */
	PSCSI_REQUEST_BLOCK Srb;
	/* Pointer to the data buffer for READ and WRITER requests.
	* For write requests the buffer is immutable, changing it's contents leads to the undefined behaviour
	*/
	PMDL pMdl;
    /** Pointer to the sense buffer
    */
    PVOID pSenseBuffer;
} EVHD_EXT_SCSI_PACKET, *PEVHD_EXT_SCSI_PACKET;

#define EVHD_MOUNT_FLAG_SHARED_ACCESS

NTSTATUS Ext_Initialize(_Out_ PEVHD_EXT_CAPABILITIES pCaps);

NTSTATUS Ext_Cleanup();

/**

 Ext_Create

 Routine Description:
	This function is called when storage VSP opens a virtual disk
 Arguments:
	DiskPath - Filesystem path to the virtual disk being opened (VHD, VHDX, ISO)
    DiskId - Page83 storage identifier
	ApplicationId - ID of the application requesting this disk (VmID in a case of disk being plugged to the virtual IDE/SCSI controller)
	DiskContext - Extension context specific to the given virtual disk. This context
	 is passed back to the EVhdExt function calls
*/
NTSTATUS Ext_Create(_In_ PCUNICODE_STRING DiskPath,
    _In_ PGUID ApplicationId,
    _In_ EDiskFormat DiskFormat,
    _In_ PGUID DiskId,
    _Outptr_opt_result_maybenull_ PVOID *DiskContext);

/**

 Ext_Delete

 Routine Description:
	This function is called to free all data associated with the
	given 
 Arguments:
	DiskContext - The extension context allocated in EVhdExtCreate
*/
NTSTATUS Ext_Delete(_In_ PVOID DiskContext);

/**

 Ext_Mount

 Routine Description:
	This function is called when storage VSP starts the IO on specified virtual disk.
	It might not be called at all if disk is opened only to query disk metadata  
 Arguments:
	DiskContext - The extension context allocated in EVhdExtCreate
*/
NTSTATUS Ext_Mount(_In_ PVOID ExtContext);

/**

 Ext_Dismount

 Routine Description:
	This function is called when storage VSP unmounts disk to free all IO-releated resources
	and unblock access to the disk
*/
NTSTATUS Ext_Dismount(_In_ PVOID ExtContext);

/**

 Ext_Pause

 Routine Description:
	This function is called when storage VSP suspends and current stores disk state to the media
	(for example during migration)
*/
NTSTATUS Ext_Pause(_In_ PVOID ExtContext, _In_ PVOID SaveBuffer, _Inout_ SIZE_T *SaveBufferSize);

/**

 Ext_Restore

 Routine Description:
	This function is called to restore previosly saved state by EVhdExtPause
*/
NTSTATUS Ext_Restore(_In_ PVOID ExtContext, PVOID RestoreBuffer, SIZE_T RestoreBufferSize);

/**
 Ext_StartScsiRequest

 Routine Description:
	This function is called to filter all SCSI commands
*/
NTSTATUS Ext_StartScsiRequest(_In_ PVOID ExtContext, _In_ PEVHD_EXT_SCSI_PACKET pExtPacket);

/**
 Ext_CompleteScsiRequest

 Routine Description:
	 	
*/
NTSTATUS Ext_CompleteScsiRequest(_In_ PVOID ExtContext, _In_ PEVHD_EXT_SCSI_PACKET pExtPacket, _In_ NTSTATUS Status);
