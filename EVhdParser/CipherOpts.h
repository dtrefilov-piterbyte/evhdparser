#pragma once  
#ifdef _WINKRNL
#include <ntifs.h>
#else
#endif

typedef enum {
	OperationMode_Unknown,
	OperationMode_CBC,
	OperationMode_ECB,
	OperationMode_CFB,
	OperationMode_OFB,
	OperationMode_CCM,
	OperationMode_GCM
} EOperationMode;

typedef enum
{
	ECipherAlgo_Disabled,
	ECipherAlgo_Xor,
	ECipherAlgo_AES128,
	ECipherAlgo_Gost89
} ECipherAlgo;

typedef enum {
	ESBlock_GostR3411_94_TestParamSet,
	ESBlock_Gost28147_89_CryptoPro_A_ParamSet,
	ESBlock_Gost28147_89_CryptoPro_B_ParamSet,
	ESBlock_Gost28147_89_CryptoPro_C_ParamSet,
	ESBlock_Gost28147_89_CryptoPro_D_ParamSet,
	ESBlock_tc26_gost28147_param_Z
} ESBlock;

typedef struct
{
	ULONG32 XorMixingValue;
} XorCipherOptions;

typedef struct
{
	UCHAR Key[16];
	EOperationMode OperationMode;
} Aes128CipherOptions;

typedef struct
{
	UCHAR Key[32];
	ESBlock SBlock;
	EOperationMode OperationMode;
} Gost89CipherOptions;
