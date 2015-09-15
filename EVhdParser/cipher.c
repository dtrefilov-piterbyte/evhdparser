#include "cipher.h"
#include "XorCipher.h"

CipherEngine *CipherEngineGet(ECipherAlgo eCipherAlgo)
{
	switch (eCipherAlgo)
	{
	case ECipherAlgo_Xor:
		return &XorCipherEngine;
	}
	return NULL;
}
