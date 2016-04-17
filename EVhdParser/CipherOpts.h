#pragma once  
#ifdef _WINKRNL
#include <ntifs.h>
#else
#endif

typedef enum
{
	ECipherAlgo_Disabled,
    ECipherAlgo_AesXts,
} ECipherAlgo;

typedef struct
{
    UCHAR CryptoKey[32];
    UCHAR TweakKey[32];
} AesXtsCipherOptions;
