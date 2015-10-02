#pragma once
#include "cipher.h"

typedef enum {
	ESBlock_GostR3411_94_TestParamSet,
	ESBlock_Gost28147_89_CryptoPro_A_ParamSet,
	ESBlock_Gost28147_89_CryptoPro_B_ParamSet,
	ESBlock_Gost28147_89_CryptoPro_C_ParamSet,
	ESBlock_Gost28147_89_CryptoPro_D_ParamSet,
	ESBlock_tc26_gost28147_param_Z
} ESBlock;

typedef struct {
	EChainingMode ChainingMode;
	ESBlock SBlock;
} Gost89CipherConfig;

NTSTATUS Gost89CipherCreate(PVOID cipherConfig, PVOID *pOutContext);
NTSTATUS Gost89CipherDestroy(PVOID ctx);
NTSTATUS Gost89CipherInit(PVOID ctx, CONST VOID *key, CONST VOID *iv);
NTSTATUS Gost89CipherEncrypt(PVOID ctx, CONST VOID *clear, VOID *cipher, SIZE_T size);
NTSTATUS Gost89CipherDecrypt(PVOID ctx, CONST VOID *cipher, VOID *clear, SIZE_T size);

typedef struct {
	/** constant s-boxes */
	ULONG32 k87[256], k65[256], k43[256], k21[256];
	/** 256-bit key */
	ULONG32 k[8];
	/** Cipher mode selected is CFB */
	BOOLEAN		bIsCfb;
	/** Initialization vector */
	UCHAR		iv[8];
} Gost89CipherContext;

extern CipherEngine Gost89CipherEngine;
