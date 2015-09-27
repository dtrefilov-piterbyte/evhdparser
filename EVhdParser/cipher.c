#include "cipher.h"
#include "XorCipher.h"
#include "AesCipher.h"

CipherEngine *CipherEngineGet(ECipherAlgo eCipherAlgo)
{
	switch (eCipherAlgo)
	{
	case ECipherAlgo_Xor:
		return &XorCipherEngine;
	case ECipherAlgo_AES128:
		return &Aes128CipherEngine;
	}
	return NULL;
}
