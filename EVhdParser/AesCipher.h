#pragma once
#include "cipher.h"
#include <bcrypt.h>

typedef struct {
	EChainingMode ChainingMode;
} Aes128CipherConfig;

NTSTATUS Aes128CipherCreate(PVOID cipherConfig, PVOID *pOutContext);
NTSTATUS Aes128CipherDestroy(PVOID ctx);
NTSTATUS Aes128CipherInit(PVOID ctx, CONST VOID *key, CONST VOID *iv);
NTSTATUS Aes128CipherEncrypt(PVOID ctx, CONST VOID *clear, VOID *cipher, SIZE_T size);
NTSTATUS Aes128CipherDecrypt(PVOID ctx, CONST VOID *cipher, VOID *clear, SIZE_T size);

typedef struct {
	BCRYPT_ALG_HANDLE hAlgorithm;
	BCRYPT_KEY_HANDLE hKey;
	UCHAR iv[16];
	UCHAR *pbKeyObject;
	ULONG cbKeyObject;
} Aes128CipherContext;

extern CipherEngine Aes128CipherEngine;
