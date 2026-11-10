#include "mimutex.h"

#include <stddef.h>

#if MI_BASE_OS_WINDOWS
#include <Windows.h>
#endif

#if MI_BASE_OS_POSIX
#include <pthread.h>
#endif

#if MI_BASE_OS_FREERTOS
#include "FreeRTOS.h"
#include "semphr.h"
#endif

#if MI_BASE_OS_VXWORKS
#include <vxWorks.h>
#include <semLib.h>
#endif

#if MI_BASE_OS_THREADX
#include "tx_api.h"
#endif

#if MI_BASE_OS_TXW82X
#include "typesdef.h"
#include "osal/mutex.h"
#endif

typedef struct mi_MutexImpl {
    mi_Mutex api;
    int reentrant;
#if MI_BASE_OS_WINDOWS
    CRITICAL_SECTION critical_section;
#elif MI_BASE_OS_POSIX
    pthread_mutex_t mutex;
#elif MI_BASE_OS_FREERTOS
    SemaphoreHandle_t mutex;
#elif MI_BASE_OS_VXWORKS
    SEM_ID mutex;
#elif MI_BASE_OS_THREADX
    TX_MUTEX mutex;
#elif MI_BASE_OS_TXW82X
    os_mutex_t mutex;
#endif
} mi_MutexImpl;

static void mi_lock_impl(struct mi_Mutex* mutex)
{
    mi_MutexImpl* lock = (mi_MutexImpl*)mutex;
    if (lock == NULL) {
        return;
    }

#if MI_BASE_OS_WINDOWS
    EnterCriticalSection(&lock->critical_section);
#elif MI_BASE_OS_POSIX
    pthread_mutex_lock(&lock->mutex);
#elif MI_BASE_OS_FREERTOS
#if (configUSE_RECURSIVE_MUTEXES == 1)
    if (lock->reentrant) {
        xSemaphoreTakeRecursive(lock->mutex, portMAX_DELAY);
    } else {
        xSemaphoreTake(lock->mutex, portMAX_DELAY);
    }
#else
    xSemaphoreTake(lock->mutex, portMAX_DELAY);
#endif
#elif MI_BASE_OS_VXWORKS
    semTake(lock->mutex, WAIT_FOREVER);
#elif MI_BASE_OS_THREADX
    tx_mutex_get(&lock->mutex, TX_WAIT_FOREVER);
#elif MI_BASE_OS_TXW82X
    os_mutex_lock(&lock->mutex, osWaitForever);
#else
    (void)lock;
#endif
}

static void mi_unlock_impl(struct mi_Mutex* mutex)
{
    mi_MutexImpl* lock = (mi_MutexImpl*)mutex;
    if (lock == NULL) {
        return;
    }

#if MI_BASE_OS_WINDOWS
    LeaveCriticalSection(&lock->critical_section);
#elif MI_BASE_OS_POSIX
    pthread_mutex_unlock(&lock->mutex);
#elif MI_BASE_OS_FREERTOS
#if (configUSE_RECURSIVE_MUTEXES == 1)
    if (lock->reentrant) {
        xSemaphoreGiveRecursive(lock->mutex);
    } else {
        xSemaphoreGive(lock->mutex);
    }
#else
    xSemaphoreGive(lock->mutex);
#endif
#elif MI_BASE_OS_VXWORKS
    semGive(lock->mutex);
#elif MI_BASE_OS_THREADX
    tx_mutex_put(&lock->mutex);
#elif MI_BASE_OS_TXW82X
    os_mutex_unlock(&lock->mutex);
#else
    (void)lock;
#endif
}

struct mi_Mutex* mi_CreateMutex(int reentrant)
{
    mi_MutexImpl* lock = (mi_MutexImpl*)mi_mem_malloc((int)sizeof(mi_MutexImpl));
    if (lock == NULL) {
        return NULL;
    }

    lock->api.Lock = mi_lock_impl;
    lock->api.UnLock = mi_unlock_impl;
    lock->reentrant = reentrant ? 1 : 0;

#if MI_BASE_OS_WINDOWS
    InitializeCriticalSection(&lock->critical_section);
#elif MI_BASE_OS_POSIX
    {
        pthread_mutexattr_t attr;
        if (pthread_mutexattr_init(&attr) != 0) {
            mi_mem_free(lock);
            return NULL;
        }
        pthread_mutexattr_settype(&attr, lock->reentrant ? PTHREAD_MUTEX_RECURSIVE : PTHREAD_MUTEX_NORMAL);
        if (pthread_mutex_init(&lock->mutex, &attr) != 0) {
            pthread_mutexattr_destroy(&attr);
            mi_mem_free(lock);
            return NULL;
        }
        pthread_mutexattr_destroy(&attr);
    }
#elif MI_BASE_OS_FREERTOS
#if (configUSE_RECURSIVE_MUTEXES == 1)
    lock->mutex = lock->reentrant ? xSemaphoreCreateRecursiveMutex() : xSemaphoreCreateMutex();
#else
    if (lock->reentrant) {
        mi_mem_free(lock);
        return NULL;
    }
    lock->mutex = xSemaphoreCreateMutex();
#endif
    if (lock->mutex == NULL) {
        mi_mem_free(lock);
        return NULL;
    }
#elif MI_BASE_OS_VXWORKS
    lock->mutex = semMCreate(SEM_Q_PRIORITY | SEM_INVERSION_SAFE);
    if (lock->mutex == SEM_ID_NULL) {
        mi_mem_free(lock);
        return NULL;
    }
#elif MI_BASE_OS_THREADX
    if (tx_mutex_create(&lock->mutex, (CHAR*)"mi_mutex", TX_INHERIT) != TX_SUCCESS) {
        mi_mem_free(lock);
        return NULL;
    }
#elif MI_BASE_OS_TXW82X
    if (os_mutex_init(&lock->mutex) != RET_OK) {
        mi_mem_free(lock);
        return NULL;
    }
#else
    mi_mem_free(lock);
    return NULL;
#endif

    return &lock->api;
}

void mi_DelMutex(struct mi_Mutex* mutex)
{
    mi_MutexImpl* lock = (mi_MutexImpl*)mutex;
    if (lock == NULL) {
        return;
    }

#if MI_BASE_OS_WINDOWS
    DeleteCriticalSection(&lock->critical_section);
#elif MI_BASE_OS_POSIX
    pthread_mutex_destroy(&lock->mutex);
#elif MI_BASE_OS_FREERTOS
    vSemaphoreDelete(lock->mutex);
#elif MI_BASE_OS_VXWORKS
    semDelete(lock->mutex);
#elif MI_BASE_OS_THREADX
    tx_mutex_delete(&lock->mutex);
#elif MI_BASE_OS_TXW82X
    os_mutex_del(&lock->mutex);
#endif

    mi_mem_free(lock);
}
