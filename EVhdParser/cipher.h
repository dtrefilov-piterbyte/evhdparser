#pragma once
#include <ntifs.h>
#include "CipherOpts.h"

/** Creates cipher instance */
typedef NTSTATUS(*CipherCreate_t)(PVOID cipherConfig, PVOID *pOutContext);
/** Initializes cipher with the given iv */
typedef NTSTATUS(*CipherInit_t)(PVOID ctx, CONST VOID *iv);
/** Cipher function performing data encryption */
typedef NTSTATUS(*CipherEnc_t)(PVOID ctx, CONST VOID *clear, VOID *cipher, SIZE_T size, SIZE_T sector);
/** Cipher function performing data decryption */
typedef NTSTATUS(*CipherDec_t)(PVOID ctx, CONST VOID *cipher, VOID *clear, SIZE_T size, SIZE_T sector);
/** Destroys cipher instance */
typedef NTSTATUS(*CipherDestroy_t)(PVOID ctx);

typedef struct _CipherEngine {
	ULONG32			dwBlockSize;
	ULONG32			dwKeySize;
	CipherCreate_t	pfnCreate;
	CipherDestroy_t	pfnDestroy;
	CipherInit_t	pfnInit;
	CipherEnc_t		pfnEncrypt;
	CipherDec_t		pfnDecrypt;
} CipherEngine;

NTSTATUS CipherEngineGet(PGUID pDiskId, CipherEngine **pOutCipherEngine, PVOID *pOutCipherContext);

NTSTATUS CipherInit();
NTSTATUS CipherCleanup();

NTSTATUS SetCipherOpts(PGUID pDiskId, ECipherAlgo Algorithm, PVOID pCipherOpts);
