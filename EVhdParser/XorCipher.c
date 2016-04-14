#include "stdafx.h"
#include "XorCipher.h"
#include "utils.h"
#include "Log.h"
#include <xmmintrin.h>

const ULONG32 XorCipherTag = 'Xor ';

// xmmintrin workaround
int  _fltused = 0;

typedef struct {
	ULONG32		dwXorVal;
} XorCipherContext;

NTSTATUS XorCipherCreate(PVOID cipherConfig, PVOID *pOutContext)
{
	XorCipherContext *xorCipher = NULL;
	XorCipherOptions *xorOpts = (XorCipherOptions *)cipherConfig;
	if (!cipherConfig || !pOutContext)
		return STATUS_INVALID_PARAMETER;
	xorCipher = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(XorCipherContext), XorCipherTag);
	if (!xorCipher)
	{
		LOG_FUNCTION(LL_FATAL, LOG_CTG_CIPHER, "Failed to allocate memory for XorCipherContext\n");
		return STATUS_NO_MEMORY;
	}
	xorCipher->dwXorVal = xorOpts->XorMixingValue;
	*pOutContext = xorCipher;
	return STATUS_SUCCESS;
}

NTSTATUS XorCipherDestroy(PVOID ctx)
{
	if (!ctx)
		return STATUS_INVALID_PARAMETER;
	ExFreePoolWithTag(ctx, XorCipherTag);
	return STATUS_SUCCESS;
}

NTSTATUS XorCipherInit(PVOID ctx, CONST VOID *iv)
{
	UNREFERENCED_PARAMETER(ctx);
	UNREFERENCED_PARAMETER(iv);
	return STATUS_SUCCESS;
}

NTSTATUS XorCipherCrypt(PVOID ctx, CONST VOID *source, VOID *target, SIZE_T size, SIZE_T sector)
{
    UNREFERENCED_PARAMETER(sector);
	SIZE_T i = 0;
	const ULONG32 dwXorVal = ((XorCipherContext *)ctx)->dwXorVal;
	if (size > 64)
	{
		register const __m128 xmmxor = _mm_set_ps1(*(float *)&dwXorVal);
		for (i = 0; i < (size & ~0x7); i += sizeof(__m128))
			((__m128 *)target)[i / sizeof(__m128)] = _mm_xor_ps(((const __m128 *)source)[i / sizeof(__m128)], xmmxor);
	}
	for (i = size & ~0x7; i < (size & ~3); i += sizeof(ULONG32))
		((ULONG32 *)target)[i / sizeof(ULONG32)] = ((const ULONG32 *)source)[i / sizeof(ULONG32)] ^ dwXorVal;
	for (i = size & ~0x7; i < size; ++i)
		((UCHAR *)target)[i] = ((const UCHAR *)source)[i] ^ (UCHAR)dwXorVal;
	return STATUS_SUCCESS;
}

CipherEngine XorCipherEngine =
{
	.dwBlockSize = 0,
	.dwKeySize = 0,
	.pfnCreate = XorCipherCreate,
	.pfnDestroy = XorCipherDestroy,
	.pfnInit = XorCipherInit,
	.pfnEncrypt = XorCipherCrypt,
	.pfnDecrypt = XorCipherCrypt
};
