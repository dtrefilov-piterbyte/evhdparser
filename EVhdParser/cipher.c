#include "stdafx.h"
#include "cipher.h"
#include "DCryptCipher.h"
#include "Log.h"

#pragma warning(push)
#pragma warning(disable:4115)
#include <../dcrypt/crypto/crypto_fast/xts_fast.h>
#pragma warning(pop)

const ULONG CipherPoolTag = 'CPHp';

typedef struct _CipherOptsEntry
{
	struct _CipherOptsEntry *Next;
	GUID DiskId;
	ECipherAlgo Algorithm;
	union
	{
        Xts256CipherOptions Xts256;
        Xts256CascadeCipherOptions Xts256Cascade;
	} Opts;
} CipherOptsEntry;

CipherOptsEntry *g_pCipherOptsHead = NULL;
FAST_MUTEX g_pCipherOptsMutex;

NTSTATUS CipherEngineGet(PGUID pDiskId, CipherEngine **pOutCipherEngine, PVOID *pOutCipherContext)
{
	NTSTATUS status = STATUS_SUCCESS;
	CipherEngine *engine = NULL;
	CipherOptsEntry *pOptsNode = NULL, *pFoundNode = NULL;
	ExAcquireFastMutex(&g_pCipherOptsMutex);

	for (pOptsNode = g_pCipherOptsHead; pOptsNode; pOptsNode = pOptsNode->Next)
	{
		if (0 == memcmp(&pOptsNode->DiskId, pDiskId, sizeof(GUID)))
		{
			pFoundNode = pOptsNode;
			break;
		}
	}

	if (pFoundNode)
	{
		switch (pFoundNode->Algorithm)
		{
		case ECipherAlgo_AesXts:
			engine = &AesXtsCipherEngine;
			break;
		}

		if (engine)
		{
			PVOID pContext = NULL;
			status = engine->pfnCreate(&pFoundNode->Opts, &pContext);
			if (NT_SUCCESS(status))
			{
				*pOutCipherEngine = engine;
				*pOutCipherContext = pContext;
			}
		}
	}

	ExReleaseFastMutex(&g_pCipherOptsMutex);
	return status;
}

NTSTATUS CipherInit()
{
    const UINT hw_crypt = TRUE;
    xts_init(hw_crypt);
	ExInitializeFastMutex(&g_pCipherOptsMutex);
	return STATUS_SUCCESS;
}

NTSTATUS CipherCleanup()
{
	ExReleaseFastMutex(&g_pCipherOptsMutex);
	return STATUS_SUCCESS;
}

NTSTATUS SetCipherOpts(PGUID pDiskId, ECipherAlgo Algorithm, PVOID pCipherOpts)
{
	NTSTATUS status = STATUS_SUCCESS;
	CipherOptsEntry *pOptsNode = NULL, *pThisNode = NULL;
	ExAcquireFastMutex(&g_pCipherOptsMutex);

	switch (Algorithm)
	{
	case ECipherAlgo_Disabled:
    case ECipherAlgo_AesXts:
    case ECipherAlgo_TwofishXts:
    case ECipherAlgo_SerpentXts:
		// Try find existing opts in a list
		for (pOptsNode = g_pCipherOptsHead; pOptsNode; pOptsNode = pOptsNode->Next)
		{
			if (0 == memcmp(&pOptsNode->DiskId, pDiskId, sizeof(GUID)))
			{
				pThisNode = pOptsNode;
				break;
			}
		}

		if (!pThisNode)
		{
			pThisNode = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(CipherOptsEntry), CipherPoolTag);
			if (pThisNode)
			{
				pThisNode->Next = g_pCipherOptsHead;
				g_pCipherOptsHead = pThisNode;
			}
			else
				status = STATUS_NO_MEMORY;
		}

		if (pThisNode)
		{
			memcpy(&pThisNode->DiskId, pDiskId, sizeof(GUID));
			pThisNode->Algorithm = Algorithm;
			switch (Algorithm)
			{
            case ECipherAlgo_AesXts:
            case ECipherAlgo_TwofishXts:
            case ECipherAlgo_SerpentXts:
				memcpy(&pThisNode->Opts.Xts256, pCipherOpts, sizeof(Xts256CipherOptions));
				break;
			}
		}
		break;
	default:
        
		status = STATUS_INVALID_PARAMETER;
	}

	ExReleaseFastMutex(&g_pCipherOptsMutex);
	return status;
}
