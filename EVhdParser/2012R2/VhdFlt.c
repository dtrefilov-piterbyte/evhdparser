#include "parser.h"
#include "Ioctl.h"
#include <fltKernel.h>

NTSTATUS EvhdRemoveVhd(ParserInstance *parser, PVOID systemBuffer)
{
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_OBJECT pDeviceObject = NULL;
	KeEnterCriticalRegion();
	FltAcquirePushLockExclusiveEx(&parser->IoLock, FLT_PUSH_LOCK_ENABLE_AUTO_BOOST);

	IoReuseIrp(parser->pIrp, STATUS_PENDING);
	parser->pIrp->Flags |= IRP_NOCACHE;
	parser->pIrp->Tail.Overlay.Thread = (PETHREAD)__readgsqword(0x188);		// Pointer to calling thread control block
	parser->pIrp->AssociatedIrp.SystemBuffer = systemBuffer;				// IO buffer for buffered control code
	// fill stack frame parameters for synchronous IRP call
	PIO_STACK_LOCATION pStackFrame = IoGetNextIrpStackLocation(parser->pIrp);
	pDeviceObject = IoGetRelatedDeviceObject(parser->pVhdmpFileObject);
	pStackFrame->FileObject = parser->pVhdmpFileObject;
	pStackFrame->DeviceObject = pDeviceObject;
	pStackFrame->Parameters.DeviceIoControl.IoControlCode = IOCTL_STORAGE_REMOVE_VIRTUAL_DISK;
	pStackFrame->Parameters.DeviceIoControl.InputBufferLength = 0xC;
	pStackFrame->Parameters.DeviceIoControl.OutputBufferLength = 0;
	pStackFrame->MajorFunction = IRP_MJ_DEVICE_CONTROL;
	pStackFrame->MinorFunction = 0;
	pStackFrame->Flags = 0;
	pStackFrame->Control = 0;
	IoSynchronousCallDriver(pDeviceObject, parser->pIrp);
	status = parser->pIrp->IoStatus.Status;
	FltReleasePushLockEx(&parser->IoLock, FLT_PUSH_LOCK_ENABLE_AUTO_BOOST);
	KeLeaveCriticalRegion();
	return status;
}
