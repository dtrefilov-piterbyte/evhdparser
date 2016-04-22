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

typedef struct _LOG_SETTINGS {
    ULONG32 LogLevel;
    ULONG32 LogCategories;
    ULONG32 MaxFileSize;
    ULONG32 MaxKeptRotatedFiles;

    UINT8 Reserved[32];
} LOG_SETTINGS;

C_ASSERT(sizeof(LOG_SETTINGS) == 48);

typedef struct _CREATE_SUBSCRIPTION_REQUEST
{
    BOOLEAN Servicing;
    UCHAR Reserved[3];
} CREATE_SUBSCRIPTION_REQUEST;

typedef struct _PARSER_RESPONSE_MESSAGE
{
    LONG RequestId;

    enum
    {
        MessageTypeResponseCipherConfig
    } Type;

    union
    {
        EVHD_SET_CIPHER_CONFIG_REQUEST CipherConfig;
        UINT8 Raw[0x100];
    } Message;
} PARSER_RESPONSE_MESSAGE;

#define MESSAGE_LENGTH 1016

typedef struct _PARSER_MESSAGE {
    enum {
        MessageTypeNone,
        MessageTypeQueryCipherConfig
    } Type;
    LONG RequestId;

    union {
        UCHAR Raw[MESSAGE_LENGTH];
        struct {
            GUID DiskId;
            GUID ApplicationId;
        } QueryCipherConfig;
    } Message;
} PARSER_MESSAGE;

C_ASSERT(sizeof(PARSER_MESSAGE) == 1024);

#define IOCTL_VIRTUAL_DISK_SET_CIPHER		    CTL_CODE(FILE_DEVICE_VIRTUAL_DISK, 0x2001, METHOD_IN_DIRECT, FILE_WRITE_ACCESS)	
#define IOCTL_VIRTUAL_DISK_SET_LOGGER           CTL_CODE(FILE_DEVICE_VIRTUAL_DISK, 0x2002, METHOD_IN_DIRECT, FILE_WRITE_ACCESS)
#define IOCTL_VIRTUAL_DISK_GET_LOGGER           CTL_CODE(FILE_DEVICE_VIRTUAL_DISK, 0x2003, METHOD_OUT_DIRECT, FILE_READ_ACCESS)
#define IOCTL_VIRTUAL_DISK_CREATE_SUBSCRIPTION  CTL_CODE(FILE_DEVICE_VIRTUAL_DISK, 0x2004, METHOD_IN_DIRECT, FILE_READ_ACCESS)
#define IOCTL_VIRTUAL_DISK_FINISH_REQUEST       CTL_CODE(FILE_DEVICE_VIRTUAL_DISK, 0x2005, METHOD_IN_DIRECT, FILE_WRITE_ACCESS)
