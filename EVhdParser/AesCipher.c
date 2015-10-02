#include "AesCipher.h"
#include "utils.h"

const ULONG32 AesCipherTag = 'Aes ';

NTSTATUS Aes128CipherCreate(PVOID cipherConfig, PVOID *pOutContext)
{
	NTSTATUS status = STATUS_SUCCESS;
	Aes128CipherContext *aesCipher = NULL;
	Aes128CipherConfig *aesConfig = cipherConfig;
	PWSTR pszChainMode = NULL;

	if (!cipherConfig || !pOutContext)
		return STATUS_INVALID_PARAMETER;
	aesCipher = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(Aes128CipherContext), AesCipherTag);
	if (!aesCipher)
	{
		DEBUG("Aes128Cipher: Failed to allocate memory for Aes128CipherContext");
		return STATUS_NO_MEMORY;
	}
	RtlZeroMemory(aesCipher, sizeof(Aes128CipherContext));
	if (!NT_SUCCESS(status = BCryptOpenAlgorithmProvider(
		&aesCipher->hAlgorithm,
		BCRYPT_AES_ALGORITHM,
		NULL,
		0)))
	{
		DEBUG("Aes128Cipher: Failed to open AES algorithm provider");
		ExFreePoolWithTag(aesCipher, AesCipherTag);
		return status;
	}
	switch (aesConfig->ChainingMode)
	{
	case ChainingMode_CBC:
		pszChainMode = BCRYPT_CHAIN_MODE_CBC;
		break;
	case ChainingMode_CFB:
		pszChainMode = BCRYPT_CHAIN_MODE_CFB;
		break;
	case ChainingMode_CCM:
		pszChainMode = BCRYPT_CHAIN_MODE_CCM;
		break;
	case ChainingMode_GCM:
		pszChainMode = BCRYPT_CHAIN_MODE_GCM;
		break;
	case ChainingMode_ECB:
	default:
		pszChainMode = BCRYPT_CHAIN_MODE_ECB;
		break;
	}

	if (pszChainMode && !NT_SUCCESS(status = BCryptSetProperty(
		aesCipher->hAlgorithm,
		BCRYPT_CHAINING_MODE,
		(PUCHAR)pszChainMode,
		(ULONG)wcslen(pszChainMode) + 1,
		0)))
	{
		DEBUG("Aes128Cipher: Could not set chaingin mode");
	}

	*pOutContext = aesCipher;
	return STATUS_SUCCESS;
}

NTSTATUS Aes128CipherDestroy(PVOID ctx)
{
	NTSTATUS status = STATUS_SUCCESS;
	Aes128CipherContext *aesCipher = (Aes128CipherContext *)ctx;
	if (aesCipher->hKey)
		BCryptDestroyKey(aesCipher->hKey);
	if (aesCipher->hAlgorithm)
		BCryptCloseAlgorithmProvider(aesCipher->hAlgorithm, 0);
	if (aesCipher->pbKeyObject)
		ExFreePoolWithTag(aesCipher->pbKeyObject, AesCipherTag);
	ExFreePoolWithTag(aesCipher, AesCipherTag);

	return status;
}

NTSTATUS Aes128CipherInit(PVOID ctx, CONST VOID *key, CONST VOID *iv)
{
	NTSTATUS status = STATUS_SUCCESS;
	Aes128CipherContext *aesCipher = (Aes128CipherContext *)ctx;
	ULONG cbData = 0, cbBlockLen = 0;

	if (!aesCipher || aesCipher->hAlgorithm == NULL)
	{
		DEBUG("Aes128Cipher: invalid cipher");
		return STATUS_INVALID_HANDLE;
	}
	if (!key)
	{
		DEBUG("Aes128Cipher: The key cannot be null");
		return STATUS_INVALID_PARAMETER;
	}
	if (!NT_SUCCESS(status = BCryptGetProperty(
		aesCipher->hAlgorithm,
		BCRYPT_OBJECT_LENGTH,
		(PUCHAR)&aesCipher->cbKeyObject,
		sizeof(ULONG),
		&cbData,
		0)))
	{
		DEBUG("Aes128Cipher: Could not get key object length");
		return status;
	}

	if (!NT_SUCCESS(status = BCryptGetProperty(
		aesCipher->hAlgorithm,
		BCRYPT_BLOCK_LENGTH,
		(PUCHAR)&cbBlockLen,
		sizeof(ULONG),
		&cbData,
		0)))
	{
		DEBUG("Aes128Cipher: Could not get block length");
		return status;
	}

	if (cbBlockLen != Aes128CipherEngine.dwBlockSize)
	{
		DEBUG("Aes128Cipher: Block size do not match");
		return STATUS_INVALID_BUFFER_SIZE;
	}

	aesCipher->pbKeyObject = ExAllocatePoolWithTag(NonPagedPoolNx, aesCipher->cbKeyObject, AesCipherTag);
	if (!aesCipher->pbKeyObject)
	{
		DEBUG("Aes128Cipher: Failed to allocate memory for key object\n");
		return STATUS_NO_MEMORY;
	}

	if (!NT_SUCCESS(status = BCryptGenerateSymmetricKey(
		aesCipher->hAlgorithm,
		&aesCipher->hKey,
		aesCipher->pbKeyObject,
		aesCipher->cbKeyObject,
		(PUCHAR)key,
		Aes128CipherEngine.dwKeySize,
		0)))
	{
		DEBUG("Aes128Cipher: Could not generate key");
		ExFreePoolWithTag(aesCipher->pbKeyObject, AesCipherTag);
		aesCipher->pbKeyObject = NULL;
		return status;
	}

	if (iv)
	{
		memmove(aesCipher->iv, iv, cbBlockLen);
	}

	return status;
}

NTSTATUS Aes128CipherEncrypt(PVOID ctx, CONST VOID *plain, VOID *cipher, SIZE_T size)
{
	NTSTATUS status = STATUS_SUCCESS;
	Aes128CipherContext *aesCipher = (Aes128CipherContext *)ctx;
	if (!aesCipher || aesCipher->hKey == NULL)
	{
		DEBUG("Aes128Cipher: invalid or uninitialized cipher");
		return STATUS_INVALID_HANDLE;
	}
	ULONG cbResult = 0;
	UCHAR iv[16];
	memmove(iv, aesCipher->iv, sizeof(iv));

	if (!NT_SUCCESS(status = BCryptEncrypt(
		aesCipher->hKey,
		(PUCHAR)plain,
		(ULONG)size,
		NULL,
		iv,
		sizeof(iv),
		(PUCHAR)cipher,
		(ULONG)size,
		&cbResult,
		0)))
	{
		DEBUG("Aes128Cipher: failed to encrypt 0x%X bytes", size);
		return status;
	}
	return status;
}

NTSTATUS Aes128CipherDecrypt(PVOID ctx, CONST VOID *cipher, VOID *plain, SIZE_T size)
{
	NTSTATUS status = STATUS_SUCCESS;
	Aes128CipherContext *aesCipher = (Aes128CipherContext *)ctx;
	if (!aesCipher || aesCipher->hKey == NULL)
	{
		DEBUG("Aes128Cipher: invalid or uninitialized cipher");
		return STATUS_INVALID_HANDLE;
	}
	ULONG cbResult = 0;
	UCHAR iv[16];
	memmove(iv, aesCipher->iv, sizeof(iv));

	if (!NT_SUCCESS(status = BCryptDecrypt(
		aesCipher->hKey,
		(PUCHAR)cipher,
		(ULONG)size,
		NULL,
		iv,
		sizeof(iv),
		(PUCHAR)plain,
		(ULONG)size,
		&cbResult,
		0)))
	{
		DEBUG("Aes128Cipher: failed to decrypt 0x%X bytes", size);
		return status;
	}
	return status;
}

CipherEngine Aes128CipherEngine =
{
	.dwBlockSize = 16,
	.dwKeySize = 16,
	.pfnCreate = Aes128CipherCreate,
	.pfnDestroy = Aes128CipherDestroy,
	.pfnInit = Aes128CipherInit,
	.pfnEncrypt = Aes128CipherEncrypt,
	.pfnDecrypt = Aes128CipherDecrypt
};
