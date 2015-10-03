#include "cipher.h"
#include "XorCipher.h"
#include "AesCipher.h"
#include "Gost89Cipher.h"

const ULONG CipherPoolTag = 'CPHp';

typedef struct _CipherOptsEntry
{
	struct _CipherOptsEntry *Next;
	GUID DiskId;
	ECipherAlgo Algorithm;
	union
	{
		XorCipherOptions Xor;
		Aes128CipherOptions Aes128;
		Gost89CipherOptions Gost89;
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
		case ECipherAlgo_Xor:
			engine = &XorCipherEngine;
			break;
		case ECipherAlgo_AES128:
			engine = &Aes128CipherEngine;
			break;
		case ECipherAlgo_Gost89:
			engine = &Gost89CipherEngine;
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
	case ECipherAlgo_Xor:
	case ECipherAlgo_AES128:
	case ECipherAlgo_Gost89:
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
			case ECipherAlgo_AES128:
				memcpy(&pThisNode->Opts.Aes128, pCipherOpts, sizeof(Aes128CipherOptions));
				break;
			case ECipherAlgo_Gost89:
				memcpy(&pThisNode->Opts.Gost89, pCipherOpts, sizeof(Gost89CipherOptions));
				break;
			case ECipherAlgo_Xor:
				memcpy(&pThisNode->Opts.Xor, pCipherOpts, sizeof(XorCipherOptions));
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
