#include "cipher.h"
#include "XorCipher.h"
#include "AesCipher.h"
#include "Gost89Cipher.h"

CipherEngine *CipherEngineGet(ECipherAlgo eCipherAlgo)
{
	switch (eCipherAlgo)
	{
	case ECipherAlgo_Xor:
		return &XorCipherEngine;
	case ECipherAlgo_AES128:
		return &Aes128CipherEngine;
	case ECipherAlgo_Gost89:
		return &Gost89CipherEngine;
	}
	return NULL;
}
