#pragma once		
#ifdef _WINKRNL
#include <ntifs.h>
#include <devioctl.h>
#else
#include <winioctl.h>
#endif
#include "CipherOpts.h"

typedef struct
{
	GUID DiskId;
	ECipherAlgo Algorithm;
	union
	{
        Xts256CipherOptions Xts256;
        Xts256CascadeCipherOptions Xts256Cascade;
		UCHAR Reserved[0xEC];
	} Opts;
} EVHD_SET_CIPHER_CONFIG_REQUEST;

C_ASSERT(sizeof(EVHD_SET_CIPHER_CONFIG_REQUEST) == 0x100);

typedef struct
{
	GUID DiskId;
} EVHD_QUERY_CIPHER_CONFIG_REQUEST;

typedef struct
{
	GUID DiskId;
	ECipherAlgo Algorithm;
} EVHD_QUERY_CIPHER_CONFIG_RESPONSE;

typedef struct _LOG_SETTINGS {
    ULONG32 LogLevel;
    ULONG32 LogCategories;
    ULONG32 MaxFileSize;
    ULONG32 MaxKeptRotatedFiles;

    UINT8 Reserved[32];
} LOG_SETTINGS;

C_ASSERT(sizeof(LOG_SETTINGS) == 48);

#define MESSAGE_LENGTH 1020

typedef struct _PARSER_MESSAGE {
    enum {
        MessageTypeNone,
    } Type;

    union {
        UCHAR Raw[MESSAGE_LENGTH];
    } Message;
} PARSER_MESSAGE;

C_ASSERT(sizeof(PARSER_MESSAGE) == 1024);

#define IOCTL_VIRTUAL_DISK_SET_CIPHER		CTL_CODE(FILE_DEVICE_VIRTUAL_DISK, 0x2001, METHOD_IN_DIRECT, FILE_WRITE_ACCESS)	
#define IOCTL_VIRTUAL_DISK_GET_CIPHER		CTL_CODE(FILE_DEVICE_VIRTUAL_DISK, 0x2002, METHOD_OUT_DIRECT, FILE_READ_ACCESS)	
#define IOCTL_VIRTUAL_DISK_SET_LOGGER       CTL_CODE(FILE_DEVICE_VIRTUAL_DISK, 0x2003, METHOD_IN_DIRECT, FILE_WRITE_ACCESS)
#define IOCTL_VIRTUAL_DISK_GET_LOGGER       CTL_CODE(FILE_DEVICE_VIRTUAL_DISK, 0x2004, METHOD_OUT_DIRECT, FILE_READ_ACCESS)
