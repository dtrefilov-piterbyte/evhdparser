#pragma once  
#ifdef _WINKRNL
#include <ntifs.h>
#else
#endif

typedef enum {
	ChainingMode_Unknown,
	ChainingMode_CBC,
	ChainingMode_ECB,
	ChainingMode_CFB,
	ChainingMode_OFB,
	ChainingMode_CCM,
	ChainingMode_GCM
} EChainingMode;

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
	EChainingMode ChainingMode;
} Aes128CipherOptions;

typedef struct
{
	UCHAR Key[32];
	ESBlock SBlock;
	EChainingMode ChainingMode;
} Gost89CipherOptions;
