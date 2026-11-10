#ifndef MiBase_H_
#define MiBase_H_

#if defined(__linux__) || defined(__APPLE__) || defined(__unix__) || defined(MI_BASE_POSIX)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32) || defined(WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WINVER
#define WINVER _WIN32_WINNT
#endif
#endif

#define MI_BASE_PLATFORM_UNKNOWN   0
#define MI_BASE_PLATFORM_WINDOWS   1
#define MI_BASE_PLATFORM_POSIX     2
#define MI_BASE_PLATFORM_FREERTOS  3
#define MI_BASE_PLATFORM_VXWORKS   4
#define MI_BASE_PLATFORM_THREADX   5
#define MI_BASE_PLATFORM_TXW82X    6

#if defined(TXW82X) || defined(MI_BASE_TXW82X)
#define MI_BASE_OS_TXW82X 1
#define MI_BASE_PLATFORM MI_BASE_PLATFORM_TXW82X
#elif defined(FREE_RTOS) || defined(FREERTOS) || defined(MI_BASE_FREERTOS)
#define MI_BASE_OS_FREERTOS 1
#define MI_BASE_PLATFORM MI_BASE_PLATFORM_FREERTOS
#elif defined(THREADX) || defined(MI_BASE_THREADX)
#define MI_BASE_OS_THREADX 1
#define MI_BASE_PLATFORM MI_BASE_PLATFORM_THREADX
#elif defined(VXWORKS) || defined(__VXWORKS__) || defined(MI_BASE_VXWORKS)
#define MI_BASE_OS_VXWORKS 1
#define MI_BASE_PLATFORM MI_BASE_PLATFORM_VXWORKS
#elif defined(_WIN32) || defined(WIN32)
#define MI_BASE_OS_WINDOWS 1
#define MI_BASE_PLATFORM MI_BASE_PLATFORM_WINDOWS
#elif defined(__linux__) || defined(__APPLE__) || defined(__unix__) || defined(MI_BASE_POSIX)
#ifndef MI_BASE_POSIX
#define MI_BASE_POSIX 1
#endif
#define MI_BASE_OS_POSIX 1
#define MI_BASE_PLATFORM MI_BASE_PLATFORM_POSIX
#else
#define MI_BASE_PLATFORM MI_BASE_PLATFORM_UNKNOWN
#endif

#ifndef MI_BASE_OS_WINDOWS
#define MI_BASE_OS_WINDOWS 0
#endif
#ifndef MI_BASE_OS_POSIX
#define MI_BASE_OS_POSIX 0
#endif
#ifndef MI_BASE_OS_FREERTOS
#define MI_BASE_OS_FREERTOS 0
#endif
#ifndef MI_BASE_OS_VXWORKS
#define MI_BASE_OS_VXWORKS 0
#endif
#ifndef MI_BASE_OS_THREADX
#define MI_BASE_OS_THREADX 0
#endif
#ifndef MI_BASE_OS_TXW82X
#define MI_BASE_OS_TXW82X 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

void* mi_mem_malloc(int len);
void  mi_mem_free(void* pf);
void* mi_mem_malloc_large(int len);
void* mi_mem_calloc_large(int count, int len);
void  mi_mem_free_large(void* pf);
uint32_t mi_getTick32(void);

#ifdef __cplusplus
}
#endif

#endif
