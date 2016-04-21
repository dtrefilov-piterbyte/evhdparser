#pragma once  
#ifdef _WINKRNL
#include <ntifs.h>
#else
#endif

typedef enum
{
	ECipherAlgo_Disabled,
    ECipherAlgo_AesXts,
    ECipherAlgo_TwofishXts,
    ECipherAlgo_SerpentXts,
} ECipherAlgo;

typedef struct
{
    UCHAR CryptoKey[32];
    UCHAR TweakKey[32];
} Xts256CipherOptions;

typedef struct
{
    UCHAR CryptoKeyInner[32];
    UCHAR TweakKeyInner[32];

    UCHAR CryptoKeyOuter[32];
    UCHAR TweakKeyOuter[32];
} Xts256CascadeCipherOptions;
