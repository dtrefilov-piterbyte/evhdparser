#pragma once
#include "cipher.h"

NTSTATUS XorCipherCreate(PVOID cipherConfig, PVOID *pOutContext);
NTSTATUS XorCipherDestroy(PVOID ctx);
NTSTATUS XorCipherInit(PVOID ctx, CONST VOID *iv);
NTSTATUS XorCipherCrypt(PVOID ctx, CONST VOID *source, VOID *target, SIZE_T size, SIZE_T sector);

extern CipherEngine XorCipherEngine;
