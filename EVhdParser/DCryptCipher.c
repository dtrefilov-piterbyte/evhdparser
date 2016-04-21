#include "stdafx.h"
#include "DCryptCipher.h"
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

NTSTATUS DCryptCipherCreate(PVOID cipherConfig, INT algId, PVOID *pOutContext)
{
    UCHAR key[XTS_FULL_KEY] = { 0 };
    DiskCryptorCipherContext *context = NULL;
    Xts256CipherOptions *pOptions = cipherConfig;
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
    xts_set_key(key, algId, &context->dcrypt);
    *pOutContext = context;
    return STATUS_SUCCESS;
}

NTSTATUS DCryptCipherDestroy(PVOID ctx)
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

NTSTATUS AesXtsCipherCreate(PVOID cipherConfig, PVOID pOutContext)
{
    return DCryptCipherCreate(cipherConfig, CF_AES, pOutContext);
}

NTSTATUS TwofishXtsCipherCreate(PVOID cipherConfig, PVOID pOutContext)
{
    return DCryptCipherCreate(cipherConfig, CF_TWOFISH, pOutContext);
}

NTSTATUS SerpentXtsCipherCreate(PVOID cipherConfig, PVOID pOutContext)
{
    return DCryptCipherCreate(cipherConfig, CF_SERPENT, pOutContext);
}

CipherEngine AesXtsCipherEngine =
{
    .dwBlockSize = XTS_BLOCK_SIZE,
    .dwKeySize = XTS_KEY_SIZE,
    .pfnCreate = AesXtsCipherCreate,
    .pfnDestroy = DCryptCipherDestroy,
    .pfnInit = DCryptCipherInit,
    .pfnEncrypt = DCryptCipherEncrypt,
    .pfnDecrypt = DCryptCipherDecrypt
};

CipherEngine TwofishXtsCipherEngine =
{
    .dwBlockSize = XTS_BLOCK_SIZE,
    .dwKeySize = XTS_KEY_SIZE,
    .pfnCreate = TwofishXtsCipherCreate,
    .pfnDestroy = DCryptCipherDestroy,
    .pfnInit = DCryptCipherInit,
    .pfnEncrypt = DCryptCipherEncrypt,
    .pfnDecrypt = DCryptCipherDecrypt
};

CipherEngine SerpentXtsCipherEngine =
{
    .dwBlockSize = XTS_BLOCK_SIZE,
    .dwKeySize = XTS_KEY_SIZE,
    .pfnCreate = SerpentXtsCipherCreate,
    .pfnDestroy = DCryptCipherDestroy,
    .pfnInit = DCryptCipherInit,
    .pfnEncrypt = DCryptCipherEncrypt,
    .pfnDecrypt = DCryptCipherDecrypt
};
