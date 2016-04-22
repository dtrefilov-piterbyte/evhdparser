#pragma once
#include "stdafx.h"
#include "Control.h"

NTSTATUS DPT_Initialize(_In_ PDRIVER_OBJECT, _In_ PCUNICODE_STRING pRegistryPath, _Out_ PDEVICE_OBJECT *pDeviceObject);
VOID DPT_Cleanup();
VOID DPT_QueueMessage(_In_ PARSER_MESSAGE *pMessage);
BOOLEAN DPT_SynchronouseRequest(_Inout_ PARSER_MESSAGE *pRequest, _Out_opt_ PARSER_RESPONSE_MESSAGE *pResponse, _In_ ULONG TimeoutMs);
