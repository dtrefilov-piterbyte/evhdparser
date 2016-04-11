#pragma once

#pragma warning(push)
#pragma warning(disable:4201)
#include <intsafe.h>
#include <strsafe.h>
#pragma warning(pop)

#define INITGUID
#include <guiddef.h>


#define TO_STR(text) #text
#define STRINGIZE(text) TO_STR(text)
#define CONCATENATE2(a, b) a##b
#define CONCATENATE(a, b) CONCATENATE2(a, b)

#define PEPROCESS __PEPROCESS
#define PETHREAD __PETHREAD
#include <fltKernel.h>
#include <ntifs.h>
