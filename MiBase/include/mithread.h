#ifndef CROSS_PLATFORM_THREAD_H
#define CROSS_PLATFORM_THREAD_H

#include "mibase.h"
#include <stdint.h>

#define PLATFORM_UNKNOWN   MI_BASE_PLATFORM_UNKNOWN
#define PLATFORM_WINDOWS   MI_BASE_PLATFORM_WINDOWS
#define PLATFORM_POSIX     MI_BASE_PLATFORM_POSIX
#define PLATFORM_FREERTOS  MI_BASE_PLATFORM_FREERTOS
#define PLATFORM_VXWORKS   MI_BASE_PLATFORM_VXWORKS
#define PLATFORM_THREADX   MI_BASE_PLATFORM_THREADX
#define PLATFORM_TXW82X    MI_BASE_PLATFORM_TXW82X
#define CURRENT_PLATFORM   MI_BASE_PLATFORM

typedef enum {
    MI_THREAD_OK = 0,
    MI_THREAD_ERR_INVALID_PARAM = -1,
    MI_THREAD_ERR_NO_MEMORY = -2,
    MI_THREAD_ERR_CREATE_FAILED = -3,
    MI_THREAD_ERR_JOIN_FAILED = -4,
    MI_THREAD_ERR_CANCEL_FAILED = -5,
    MI_THREAD_ERR_TIMEOUT = -6,
    MI_THREAD_ERR_NOT_FOUND = -7,
    MI_THREAD_ERR_PLATFORM = -8,
    MI_THREAD_ERR_SYSTEM = -9
} mi_thread_err_t;

typedef void* (*mi_thread_func_t)(void*);

#if MI_BASE_OS_WINDOWS
#include <windows.h>
typedef HANDLE thread_t;
typedef void* thread_return_t;
#elif MI_BASE_OS_POSIX
#include <pthread.h>
typedef pthread_t thread_t;
typedef void* thread_return_t;
#elif MI_BASE_OS_FREERTOS
#include "FreeRTOS.h"
#include "task.h"
typedef TaskHandle_t thread_t;
typedef void* thread_return_t;
#elif MI_BASE_OS_VXWORKS
#include <vxWorks.h>
#include <taskLib.h>
typedef int thread_t;
typedef void* thread_return_t;
#elif MI_BASE_OS_THREADX
#include "tx_api.h"
typedef TX_THREAD* thread_t;
typedef void* thread_return_t;
#elif MI_BASE_OS_TXW82X
typedef void* thread_t;
typedef void* thread_return_t;
#else
typedef void* thread_t;
typedef void* thread_return_t;
#endif

typedef void (*mi_thread_complete_fn)(void* user_data, thread_return_t result);

typedef enum {
    THREAD_STATE_IDLE,
    THREAD_STATE_RUNNING,
    THREAD_STATE_FINISHED,
    THREAD_STATE_CANCELLED
} thread_state_t;

typedef struct {
    thread_t thread;
    volatile int running;
    volatile int finished;
    int detached;
    int priority;
    uint32_t stack_size;
    char name[64];
    thread_return_t result;
    mi_thread_complete_fn on_complete;
    void* complete_user_data;
#if MI_BASE_OS_THREADX
    void* stack;
#endif
} mi_thread_t;

typedef struct {
    const char* name;
    int priority;
    uint32_t stack_size;
    int detached;
    mi_thread_complete_fn on_complete;
    void* complete_user_data;
} thread_config_t;

#ifdef __cplusplus
extern "C" {
#endif

mi_thread_err_t mi_thread_create(mi_thread_t* handle, mi_thread_func_t func, void* arg, const thread_config_t* config);
mi_thread_err_t mi_thread_join(mi_thread_t* handle, thread_return_t* ret, uint32_t timeout_ms);
mi_thread_err_t mi_thread_cancel(mi_thread_t* handle);
int mi_thread_is_current(const mi_thread_t* handle);
uint64_t mi_thread_get_current_id(void);
thread_state_t mi_thread_get_state(const mi_thread_t* handle);
void mi_thread_sleep(uint32_t milliseconds);
int mi_thread_get_platform(void);
const char* mi_thread_get_platform_name(void);
const char* mi_thread_strerror(mi_thread_err_t error);

#ifdef __cplusplus
}
#endif

#endif
