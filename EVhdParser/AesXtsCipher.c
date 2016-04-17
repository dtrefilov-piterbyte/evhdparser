#include "stdafx.h"
#include "AesXtsCipher.h"
#include "utils.h"
#include "Log.h"

#pragma warning(push)
#pragma warning(disable:4115)
#include <../dcrypt/crypto/crypto_fast/xts_fast.h>
#pragma warning(pop)

const ULONG32 CipherTag = 'Cphr';

// xmmintrin workaround
int  _fltused = 0;

typedef struct {
    xts_key dcrypt;
} DiskCryptorCipherContext;

NTSTATUS AesXtsCipherCreate(PVOID cipherConfig, PVOID *pOutContext)
{
    UCHAR key[XTS_FULL_KEY] = { 0 };
    DiskCryptorCipherContext *context = NULL;
    AesXtsCipherOptions *pOptions = cipherConfig;
    if (!cipherConfig || !pOutContext)
        return STATUS_INVALID_PARAMETER;
    context = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(DiskCryptorCipherContext), CipherTag);
    if (!context)
    {
        LOG_FUNCTION(LL_FATAL, LOG_CTG_CIPHER, "Failed to allocate memory for DiskCryptorCipherContext\n");
        return STATUS_NO_MEMORY;
    }
    memmove(key, pOptions->CryptoKey, XTS_KEY_SIZE);
    memmove(key + XTS_KEY_SIZE, pOptions->TweakKey, XTS_KEY_SIZE);
    xts_set_key(key, CF_AES, &context->dcrypt);
    *pOutContext = context;
    return STATUS_SUCCESS;
}

NTSTATUS AesXtsCipherDestroy(PVOID ctx)
{
    DiskCryptorCipherContext * pContext = ctx;
    RtlZeroMemory(&pContext->dcrypt, sizeof(xts_key));
    ExFreePoolWithTag(ctx, CipherTag);
    return STATUS_SUCCESS;
}

NTSTATUS DCryptCipherInit(PVOID ctx, CONST VOID *iv)
{
    UNREFERENCED_PARAMETER(ctx);
    UNREFERENCED_PARAMETER(iv);
    return STATUS_SUCCESS;
}

NTSTATUS DCryptCipherEncrypt(PVOID ctx, CONST VOID *source, VOID *target, SIZE_T size, SIZE_T sector)
{
    LOG_ASSERT(size % XTS_SECTOR_SIZE == 0);
    DiskCryptorCipherContext * pContext = ctx;
    xts_encrypt(source, target, size, sector * XTS_SECTOR_SIZE, &pContext->dcrypt);
    return STATUS_SUCCESS;
}

NTSTATUS DCryptCipherDecrypt(PVOID ctx, CONST VOID *source, VOID *target, SIZE_T size, SIZE_T sector)
{
    LOG_ASSERT(size % XTS_SECTOR_SIZE == 0);
    DiskCryptorCipherContext * pContext = ctx;
    xts_decrypt(source, target, size, sector * XTS_SECTOR_SIZE, &pContext->dcrypt);
    return STATUS_SUCCESS;
}

CipherEngine AesXtsCipherEngine =
{
    .dwBlockSize = XTS_BLOCK_SIZE,
    .dwKeySize = XTS_KEY_SIZE,
    .pfnCreate = AesXtsCipherCreate,
    .pfnDestroy = AesXtsCipherDestroy,
    .pfnInit = DCryptCipherInit,
    .pfnEncrypt = DCryptCipherEncrypt,
    .pfnDecrypt = DCryptCipherDecrypt
};
