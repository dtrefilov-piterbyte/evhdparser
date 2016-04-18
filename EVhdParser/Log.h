#pragma once
#include "Control.h"

typedef enum _LOG_LEVEL
{
	LL_FATAL = 1,
	LL_ERROR = 2,
	LL_WARNING = 3,
	LL_INFO = 4,
	LL_VERBOSE = 5,
	LL_DEBUG = 6,
	LL_MAX
} LOG_LEVEL;

#define LOG_CTG_GENERAL             1
#define LOG_CTG_PARSER              2
#define LOG_CTG_CIPHER              4
#define LOG_CTG_DISPATCH            8
#define LOG_CTG_EXTENSION           16

#define LOG_CTG_ALL (LOG_CTG_GENERAL | LOG_CTG_PARSER | LOG_CTG_CIPHER | LOG_CTG_DISPATCH | LOG_CTG_EXTENSION)
#define LOG_CTG_DEFAULT (LOG_CTG_GENERAL | LOG_CTG_PARSER | LOG_CTG_CIPHER | LOG_CTG_DISPATCH | LOG_CTG_EXTENSION)

#define GUID_FORMAT "%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX"
#define WGUID_FORMAT L"%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX"
#define GUID_PARAMETERS(guid) (guid).Data1, (guid).Data2, (guid).Data3, \
	(guid).Data4[0], (guid).Data4[1], (guid).Data4[2], (guid).Data4[3], \
	(guid).Data4[4], (guid).Data4[5], (guid).Data4[6], (guid).Data4[7]

extern LOG_SETTINGS LogSettings;

NTSTATUS Log_Initialize(PDEVICE_OBJECT DeviceObject, PUNICODE_STRING RegistryPath);
VOID Log_Cleanup();
NTSTATUS Log_Print(LOG_LEVEL Level, LPCSTR pszFormat, ...);
NTSTATUS Log_SetSetting(_In_ LOG_SETTINGS *Settings);
NTSTATUS Log_QueryLogSettings(_Out_ LOG_SETTINGS *Settings);

#define MULTILINE_BEGIN do{
#define MULTILINE_END                  \
	__pragma(warning(push))            \
	__pragma(warning(disable: 4127))   \
	}while(0);                         \
	__pragma(warning(pop))

#define TO_STR(text) #text
#define STRINGIZE(text) TO_STR(text)
#define CONCATENATE2(a, b) a##b
#define CONCATENATE(a, b) CONCATENATE2(a, b)

#define LOGPRINT_LVL_CTG(level, category, format, ...) \
	if (level <= LogSettings.LogLevel && (LogSettings.LogCategories & category) == category) \
		Log_Print(level, CONCATENATE(format, "\r\n"), __VA_ARGS__); \

#define LOG(level, category, format, ...) \
	MULTILINE_BEGIN                           \
	LOGPRINT_LVL_CTG(level, category, format, __VA_ARGS__) \
	MULTILINE_END

#define LOG_FUNCTION(level, category, format, ...) \
	LOG(level, category, CONCATENATE("["__FUNCTION__"] ", format), __VA_ARGS__)

#define TRACE_FUNCTION_IN() \
	LOG_FUNCTION(LL_VERBOSE, LOG_CTG_GENERAL, "=>")

#define TRACE_FUNCTION_OUT() \
	LOG_FUNCTION(LL_VERBOSE, LOG_CTG_GENERAL, "<=")

#define TRACE_FUNCTION_OUT_STATUS(Status) \
	LOG_FUNCTION(LL_VERBOSE, LOG_CTG_GENERAL, "<= 0x%08X", Status)

#define LOG_FATAL(category, format, ...) \
	LOG_FUNCTION(LL_FATAL, category, format, __VA_ARGS__)

#define LOG_ERROR(category, format, ...) \
	LOG_FUNCTION(LL_ERROR, category, format, __VA_ARGS__)

#define LOG_WARNING(category, format, ...) \
	LOG_FUNCTION(LL_WARNING, category, format, __VA_ARGS__)

#define LOG_INFO(category, format, ...) \
	LOG_FUNCTION(LL_INFO, category, format, __VA_ARGS__)

#define LOG_VERBOSE(category, format, ...) \
	LOG_FUNCTION(LL_VERBOSE, category, format, __VA_ARGS__)

#define LOG_DEBUG(category, format, ...) \
	LOG_FUNCTION(LL_DEBUG, category, format, __VA_ARGS__)

#define GNRLLOG(level, format, ...) LOG(level, LOG_CTG_GENERAL, format, __VA_ARGS__)

#define LOG_ASSERT( exp ) \
    ((!(exp)) ? \
        (Log_Print(LL_FATAL, CONCATENATE ( "["__FUNCTION__"] Assertion failed: " #exp , "\r\n" ) ), FALSE) : \
        TRUE)

#define LOG_ASSERTMSG( msg, exp ) \
    ((!(exp)) ? \
        (Log_Print(LL_FATAL, CONCATENATE ( CONCATENATE ( "["__FUNCTION__"] ", msg ) , "\r\n" ) ), FALSE) : \
        TRUE)
