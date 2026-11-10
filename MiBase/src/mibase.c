#include "mibase.h"

#include <string.h>

#if MI_BASE_OS_WINDOWS
#include <Windows.h>
#endif

#if MI_BASE_OS_POSIX
#include <time.h>
#endif

#if MI_BASE_OS_FREERTOS
#include "FreeRTOS.h"
#include "task.h"
#endif

#if MI_BASE_OS_VXWORKS
#include <vxWorks.h>
#include <sysLib.h>
#include <tickLib.h>
#endif

#if MI_BASE_OS_THREADX
#include "tx_api.h"
#endif

#if MI_BASE_OS_TXW82X
#include "typesdef.h"
#include "osal/time.h"
#if !defined(MI_BASE_TXW82X_DISABLE_AV_PSRAM)
#include "lib/heap/av_psram_heap.h"
#define MI_BASE_TXW82X_USE_AV_PSRAM 1
#endif
#endif

#define MI_MEM_LARGE_MAGIC 0x4D4C474Du
#define MI_MEM_LARGE_KIND_DEFAULT 0u
#define MI_MEM_LARGE_KIND_AV_PSRAM 1u

typedef union mi_mem_large_header_u {
    struct {
        uint32_t magic;
        uint16_t kind;
        uint16_t reserved;
        size_t size;
    } f;
    void* align_ptr;
    long double align_ld;
} mi_mem_large_header_t;

static void* mi_mem_malloc_default(int len)
{
    if (len <= 0) {
        return NULL;
    }

#if MI_BASE_OS_FREERTOS
    return pvPortMalloc((size_t)len);
#else
    return malloc((size_t)len);
#endif
}

static void mi_mem_free_default(void* pf)
{
    if (pf == NULL) {
        return;
    }

#if MI_BASE_OS_FREERTOS
    vPortFree(pf);
#else
    free(pf);
#endif
}

void* mi_mem_malloc(int len)
{
    return mi_mem_malloc_default(len);
}

void mi_mem_free(void* pf)
{
    mi_mem_free_default(pf);
}

static void* mi_mem_large_alloc_raw(int len, uint16_t* kind)
{
    void* ptr;

    if (kind != NULL) {
        *kind = MI_MEM_LARGE_KIND_DEFAULT;
    }

#if MI_BASE_OS_TXW82X && defined(MI_BASE_TXW82X_USE_AV_PSRAM)
    ptr = av_psram_malloc(len);
    if (ptr != NULL) {
        if (kind != NULL) {
            *kind = MI_MEM_LARGE_KIND_AV_PSRAM;
        }
        return ptr;
    }
#endif

    ptr = mi_mem_malloc_default(len);
    return ptr;
}

static void mi_mem_large_free_raw(void* ptr, uint16_t kind)
{
    if (ptr == NULL) {
        return;
    }

#if MI_BASE_OS_TXW82X && defined(MI_BASE_TXW82X_USE_AV_PSRAM)
    if (kind == MI_MEM_LARGE_KIND_AV_PSRAM) {
        av_psram_free(ptr);
        return;
    }
#else
    (void)kind;
#endif

    mi_mem_free_default(ptr);
}

void* mi_mem_malloc_large(int len)
{
    mi_mem_large_header_t* header;
    uint16_t kind;
    int total;

    if (len <= 0 || len > (int)(0x7fffffffu - sizeof(*header))) {
        return NULL;
    }

    total = (int)(sizeof(*header) + (size_t)len);
    header = (mi_mem_large_header_t*)mi_mem_large_alloc_raw(total, &kind);
    if (header == NULL) {
        return NULL;
    }

    header->f.magic = MI_MEM_LARGE_MAGIC;
    header->f.kind = kind;
    header->f.reserved = 0;
    header->f.size = (size_t)len;
    return header + 1;
}

void* mi_mem_calloc_large(int count, int len)
{
    void* ptr;
    int total;

    if (count <= 0 || len <= 0 || count > 0x7fffffff / len) {
        return NULL;
    }

    total = count * len;
    ptr = mi_mem_malloc_large(total);
    if (ptr != NULL) {
        memset(ptr, 0, (size_t)total);
    }
    return ptr;
}

void mi_mem_free_large(void* pf)
{
    mi_mem_large_header_t* header;

    if (pf == NULL) {
        return;
    }

    header = ((mi_mem_large_header_t*)pf) - 1;
    if (header->f.magic != MI_MEM_LARGE_MAGIC) {
        mi_mem_free_default(pf);
        return;
    }

    header->f.magic = 0;
    mi_mem_large_free_raw(header, header->f.kind);
}

uint32_t mi_getTick32(void)
{
#if MI_BASE_OS_WINDOWS
    return GetTickCount();
#elif MI_BASE_OS_POSIX
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000u));
#elif MI_BASE_OS_FREERTOS
#if defined(portTICK_PERIOD_MS)
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
#else
    return (uint32_t)((uint64_t)xTaskGetTickCount() * 1000u / configTICK_RATE_HZ);
#endif
#elif MI_BASE_OS_VXWORKS
    int rate = sysClkRateGet();
    if (rate <= 0) {
        return 0;
    }
    return (uint32_t)((uint64_t)tickGet() * 1000u / (uint32_t)rate);
#elif MI_BASE_OS_THREADX
#ifndef MI_BASE_THREADX_TICKS_PER_SECOND
#define MI_BASE_THREADX_TICKS_PER_SECOND 100u
#endif
    return (uint32_t)((uint64_t)tx_time_get() * 1000u / MI_BASE_THREADX_TICKS_PER_SECOND);
#elif MI_BASE_OS_TXW82X
    return (uint32_t)os_mseconds();
#else
    return 0;
#endif
}
