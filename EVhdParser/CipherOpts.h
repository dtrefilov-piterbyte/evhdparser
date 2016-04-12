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
	ECipherAlgo_AES128
} ECipherAlgo;

typedef struct
{
	ULONG32 XorMixingValue;
} XorCipherOptions;

typedef struct
{
	UCHAR Key[16];
	EOperationMode OperationMode;
} Aes128CipherOptions;
