#pragma once
#include "cipher.h"
#include <bcrypt.h>

NTSTATUS Aes128CipherCreate(PVOID cipherConfig, PVOID *pOutContext);
NTSTATUS Aes128CipherDestroy(PVOID ctx);
NTSTATUS Aes128CipherInit(PVOID ctx, CONST VOID *iv);
NTSTATUS Aes128CipherEncrypt(PVOID ctx, CONST VOID *clear, VOID *cipher, SIZE_T size, SIZE_T sector);
NTSTATUS Aes128CipherDecrypt(PVOID ctx, CONST VOID *cipher, VOID *clear, SIZE_T size, SIZE_T sector);

typedef struct {
	BCRYPT_ALG_HANDLE hAlgorithm;
	BCRYPT_KEY_HANDLE hKey;
	UCHAR iv[16];
	UCHAR *pbKeyObject;
	ULONG cbKeyObject;
} Aes128CipherContext;

extern CipherEngine Aes128CipherEngine;
