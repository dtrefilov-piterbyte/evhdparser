#include "stdafx.h"
#include "Dispatch.h"
#include "Log.h"
#include "cipher.h"

#define DPTLOG(level, format, ...) LOG_FUNCTION(level, LOG_CTG_DISPATCH, format, __VA_ARGS__)

#define DEVICE_NAME L"\\Device\\EVhdParser"
#define DOSDEVICE_NAME L"\\DosDevices\\EVhdParser"

const ULONG32 DptAllocationTag = 'Dpt ';

typedef struct {
	LIST_ENTRY Link;

	PARSER_MESSAGE Message;
} PARSER_MESSAGE_ENTRY, *PPARSER_MESSAGE_ENTRY;

typedef struct _AUDIT_SUBSCRIPTION_CONTEXT {
    LIST_ENTRY Link;

	PFILE_OBJECT pFileObject;
	LIST_ENTRY PendedReads;
	ULONG PendedReadsCount;
	LIST_ENTRY PendedMessages;
	ULONG PendedMessagesCount;

	KSPIN_LOCK Lock;
} AUDIT_SUBSCRIPTION_CONTEXT;

// Forward declarations

static VOID DPT_CancelPendingReads(AUDIT_SUBSCRIPTION_CONTEXT *pContext);
static NTSTATUS DPT_PassThrough(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);
static NTSTATUS DPT_Open(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);
static NTSTATUS DPT_ReadCancel(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);
static NTSTATUS DPT_Read(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);
static NTSTATUS DPT_Write(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);
static NTSTATUS DPT_Close(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);
static NTSTATUS DPT_Control(PDEVICE_OBJECT pDeviceObject, PIRP pIrp);
static BOOLEAN DPT_SendMessage(AUDIT_SUBSCRIPTION_CONTEXT *pContext, PARSER_MESSAGE *pMessage);

// Dispatch globals
PDEVICE_OBJECT DptDeviceObject = NULL;
static LIST_ENTRY DptSubscriptions = { &DptSubscriptions, &DptSubscriptions };
KSPIN_LOCK DptLock;	// synchronizes access to DptSubscriptions

NTSTATUS DPT_Initialize(_In_ PDRIVER_OBJECT pDriverObject, _In_ PCUNICODE_STRING pRegistryPath, _Out_ PDEVICE_OBJECT *ppDeviceObject)
{
    UNREFERENCED_PARAMETER(pRegistryPath);
	NTSTATUS Status = STATUS_SUCCESS;
	ULONG ulIndex = 0;
    UNICODE_STRING DeviceName;
    UNICODE_STRING DosDeviceName;
	PDRIVER_DISPATCH *majorFunctions = pDriverObject->MajorFunction;

	TRACE_FUNCTION_IN();

    KeInitializeSpinLock(&DptLock);

    RtlInitUnicodeString(&DeviceName, DEVICE_NAME);
    RtlInitUnicodeString(&DosDeviceName, DOSDEVICE_NAME);

    Status = IoCreateDevice(pDriverObject, 0, &DeviceName, FILE_DEVICE_DISK_FILE_SYSTEM,
        FILE_DEVICE_SECURE_OPEN, FALSE, &DptDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("Failed to create device: %X\n", Status);
        goto Cleanup;
    }

    Status = IoCreateSymbolicLink(&DosDeviceName, &DeviceName);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("Failed to create dos device link: %X\n", Status);
        goto Cleanup;
    }

	for (ulIndex = 0; ulIndex < IRP_MJ_MAXIMUM_FUNCTION; ++ulIndex)
	{
		switch (ulIndex)
		{
		case IRP_MJ_DEVICE_CONTROL:
			majorFunctions[ulIndex] = DPT_Control;
			break;
		case IRP_MJ_CREATE:
			majorFunctions[ulIndex] = DPT_Open;
			break;
		case IRP_MJ_CLOSE:
			majorFunctions[ulIndex] = DPT_Close;
			break;
		case IRP_MJ_READ:
			majorFunctions[ulIndex] = DPT_Read;
			break;
		case IRP_MJ_WRITE:
			majorFunctions[ulIndex] = DPT_Write;
			break;
		default:
			majorFunctions[ulIndex] = DPT_PassThrough;
			break;
		}
	}

	DptDeviceObject->Flags |= DO_DIRECT_IO;

	*ppDeviceObject = DptDeviceObject;

Cleanup:
	if (!NT_SUCCESS(Status))
	{
		DPT_Cleanup();
	}

	TRACE_FUNCTION_OUT_STATUS(Status);

	return Status;
}

VOID DPT_Cleanup()
{
	TRACE_FUNCTION_IN();

	while (!IsListEmpty(&DptSubscriptions))
    {
        PLIST_ENTRY pEntry = DptSubscriptions.Flink;

		AUDIT_SUBSCRIPTION_CONTEXT *pContext = CONTAINING_RECORD(pEntry, AUDIT_SUBSCRIPTION_CONTEXT, Link);
		DPT_CancelPendingReads(pContext);
        ExFreePoolWithTag(pContext, DptAllocationTag);
        RemoveEntryList(pEntry);
	}

    UNICODE_STRING DosDeviceName;
    RtlInitUnicodeString(&DosDeviceName, DOSDEVICE_NAME);
    IoDeleteSymbolicLink(&DosDeviceName);

    if (DptDeviceObject != NULL)
	{
        IoDeleteDevice(DptDeviceObject);
        DptDeviceObject = NULL;
	}

	TRACE_FUNCTION_OUT();
}

VOID DPT_QueueMessage(_In_ PARSER_MESSAGE *pMessage)
{
	PLIST_ENTRY pEntry = NULL;
	PPARSER_MESSAGE_ENTRY pMessageEntry = NULL;
    KIRQL OldIrql;

	TRACE_FUNCTION_IN();

	KeAcquireSpinLock(&DptLock, &OldIrql);
	for (pEntry = DptSubscriptions.Flink; pEntry != &DptSubscriptions; pEntry = pEntry->Flink)
	{
		AUDIT_SUBSCRIPTION_CONTEXT *pContext = (AUDIT_SUBSCRIPTION_CONTEXT *)pEntry;
		// if was not able to send message immediatelly, put it in the queue
		if (!DPT_SendMessage(pContext, pMessage))
		{
			DPTLOG(LL_VERBOSE, "Queueing message to subscription context %p", pContext);
			pMessageEntry = ExAllocatePoolWithTag(NonPagedPool, sizeof(PARSER_MESSAGE_ENTRY), DptAllocationTag);
			pMessageEntry->Message = *pMessage;

			KeAcquireSpinLockAtDpcLevel(&pContext->Lock);
			InsertTailList(&pContext->PendedMessages, &pMessageEntry->Link);
			++pContext->PendedMessagesCount;
			KeReleaseSpinLockFromDpcLevel(&pContext->Lock);
		}
	}
    KeReleaseSpinLock(&DptLock, OldIrql);

	TRACE_FUNCTION_OUT();
}

/** Default major function dispatcher */
static NTSTATUS DPT_PassThrough(PDEVICE_OBJECT pDeviceObject, PIRP pIrp)
{
	UNREFERENCED_PARAMETER(pDeviceObject);

	//PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pDeviceObject->DeviceExtension;
	pIrp->IoStatus.Status = STATUS_SUCCESS;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

static NTSTATUS DPT_Open(PDEVICE_OBJECT pDeviceObject, PIRP pIrp)
{
	UNREFERENCED_PARAMETER(pDeviceObject);
	PIO_STACK_LOCATION pIrpSp = NULL;
	NTSTATUS Status = STATUS_SUCCESS;
	TRACE_FUNCTION_IN();

	pIrpSp = IoGetCurrentIrpStackLocation(pIrp);
	pIrpSp->FileObject->FsContext = NULL;

	TRACE_FUNCTION_OUT_STATUS(Status);

	pIrp->IoStatus.Status = Status;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return Status;
}

static NTSTATUS DPT_ReadCancel(PDEVICE_OBJECT pDeviceObject, PIRP pIrp)
{
	UNREFERENCED_PARAMETER(pDeviceObject);
	NTSTATUS Status = STATUS_SUCCESS;
	AUDIT_SUBSCRIPTION_CONTEXT *pContext = NULL;
    KIRQL OldIrql;

	IoReleaseCancelSpinLock(pIrp->CancelIrql);
	pContext = pIrp->Tail.Overlay.DriverContext[0];

	KeAcquireSpinLock(&pContext->Lock, &OldIrql);
	RemoveEntryList(&pIrp->Tail.Overlay.ListEntry);
	--pContext->PendedReadsCount;
    KeReleaseSpinLock(&pContext->Lock, OldIrql);

	DPTLOG(LL_INFO, "Read cancelled, IRP %p", pIrp);
	pIrp->IoStatus.Status = STATUS_CANCELLED;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return Status;
}

static VOID DPT_CancelPendingReads(AUDIT_SUBSCRIPTION_CONTEXT *pContext)
{
	PLIST_ENTRY pIrpEntry = NULL;
	PIRP pIrp = NULL;
	PPARSER_MESSAGE_ENTRY pMessage = NULL;
	PLIST_ENTRY pMessageEntry = NULL;
    KIRQL OldIrql;

	TRACE_FUNCTION_IN();

	KeAcquireSpinLock(&pContext->Lock, &OldIrql);

	// Clean message queue
	while (!IsListEmpty(&pContext->PendedMessages))
	{
		pMessageEntry = pContext->PendedMessages.Flink;
		pMessage = CONTAINING_RECORD(pMessageEntry, PARSER_MESSAGE_ENTRY, Link);
		RemoveEntryList(pMessageEntry);
		--pContext->PendedMessagesCount;
		ExFreePoolWithTag(pMessage, DptAllocationTag);
	}

	// Cancel pending IRP's
	while (!IsListEmpty(&pContext->PendedReads))
	{
		pIrpEntry = pContext->PendedReads.Flink;
		pIrp = CONTAINING_RECORD(pIrpEntry, IRP, Tail.Overlay.ListEntry);

		// Check if it is being cancelled
		if (IoSetCancelRoutine(pIrp, NULL))
		{
			// It isn't
			RemoveEntryList(pIrpEntry);
			--pContext->PendedReadsCount;
			// Release the lock before completing IRP
			KeReleaseSpinLockFromDpcLevel(&pContext->Lock);

			pIrp->IoStatus.Status = STATUS_CANCELLED;
			pIrp->IoStatus.Information = 0;

			DPTLOG(LL_INFO, "IRP %p cancelled", pIrp);

			IoCompleteRequest(pIrp, IO_NO_INCREMENT);

			KeAcquireSpinLockAtDpcLevel(&pContext->Lock);
		}
		else
		{
			// It's being cancelled, let the DPC to pump
            KeReleaseSpinLockFromDpcLevel(&pContext->Lock);
            LARGE_INTEGER delay;
            delay.QuadPart = -100000;	// 10 ms
            KeDelayExecutionThread(KernelMode, TRUE, &delay);
			KeAcquireSpinLockAtDpcLevel(&pContext->Lock);
		}
	}

	KeReleaseSpinLock(&pContext->Lock, OldIrql);

	TRACE_FUNCTION_OUT();
}

static NTSTATUS DPT_Read(PDEVICE_OBJECT pDeviceObject, PIRP pIrp)
{
	UNREFERENCED_PARAMETER(pDeviceObject);
	NTSTATUS Status = STATUS_SUCCESS;
	PIO_STACK_LOCATION pIrpSp = NULL;
	AUDIT_SUBSCRIPTION_CONTEXT *pContext = NULL;
	PLIST_ENTRY pMessageEntry = NULL;
	PPARSER_MESSAGE_ENTRY pMessage = NULL;
	PUCHAR pSource = NULL, pBuffer = NULL;
	ULONG BytesRemaining = 0, BytesToCopy = 0;
    KIRQL OldIrql;

	TRACE_FUNCTION_IN();

	pIrpSp = IoGetCurrentIrpStackLocation(pIrp);
	pContext = pIrpSp->FileObject->FsContext;

	if (pContext == NULL)
	{
		DPTLOG(LL_ERROR, "NULL FsContext on FileObject");
		Status = STATUS_INVALID_HANDLE;
		goto Cleanup;
	}

	if (pIrp->MdlAddress == NULL)
	{
		DPTLOG(LL_ERROR, "NULL MdlAddress on IRP %p", pIrp);
		Status = STATUS_INVALID_PARAMETER;
		goto Cleanup;
	}

	if (MmGetMdlByteCount(pIrp->MdlAddress) < sizeof(PARSER_MESSAGE))
	{
		DPTLOG(LL_ERROR, "User buffer too small");
		Status = STATUS_INVALID_BUFFER_SIZE;
		goto Cleanup;
	}

	if (NULL == (MmGetSystemAddressForMdlSafe(pIrp->MdlAddress, NormalPagePriority)))
	{
		DPTLOG(LL_ERROR, "MmGetSystemAddressForMdlSafe failed");
		Status = STATUS_INSUFFICIENT_RESOURCES;
		goto Cleanup;
	}

	KeAcquireSpinLock(&pContext->Lock, &OldIrql);

	IoSetCancelRoutine(pIrp, DPT_ReadCancel);

	if (pIrp->Cancel && IoSetCancelRoutine(pIrp, NULL))
	{
		//
		// IRP has been canceled but the I/O manager did not manage to call our cancel routine. This
		// code is safe referencing the Irp->Cancel field without locks because of the memory barriers
		// in the interlocked exchange sequences used by IoSetCancelRoutine.
		//

		Status = STATUS_CANCELLED;
	}
	else
	{
		if (!IsListEmpty(&pContext->PendedMessages))
		{
			pMessageEntry = pContext->PendedMessages.Flink;
			RemoveEntryList(pMessageEntry);
			--pContext->PendedMessagesCount;
			pMessage = CONTAINING_RECORD(pMessageEntry, PARSER_MESSAGE_ENTRY, Link);
			pSource = (PUCHAR)&pMessage->Message;

            pBuffer = (PUCHAR)MmGetSystemAddressForMdlSafe(pIrp->MdlAddress, NormalPagePriority) + pIrp->MdlAddress->ByteOffset;
            BytesRemaining = pIrp->MdlAddress->ByteCount;
            LOG_ASSERT(pBuffer);

			BytesToCopy = min(BytesRemaining, sizeof(PARSER_MESSAGE));
			memmove(pBuffer, pSource, BytesToCopy);

			ExFreePoolWithTag(pMessage, DptAllocationTag);

			Status = STATUS_SUCCESS;
			pIrp->IoStatus.Information = BytesToCopy;

			DPTLOG(LL_INFO, "IRP %p completed with %d bytes", pIrp, BytesToCopy);
		}
		else
		{
			InsertTailList(&pContext->PendedReads, &pIrp->Tail.Overlay.ListEntry);
			pIrp->Tail.Overlay.DriverContext[0] = pContext;
			++pContext->PendedReadsCount;

			Status = STATUS_PENDING;
			pIrp->IoStatus.Information = 0;
		}
	}

	KeReleaseSpinLock(&pContext->Lock, OldIrql);

Cleanup:
	pIrp->IoStatus.Status = Status;
	if (Status != STATUS_PENDING)
		IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	else
		IoMarkIrpPending(pIrp);

	TRACE_FUNCTION_OUT_STATUS(Status);
	return Status;
}

static NTSTATUS DPT_Write(PDEVICE_OBJECT pDeviceObject, PIRP pIrp)
{
	UNREFERENCED_PARAMETER(pDeviceObject);
	NTSTATUS Status = STATUS_SUCCESS;
	TRACE_FUNCTION_IN();

	Status = STATUS_INVALID_DEVICE_REQUEST;

	TRACE_FUNCTION_OUT_STATUS(Status);

	pIrp->IoStatus.Status = Status;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return Status;
}

static NTSTATUS DPT_Close(PDEVICE_OBJECT pDeviceObject, PIRP pIrp)
{
	UNREFERENCED_PARAMETER(pDeviceObject);
	NTSTATUS Status = STATUS_SUCCESS;
	PIO_STACK_LOCATION pIrpSp = NULL;
	AUDIT_SUBSCRIPTION_CONTEXT *pContext = NULL;
    KIRQL OldIrql;
	TRACE_FUNCTION_IN();

	KeAcquireSpinLock(&DptLock, &OldIrql);

	pIrpSp = IoGetCurrentIrpStackLocation(pIrp);
	pContext = pIrpSp->FileObject->FsContext;

	if (pContext)
	{
		DPT_CancelPendingReads(pContext);
        RemoveEntryList(&pContext->Link);
	}
	pIrpSp->FileObject->FsContext = NULL;

	KeReleaseSpinLock(&DptLock, OldIrql);
	TRACE_FUNCTION_OUT_STATUS(Status);

	pIrp->IoStatus.Status = Status;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return Status;
}

static NTSTATUS DPT_Control(PDEVICE_OBJECT pDeviceObject, PIRP pIrp)
{
    UNREFERENCED_PARAMETER(pDeviceObject);
    NTSTATUS IoStatus = STATUS_SUCCESS;

    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(pIrp);

    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode)
    {
    case IOCTL_VIRTUAL_DISK_SET_CIPHER:
        if (sizeof(EVHD_SET_CIPHER_CONFIG_REQUEST) ==
            IrpSp->Parameters.DeviceIoControl.InputBufferLength)
        {
            EVHD_SET_CIPHER_CONFIG_REQUEST *request = pIrp->AssociatedIrp.SystemBuffer;
            IoStatus = SetCipherOpts(&request->DiskId, request->Algorithm, &request->Opts);
        }
        else
            IoStatus = STATUS_INVALID_BUFFER_SIZE;
        break;
    }

    pIrp->IoStatus.Status = IoStatus;
    pIrp->IoStatus.Information = 0;
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static BOOLEAN DPT_SendMessage(AUDIT_SUBSCRIPTION_CONTEXT *pContext, PARSER_MESSAGE *pMessage)
{
	PLIST_ENTRY pIrpEntry = NULL;
	PIRP pIrp = NULL;
	PUCHAR pBuffer = NULL, pSource = (PUCHAR)pMessage;
	ULONG BytesRemaining = 0, BytesToCopy = 0;
	BOOLEAN Sent = FALSE;
    KIRQL OldIrql;

	KeAcquireSpinLock(&pContext->Lock, &OldIrql);

	if (!IsListEmpty(&pContext->PendedReads))
	{
		BOOLEAN FoundPendingIrp = FALSE;
		pIrpEntry = pContext->PendedReads.Flink;
		while (pIrpEntry != &pContext->PendedReads)
		{
			pIrp = CONTAINING_RECORD(pIrpEntry, IRP, Tail.Overlay.ListEntry);
			IoAcquireCancelSpinLock(&pIrp->CancelIrql);

			// Check if it is being cancelled
			if (IoSetCancelRoutine(pIrp, NULL))
			{
				// It isn't
				FoundPendingIrp = TRUE;
				RemoveEntryList(pIrpEntry);
				--pContext->PendedReadsCount;
				IoReleaseCancelSpinLock(pIrp->CancelIrql);
				break;
			}
			else
			{
				// It's being cancelled, try next
				DPTLOG(LL_INFO, "Skipping cancelled IRP %p", pIrp);
				pIrpEntry = pIrpEntry->Flink;
				IoReleaseCancelSpinLock(pIrp->CancelIrql);
			}
		}

		if (!FoundPendingIrp)
			goto Cleanup;

		KeReleaseSpinLockFromDpcLevel(&pContext->Lock);

        pBuffer = (PUCHAR)MmGetSystemAddressForMdlSafe(pIrp->MdlAddress, NormalPagePriority) + pIrp->MdlAddress->ByteOffset;
        BytesRemaining = pIrp->MdlAddress->ByteCount;
        LOG_ASSERT(pBuffer);

		BytesToCopy = min(BytesRemaining, sizeof(PARSER_MESSAGE));
		memmove(pBuffer, pSource, BytesToCopy);

		pIrp->IoStatus.Status = STATUS_SUCCESS;
		pIrp->IoStatus.Information = BytesToCopy;

		DPTLOG(LL_INFO, "IRP %p completed with %d bytes", pIrp, BytesToCopy);

		IoCompleteRequest(pIrp, IO_NO_INCREMENT);

		KeAcquireSpinLockAtDpcLevel(&pContext->Lock);
		Sent = TRUE;
	}
Cleanup:
	KeReleaseSpinLock(&pContext->Lock, OldIrql);
	return Sent;
}
