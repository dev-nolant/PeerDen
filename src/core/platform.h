#pragma once

#ifdef _WIN32
// ssize_t is POSIX; MSVC doesn't have it
#ifndef _SSIZE_T_DEFINED
#ifdef _MSC_VER
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#else
typedef long long ssize_t;
#endif
#define _SSIZE_T_DEFINED
#endif
#else
// POSIX (macOS, Linux): ssize_t from sys/types.h or unistd.h
#include <sys/types.h>
#endif

// Windows headers define Send/Receive/Read/Write as macros; undef to avoid breaking our API
#ifdef Send
#undef Send
#endif
#ifdef Receive
#undef Receive
#endif
#ifdef Read
#undef Read
#endif
#ifdef Write
#undef Write
#endif
