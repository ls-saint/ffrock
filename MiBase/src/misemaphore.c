#include "misemaphore.h"

#if MI_BASE_OS_WINDOWS
#include <Windows.h>
#endif

#if MI_BASE_OS_POSIX
#include <errno.h>
#include <pthread.h>
#include <time.h>
#endif

#if MI_BASE_OS_FREERTOS
#include "FreeRTOS.h"
#include "semphr.h"
#endif

#if MI_BASE_OS_VXWORKS
#include <vxWorks.h>
#include <semLib.h>
#include <sysLib.h>
#endif

#if MI_BASE_OS_THREADX
#include "tx_api.h"
#endif

#if MI_BASE_OS_TXW82X
#include "typesdef.h"
#include "osal/semaphore.h"
#endif

struct mi_Semaphore {
    int max_count;
#if MI_BASE_OS_WINDOWS
    HANDLE sem;
#elif MI_BASE_OS_POSIX
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;
#elif MI_BASE_OS_FREERTOS
    SemaphoreHandle_t sem;
#elif MI_BASE_OS_VXWORKS
    SEM_ID sem;
#elif MI_BASE_OS_THREADX
    TX_SEMAPHORE sem;
#elif MI_BASE_OS_TXW82X
    os_semaphore_t sem;
#endif
};

#if MI_BASE_OS_POSIX
static void make_abs_timespec(struct timespec* ts, uint32_t timeout_ms)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += (time_t)(timeout_ms / 1000u);
    ts->tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000L;
    }
}
#endif

#if MI_BASE_OS_FREERTOS
static TickType_t timeout_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == 0) {
        return portMAX_DELAY;
    }
    return pdMS_TO_TICKS(timeout_ms);
}
#endif

#if MI_BASE_OS_VXWORKS
static int timeout_to_ticks(uint32_t timeout_ms)
{
    int rate;
    if (timeout_ms == 0) {
        return WAIT_FOREVER;
    }
    rate = sysClkRateGet();
    if (rate <= 0) {
        return 1;
    }
    return (int)(((uint64_t)timeout_ms * (uint32_t)rate + 999u) / 1000u);
}
#endif

#if MI_BASE_OS_THREADX
static ULONG timeout_to_ticks(uint32_t timeout_ms)
{
#ifndef MI_BASE_THREADX_TICKS_PER_SECOND
#define MI_BASE_THREADX_TICKS_PER_SECOND 100u
#endif
    if (timeout_ms == 0) {
        return TX_WAIT_FOREVER;
    }
    return (ULONG)(((uint64_t)timeout_ms * MI_BASE_THREADX_TICKS_PER_SECOND + 999u) / 1000u);
}
#endif

#if MI_BASE_OS_TXW82X
static int32 txw82x_timeout_to_ms(uint32_t timeout_ms)
{
    return timeout_ms == 0 ? osWaitForever : (int32)timeout_ms;
}
#endif

mi_Semaphore* mi_CreateSemaphore(int initial_count, int max_count)
{
    mi_Semaphore* semaphore;

    if (initial_count < 0 || max_count <= 0 || initial_count > max_count) {
        return NULL;
    }

    semaphore = (mi_Semaphore*)mi_mem_malloc((int)sizeof(mi_Semaphore));
    if (semaphore == NULL) {
        return NULL;
    }

    semaphore->max_count = max_count;

#if MI_BASE_OS_WINDOWS
    semaphore->sem = CreateSemaphoreA(NULL, initial_count, max_count, NULL);
    if (semaphore->sem == NULL) {
        mi_mem_free(semaphore);
        return NULL;
    }
#elif MI_BASE_OS_POSIX
    semaphore->count = initial_count;
    if (pthread_mutex_init(&semaphore->mutex, NULL) != 0) {
        mi_mem_free(semaphore);
        return NULL;
    }
    if (pthread_cond_init(&semaphore->cond, NULL) != 0) {
        pthread_mutex_destroy(&semaphore->mutex);
        mi_mem_free(semaphore);
        return NULL;
    }
#elif MI_BASE_OS_FREERTOS
    semaphore->sem = xSemaphoreCreateCounting((UBaseType_t)max_count, (UBaseType_t)initial_count);
    if (semaphore->sem == NULL) {
        mi_mem_free(semaphore);
        return NULL;
    }
#elif MI_BASE_OS_VXWORKS
    semaphore->sem = semCCreate(SEM_Q_PRIORITY, initial_count);
    if (semaphore->sem == SEM_ID_NULL) {
        mi_mem_free(semaphore);
        return NULL;
    }
#elif MI_BASE_OS_THREADX
    if (tx_semaphore_create(&semaphore->sem, (CHAR*)"mi_sem", (ULONG)initial_count) != TX_SUCCESS) {
        mi_mem_free(semaphore);
        return NULL;
    }
#elif MI_BASE_OS_TXW82X
    if (os_sema_init(&semaphore->sem, initial_count) != RET_OK) {
        mi_mem_free(semaphore);
        return NULL;
    }
#else
    mi_mem_free(semaphore);
    return NULL;
#endif

    return semaphore;
}

void mi_DelSemaphore(mi_Semaphore* semaphore)
{
    if (semaphore == NULL) {
        return;
    }

#if MI_BASE_OS_WINDOWS
    CloseHandle(semaphore->sem);
#elif MI_BASE_OS_POSIX
    pthread_cond_destroy(&semaphore->cond);
    pthread_mutex_destroy(&semaphore->mutex);
#elif MI_BASE_OS_FREERTOS
    vSemaphoreDelete(semaphore->sem);
#elif MI_BASE_OS_VXWORKS
    semDelete(semaphore->sem);
#elif MI_BASE_OS_THREADX
    tx_semaphore_delete(&semaphore->sem);
#elif MI_BASE_OS_TXW82X
    os_sema_del(&semaphore->sem);
#endif

    mi_mem_free(semaphore);
}

int mi_SemaphoreWait(mi_Semaphore* semaphore, uint32_t timeout_ms)
{
    if (semaphore == NULL) {
        return MI_SEMAPHORE_ERR_PARAM;
    }

#if MI_BASE_OS_WINDOWS
    {
        DWORD r = WaitForSingleObject(semaphore->sem, timeout_ms == 0 ? INFINITE : timeout_ms);
        if (r == WAIT_OBJECT_0) {
            return MI_SEMAPHORE_OK;
        }
        if (r == WAIT_TIMEOUT) {
            return MI_SEMAPHORE_ERR_TIMEOUT;
        }
        return MI_SEMAPHORE_ERR_SYSTEM;
    }
#elif MI_BASE_OS_POSIX
    {
        int r = 0;
        pthread_mutex_lock(&semaphore->mutex);
        if (timeout_ms == 0) {
            while (semaphore->count <= 0) {
                r = pthread_cond_wait(&semaphore->cond, &semaphore->mutex);
                if (r != 0) {
                    break;
                }
            }
        } else {
            struct timespec ts;
            make_abs_timespec(&ts, timeout_ms);
            while (semaphore->count <= 0) {
                r = pthread_cond_timedwait(&semaphore->cond, &semaphore->mutex, &ts);
                if (r != 0) {
                    break;
                }
            }
        }
        if (r == 0 && semaphore->count > 0) {
            semaphore->count--;
            pthread_mutex_unlock(&semaphore->mutex);
            return MI_SEMAPHORE_OK;
        }
        pthread_mutex_unlock(&semaphore->mutex);
        return r == ETIMEDOUT ? MI_SEMAPHORE_ERR_TIMEOUT : MI_SEMAPHORE_ERR_SYSTEM;
    }
#elif MI_BASE_OS_FREERTOS
    return xSemaphoreTake(semaphore->sem, timeout_to_ticks(timeout_ms)) == pdTRUE ? MI_SEMAPHORE_OK : MI_SEMAPHORE_ERR_TIMEOUT;
#elif MI_BASE_OS_VXWORKS
    return semTake(semaphore->sem, timeout_to_ticks(timeout_ms)) == OK ? MI_SEMAPHORE_OK : MI_SEMAPHORE_ERR_TIMEOUT;
#elif MI_BASE_OS_THREADX
    {
        UINT r = tx_semaphore_get(&semaphore->sem, timeout_to_ticks(timeout_ms));
        if (r == TX_SUCCESS) {
            return MI_SEMAPHORE_OK;
        }
        return r == TX_NO_INSTANCE ? MI_SEMAPHORE_ERR_TIMEOUT : MI_SEMAPHORE_ERR_SYSTEM;
    }
#elif MI_BASE_OS_TXW82X
    return os_sema_down(&semaphore->sem, txw82x_timeout_to_ms(timeout_ms)) == RET_OK ?
        MI_SEMAPHORE_OK : MI_SEMAPHORE_ERR_TIMEOUT;
#else
    return MI_SEMAPHORE_ERR_SYSTEM;
#endif
}

int mi_SemaphorePost(mi_Semaphore* semaphore, int count)
{
    if (semaphore == NULL || count <= 0) {
        return MI_SEMAPHORE_ERR_PARAM;
    }

#if MI_BASE_OS_WINDOWS
    return ReleaseSemaphore(semaphore->sem, count, NULL) ? MI_SEMAPHORE_OK : MI_SEMAPHORE_ERR_SYSTEM;
#elif MI_BASE_OS_POSIX
    pthread_mutex_lock(&semaphore->mutex);
    {
        int i;
        for (i = 0; i < count; ++i) {
            if (semaphore->count < semaphore->max_count) {
                semaphore->count++;
                pthread_cond_signal(&semaphore->cond);
            }
        }
    }
    pthread_mutex_unlock(&semaphore->mutex);
    return MI_SEMAPHORE_OK;
#elif MI_BASE_OS_FREERTOS
    {
        int i;
        for (i = 0; i < count; ++i) {
            if (xSemaphoreGive(semaphore->sem) != pdTRUE) {
                return MI_SEMAPHORE_ERR_SYSTEM;
            }
        }
    }
    return MI_SEMAPHORE_OK;
#elif MI_BASE_OS_VXWORKS
    {
        int i;
        for (i = 0; i < count; ++i) {
            if (semGive(semaphore->sem) != OK) {
                return MI_SEMAPHORE_ERR_SYSTEM;
            }
        }
    }
    return MI_SEMAPHORE_OK;
#elif MI_BASE_OS_THREADX
    {
        int i;
        for (i = 0; i < count; ++i) {
            if (tx_semaphore_put(&semaphore->sem) != TX_SUCCESS) {
                return MI_SEMAPHORE_ERR_SYSTEM;
            }
        }
    }
    return MI_SEMAPHORE_OK;
#elif MI_BASE_OS_TXW82X
    {
        int i;
        for (i = 0; i < count; ++i) {
            if (os_sema_count(&semaphore->sem) >= semaphore->max_count) {
                break;
            }
            if (os_sema_up(&semaphore->sem) != RET_OK) {
                return MI_SEMAPHORE_ERR_SYSTEM;
            }
        }
    }
    return MI_SEMAPHORE_OK;
#else
    return MI_SEMAPHORE_ERR_SYSTEM;
#endif
}
