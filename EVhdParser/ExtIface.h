#pragma once  
#include <ntifs.h>
#include <srb.h>
#include <scsi.h>
#include <ntddscsi.h>

typedef struct _EVHD_EXT_CAPABILITIES {
	SIZE_T StateSize;
} EVHD_EXT_CAPABILITIES, *PEVHD_EXT_CAPABILITIES;

typedef struct _EVHD_EXT_SCSI_PACKET {
	/* Raw SCSI request command buffer */
	PSCSI_REQUEST_BLOCK Srb;
	/* Pointer to the data buffer for READ and WRITER requests.
	* For write requests the buffer is immutable, changing it's contents leads to the undefined behaviour
	*/
	PMDL pMdl;
	/* Pointer to the any user-defined data corresponding to the given request */
	PVOID pUserData;
} EVHD_EXT_SCSI_PACKET, *PEVHD_EXT_SCSI_PACKET;

#define EVHD_MOUNT_FLAG_SHARED_ACCESS

NTSTATUS EVhdExtInitialize(_Out_ PEVHD_EXT_CAPABILITIES pCaps);

NTSTATUS EVhdExtFinalize();

/**

 EVhdExtCreate

 Routine Description:
	This function is called when storage VSP opens a virtual disk
 Arguments:
	DiskPath - Filesystem path to the virtual disk being opened (VHD, VHDX, ISO)
	ApplicationId - ID of the application requesting this disk (VmID in a case of disk being plugged to the virtual IDE/SCSI controller)
	DiskContext - Extension context specific to the given virtual disk. This context
	 is passed back to the EVhdExt function calls
*/
NTSTATUS EVhdExtCreate(_In_ PCUNICODE_STRING DiskPath, PGUID ApplicationId, _Outptr_opt_result_maybenull_ PVOID *DiskContext);

/**

 EVhdExtDelete

 Routine Description:
	This function is called to free all data associated with the
	given 
 Arguments:
	DiskContext - The extension context allocated in EVhdExtCreate
*/
NTSTATUS EVhdExtDelete(_In_ PVOID DiskContext);

/**

 EVhdExtMount

 Routine Description:
	This function is called when storage VSP starts the IO on specified virtual disk.
	It might not be called at all if disk is opened only to query disk metadata  
 Arguments:
	DiskContext - The extension context allocated in EVhdExtCreate
	MountFlags - combination of known flags
*/
NTSTATUS EVhdExtMount(_In_ PVOID ExtContext, _In_ INT MountFlags);

/**

 EVhdExtDismount

 Routine Description:
	This function is called when storage VSP unmounts disk to free all IO-releated resources
	and unblock access to the disk
*/
NTSTATUS EVhdExtDismount(_In_ PVOID ExtContext);

/**

 EVhdExtPause

 Routine Description:
	This function is called when storage VSP suspends and current stores disk state to the media
	(for example during migration)
*/
NTSTATUS EVhdExtPause(_In_ PVOID ExtContext, _In_ PVOID SaveBuffer, _Inout_ SIZE_T *SaveBufferSize);

/**

 EVhdExtResume

 Routine Description:
	This function is called to restore previosly saved state by EVhdExtPause
*/
NTSTATUS EVhdExtResume(_In_ PVOID ExtContext, PVOID RestoreBuffer, SIZE_T RestoreBufferSize);

/**
 EVhdExtStartScsiRequest

 Routine Description:
	This function is called to filter all SCSI commands
*/
NTSTATUS EVhdExtStartScsiRequest(_In_ PVOID ExtContext, _In_ PEVHD_EXT_SCSI_PACKET pExtPacket);

/**
 EVhdExtCompleteScsiRequest

 Routine Description:
	 	
*/
NTSTATUS EVhdExtCompleteScsiRequest(_In_ PVOID ExtContext, _In_ PEVHD_EXT_SCSI_PACKET pExtPacket, _In_ NTSTATUS Status);
