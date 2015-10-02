#pragma once
#include <ntifs.h>

typedef enum {
	ChainingMode_Unknown,
	ChainingMode_CBC,
	ChainingMode_ECB,
	ChainingMode_CFB,
	ChainingMode_OFB,
	ChainingMode_CCM,
	ChainingMode_GCM
} EChainingMode;

/** Creates cipher instance */
typedef NTSTATUS(*CipherCreate_t)(PVOID cipherConfig, PVOID *pOutContext);
/** Initializes cipher with the given key and iv */
typedef NTSTATUS(*CipherInit_t)(PVOID ctx, CONST VOID *key, CONST VOID *iv);
/** Cipher function performing data encryption */
typedef NTSTATUS(*CipherEnc_t)(PVOID ctx, CONST VOID *clear, VOID *cipher, SIZE_T size);
/** Cipher function performing data decryption */
typedef NTSTATUS(*CipherDec_t)(PVOID ctx, CONST VOID *cipher, VOID *clear, SIZE_T size);
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

typedef enum
{
	ECipherAlgo_Xor,
	ECipherAlgo_AES128,
	ECipherAlgo_Gost89
} ECipherAlgo;

CipherEngine *CipherEngineGet(ECipherAlgo eCipherAlgo);
