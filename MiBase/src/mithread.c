#include "mithread.h"

#include <string.h>

#define DEFAULT_THREAD_PRIORITY    0
#define DEFAULT_THREAD_STACK_SIZE  4096u

typedef struct thread_start_ctx {
    mi_thread_func_t func;
    void* arg;
    mi_thread_t* handle;
} thread_start_ctx;

static const char* platform_names[] = {
    "Unknown",
    "Windows",
    "POSIX",
    "FreeRTOS",
    "VxWorks",
    "ThreadX",
    "TXW82x"
};

static void init_handle(mi_thread_t* handle, const thread_config_t* config)
{
    memset(handle, 0, sizeof(*handle));
    handle->priority = config ? config->priority : DEFAULT_THREAD_PRIORITY;
    handle->stack_size = (config && config->stack_size) ? config->stack_size : DEFAULT_THREAD_STACK_SIZE;
    handle->detached = config ? (config->detached != 0) : 0;
    handle->on_complete = config ? config->on_complete : NULL;
    handle->complete_user_data = config ? config->complete_user_data : NULL;
    if (config && config->name) {
        strncpy(handle->name, config->name, sizeof(handle->name) - 1);
        handle->name[sizeof(handle->name) - 1] = '\0';
    }
}

static thread_start_ctx* create_start_ctx(mi_thread_t* handle, mi_thread_func_t func, void* arg)
{
    thread_start_ctx* ctx = (thread_start_ctx*)mi_mem_malloc((int)sizeof(thread_start_ctx));
    if (ctx == NULL) {
        return NULL;
    }
    ctx->func = func;
    ctx->arg = arg;
    ctx->handle = handle;
    return ctx;
}

static void complete_thread(thread_start_ctx* ctx, thread_return_t result)
{
    mi_thread_complete_fn on_complete = NULL;
    void* complete_user_data = NULL;

    if (ctx->handle != NULL) {
        on_complete = ctx->handle->on_complete;
        complete_user_data = ctx->handle->complete_user_data;
        ctx->handle->result = result;
        ctx->handle->finished = 1;
        ctx->handle->running = 0;
    }
    if (on_complete != NULL) {
        on_complete(complete_user_data, result);
    }
    mi_mem_free(ctx);
}

#if MI_BASE_OS_WINDOWS
static DWORD WINAPI thread_entry(LPVOID param)
{
    thread_start_ctx* ctx = (thread_start_ctx*)param;
    thread_return_t result = NULL;

    if (ctx != NULL && ctx->func != NULL) {
        result = ctx->func(ctx->arg);
        complete_thread(ctx, result);
    }

    return (DWORD)(uintptr_t)result;
}

static void apply_priority(mi_thread_t* handle)
{
    int win_priority = THREAD_PRIORITY_NORMAL;

    if (handle->priority > 90) {
        win_priority = THREAD_PRIORITY_TIME_CRITICAL;
    } else if (handle->priority > 50) {
        win_priority = THREAD_PRIORITY_HIGHEST;
    } else if (handle->priority > 0) {
        win_priority = THREAD_PRIORITY_ABOVE_NORMAL;
    } else if (handle->priority < -90) {
        win_priority = THREAD_PRIORITY_IDLE;
    } else if (handle->priority < -50) {
        win_priority = THREAD_PRIORITY_LOWEST;
    } else if (handle->priority < 0) {
        win_priority = THREAD_PRIORITY_BELOW_NORMAL;
    }

    SetThreadPriority(handle->thread, win_priority);
}

mi_thread_err_t mi_thread_create(mi_thread_t* handle, mi_thread_func_t func, void* arg, const thread_config_t* config)
{
    thread_start_ctx* ctx;

    if (handle == NULL || func == NULL) {
        return MI_THREAD_ERR_INVALID_PARAM;
    }

    init_handle(handle, config);
    ctx = create_start_ctx(handle, func, arg);
    if (ctx == NULL) {
        return MI_THREAD_ERR_NO_MEMORY;
    }

    handle->running = 1;
    handle->thread = CreateThread(NULL, (SIZE_T)handle->stack_size, thread_entry, ctx, 0, NULL);
    if (handle->thread == NULL) {
        handle->running = 0;
        mi_mem_free(ctx);
        return MI_THREAD_ERR_CREATE_FAILED;
    }

    apply_priority(handle);

    if (handle->detached) {
        CloseHandle(handle->thread);
        handle->thread = NULL;
    }
    return MI_THREAD_OK;
}

mi_thread_err_t mi_thread_join(mi_thread_t* handle, thread_return_t* ret, uint32_t timeout_ms)
{
    DWORD wait_result;

    if (handle == NULL || handle->detached || handle->thread == NULL) {
        return MI_THREAD_ERR_INVALID_PARAM;
    }

    wait_result = WaitForSingleObject(handle->thread, timeout_ms == 0 ? INFINITE : timeout_ms);
    if (wait_result == WAIT_TIMEOUT) {
        return MI_THREAD_ERR_TIMEOUT;
    }
    if (wait_result != WAIT_OBJECT_0) {
        return MI_THREAD_ERR_JOIN_FAILED;
    }

    if (ret != NULL) {
        *ret = handle->result;
    }
    CloseHandle(handle->thread);
    handle->thread = NULL;
    handle->running = 0;
    handle->finished = 1;
    return MI_THREAD_OK;
}

mi_thread_err_t mi_thread_cancel(mi_thread_t* handle)
{
    if (handle == NULL || handle->detached || handle->thread == NULL) {
        return MI_THREAD_ERR_INVALID_PARAM;
    }

    if (!TerminateThread(handle->thread, 0)) {
        return MI_THREAD_ERR_CANCEL_FAILED;
    }

    CloseHandle(handle->thread);
    handle->thread = NULL;
    handle->running = 0;
    handle->finished = 1;
    handle->result = NULL;
    return MI_THREAD_OK;
}

int mi_thread_is_current(const mi_thread_t* handle)
{
    if (handle == NULL || handle->thread == NULL) {
        return 0;
    }
    return GetThreadId(handle->thread) == GetCurrentThreadId() ? 1 : 0;
}

uint64_t mi_thread_get_current_id(void)
{
    return (uint64_t)GetCurrentThreadId();
}

void mi_thread_sleep(uint32_t milliseconds)
{
    Sleep(milliseconds);
}

#elif MI_BASE_OS_POSIX
#include <limits.h>
#include <time.h>

static void* thread_entry(void* param)
{
    thread_start_ctx* ctx = (thread_start_ctx*)param;
    thread_return_t result = NULL;

    if (ctx != NULL && ctx->func != NULL) {
        result = ctx->func(ctx->arg);
        complete_thread(ctx, result);
    }
    return result;
}

mi_thread_err_t mi_thread_create(mi_thread_t* handle, mi_thread_func_t func, void* arg, const thread_config_t* config)
{
    pthread_attr_t attr;
    thread_start_ctx* ctx;
    int ret;

    if (handle == NULL || func == NULL) {
        return MI_THREAD_ERR_INVALID_PARAM;
    }

    init_handle(handle, config);
    ctx = create_start_ctx(handle, func, arg);
    if (ctx == NULL) {
        return MI_THREAD_ERR_NO_MEMORY;
    }

    pthread_attr_init(&attr);
#if defined(PTHREAD_STACK_MIN)
    if (handle->stack_size > 0 && handle->stack_size < (uint32_t)PTHREAD_STACK_MIN) {
        handle->stack_size = (uint32_t)PTHREAD_STACK_MIN;
    }
#endif
    if (handle->stack_size > 0) {
        pthread_attr_setstacksize(&attr, (size_t)handle->stack_size);
    }
    if (handle->detached) {
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    }

    handle->running = 1;
    ret = pthread_create(&handle->thread, &attr, thread_entry, ctx);
    pthread_attr_destroy(&attr);
    if (ret != 0) {
        handle->running = 0;
        mi_mem_free(ctx);
        return MI_THREAD_ERR_CREATE_FAILED;
    }

    return MI_THREAD_OK;
}

mi_thread_err_t mi_thread_join(mi_thread_t* handle, thread_return_t* ret, uint32_t timeout_ms)
{
    void* result = NULL;
    int r;

    if (handle == NULL || handle->detached) {
        return MI_THREAD_ERR_INVALID_PARAM;
    }

    if (timeout_ms != 0) {
        uint32_t start = mi_getTick32();
        while (!handle->finished) {
            if ((uint32_t)(mi_getTick32() - start) >= timeout_ms) {
                return MI_THREAD_ERR_TIMEOUT;
            }
            mi_thread_sleep(10);
        }
    }

    r = pthread_join(handle->thread, &result);
    if (r != 0) {
        return MI_THREAD_ERR_JOIN_FAILED;
    }

    if (ret != NULL) {
        *ret = result;
    }
    handle->running = 0;
    handle->finished = 1;
    handle->result = result;
    return MI_THREAD_OK;
}

mi_thread_err_t mi_thread_cancel(mi_thread_t* handle)
{
    if (handle == NULL || handle->detached) {
        return MI_THREAD_ERR_INVALID_PARAM;
    }

    if (pthread_cancel(handle->thread) != 0) {
        return MI_THREAD_ERR_CANCEL_FAILED;
    }
    pthread_join(handle->thread, NULL);
    handle->running = 0;
    handle->finished = 1;
    handle->result = NULL;
    return MI_THREAD_OK;
}

int mi_thread_is_current(const mi_thread_t* handle)
{
    if (handle == NULL) {
        return 0;
    }
    return pthread_equal(handle->thread, pthread_self()) != 0 ? 1 : 0;
}

uint64_t mi_thread_get_current_id(void)
{
    return (uint64_t)(uintptr_t)pthread_self();
}

void mi_thread_sleep(uint32_t milliseconds)
{
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000u;
    ts.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}

#elif MI_BASE_OS_FREERTOS
static void thread_entry(void* param)
{
    thread_start_ctx* ctx = (thread_start_ctx*)param;
    thread_return_t result = NULL;

    if (ctx != NULL && ctx->func != NULL) {
        result = ctx->func(ctx->arg);
        complete_thread(ctx, result);
    }
    vTaskDelete(NULL);
}

mi_thread_err_t mi_thread_create(mi_thread_t* handle, mi_thread_func_t func, void* arg, const thread_config_t* config)
{
    thread_start_ctx* ctx;
    BaseType_t r;
    UBaseType_t priority;
    configSTACK_DEPTH_TYPE stack_depth;

    if (handle == NULL || func == NULL) {
        return MI_THREAD_ERR_INVALID_PARAM;
    }

    init_handle(handle, config);
    ctx = create_start_ctx(handle, func, arg);
    if (ctx == NULL) {
        return MI_THREAD_ERR_NO_MEMORY;
    }

    priority = handle->priority > 0 ? (UBaseType_t)handle->priority : (tskIDLE_PRIORITY + 1);
    stack_depth = (configSTACK_DEPTH_TYPE)(handle->stack_size / sizeof(StackType_t));
    if (stack_depth < configMINIMAL_STACK_SIZE) {
        stack_depth = configMINIMAL_STACK_SIZE;
    }

    handle->running = 1;
    r = xTaskCreate(thread_entry,
                    handle->name[0] ? handle->name : "mi_thread",
                    stack_depth,
                    ctx,
                    priority,
                    &handle->thread);
    if (r != pdPASS) {
        handle->running = 0;
        mi_mem_free(ctx);
        return MI_THREAD_ERR_CREATE_FAILED;
    }

    return MI_THREAD_OK;
}

mi_thread_err_t mi_thread_join(mi_thread_t* handle, thread_return_t* ret, uint32_t timeout_ms)
{
    uint32_t start;

    if (handle == NULL) {
        return MI_THREAD_ERR_INVALID_PARAM;
    }

    start = mi_getTick32();
    while (!handle->finished) {
        if (timeout_ms != 0 && (uint32_t)(mi_getTick32() - start) >= timeout_ms) {
            return MI_THREAD_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (ret != NULL) {
        *ret = handle->result;
    }
    return MI_THREAD_OK;
}

mi_thread_err_t mi_thread_cancel(mi_thread_t* handle)
{
    if (handle == NULL || handle->thread == NULL) {
        return MI_THREAD_ERR_INVALID_PARAM;
    }
    vTaskDelete(handle->thread);
    handle->thread = NULL;
    handle->running = 0;
    handle->finished = 1;
    handle->result = NULL;
    return MI_THREAD_OK;
}

int mi_thread_is_current(const mi_thread_t* handle)
{
    return handle != NULL && handle->thread == xTaskGetCurrentTaskHandle() ? 1 : 0;
}

uint64_t mi_thread_get_current_id(void)
{
    return (uint64_t)(uintptr_t)xTaskGetCurrentTaskHandle();
}

void mi_thread_sleep(uint32_t milliseconds)
{
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

#elif MI_BASE_OS_VXWORKS
#include <sysLib.h>
static int thread_entry(uintptr_t param)
{
    thread_start_ctx* ctx = (thread_start_ctx*)param;
    thread_return_t result = NULL;

    if (ctx != NULL && ctx->func != NULL) {
        result = ctx->func(ctx->arg);
        complete_thread(ctx, result);
    }
    return (int)(intptr_t)result;
}

mi_thread_err_t mi_thread_create(mi_thread_t* handle, mi_thread_func_t func, void* arg, const thread_config_t* config)
{
    thread_start_ctx* ctx;
    int priority;

    if (handle == NULL || func == NULL) {
        return MI_THREAD_ERR_INVALID_PARAM;
    }

    init_handle(handle, config);
    ctx = create_start_ctx(handle, func, arg);
    if (ctx == NULL) {
        return MI_THREAD_ERR_NO_MEMORY;
    }

    priority = handle->priority != 0 ? handle->priority : 100;
    handle->running = 1;
    handle->thread = taskSpawn(handle->name[0] ? handle->name : "mi_thread",
                               priority,
                               0,
                               (int)handle->stack_size,
                               (FUNCPTR)thread_entry,
                               (intptr_t)ctx,
                               0, 0, 0, 0, 0, 0, 0, 0, 0);
    if (handle->thread == ERROR) {
        handle->running = 0;
        mi_mem_free(ctx);
        return MI_THREAD_ERR_CREATE_FAILED;
    }

    return MI_THREAD_OK;
}

mi_thread_err_t mi_thread_join(mi_thread_t* handle, thread_return_t* ret, uint32_t timeout_ms)
{
    uint32_t start;

    if (handle == NULL) {
        return MI_THREAD_ERR_INVALID_PARAM;
    }

    start = mi_getTick32();
    while (!handle->finished) {
        if (timeout_ms != 0 && (uint32_t)(mi_getTick32() - start) >= timeout_ms) {
            return MI_THREAD_ERR_TIMEOUT;
        }
        mi_thread_sleep(10);
    }

    if (ret != NULL) {
        *ret = handle->result;
    }
    return MI_THREAD_OK;
}

mi_thread_err_t mi_thread_cancel(mi_thread_t* handle)
{
    if (handle == NULL || handle->thread == ERROR) {
        return MI_THREAD_ERR_INVALID_PARAM;
    }
    if (taskDelete(handle->thread) != OK) {
        return MI_THREAD_ERR_CANCEL_FAILED;
    }
    handle->thread = ERROR;
    handle->running = 0;
    handle->finished = 1;
    handle->result = NULL;
    return MI_THREAD_OK;
}

int mi_thread_is_current(const mi_thread_t* handle)
{
    return handle != NULL && handle->thread != ERROR && handle->thread == taskIdSelf() ? 1 : 0;
}

uint64_t mi_thread_get_current_id(void)
{
    return (uint64_t)(uintptr_t)taskIdSelf();
}

void mi_thread_sleep(uint32_t milliseconds)
{
    int rate = sysClkRateGet();
    int ticks = rate > 0 ? (int)(((uint64_t)milliseconds * (uint32_t)rate + 999u) / 1000u) : 1;
    taskDelay(ticks > 0 ? ticks : 1);
}

#elif MI_BASE_OS_THREADX
static void thread_entry(ULONG param)
{
    thread_start_ctx* ctx = (thread_start_ctx*)param;
    thread_return_t result = NULL;

    if (ctx != NULL && ctx->func != NULL) {
        result = ctx->func(ctx->arg);
        complete_thread(ctx, result);
    }
}

mi_thread_err_t mi_thread_create(mi_thread_t* handle, mi_thread_func_t func, void* arg, const thread_config_t* config)
{
    thread_start_ctx* ctx;
    UINT priority;

    if (handle == NULL || func == NULL) {
        return MI_THREAD_ERR_INVALID_PARAM;
    }

    init_handle(handle, config);
    handle->thread = (TX_THREAD*)mi_mem_malloc((int)sizeof(TX_THREAD));
    handle->stack = mi_mem_malloc((int)handle->stack_size);
    ctx = create_start_ctx(handle, func, arg);
    if (handle->thread == NULL || handle->stack == NULL || ctx == NULL) {
        mi_mem_free(ctx);
        mi_mem_free(handle->stack);
        mi_mem_free(handle->thread);
        return MI_THREAD_ERR_NO_MEMORY;
    }

    priority = handle->priority > 0 ? (UINT)handle->priority : 10u;
    handle->running = 1;
    if (tx_thread_create(handle->thread,
                         (CHAR*)(handle->name[0] ? handle->name : "mi_thread"),
                         thread_entry,
                         (ULONG)ctx,
                         handle->stack,
                         handle->stack_size,
                         priority,
                         priority,
                         TX_NO_TIME_SLICE,
                         TX_AUTO_START) != TX_SUCCESS) {
        mi_mem_free(ctx);
        mi_mem_free(handle->stack);
        mi_mem_free(handle->thread);
        handle->stack = NULL;
        handle->thread = NULL;
        handle->running = 0;
        return MI_THREAD_ERR_CREATE_FAILED;
    }

    return MI_THREAD_OK;
}

mi_thread_err_t mi_thread_join(mi_thread_t* handle, thread_return_t* ret, uint32_t timeout_ms)
{
    uint32_t start;

    if (handle == NULL) {
        return MI_THREAD_ERR_INVALID_PARAM;
    }

    start = mi_getTick32();
    while (!handle->finished) {
        if (timeout_ms != 0 && (uint32_t)(mi_getTick32() - start) >= timeout_ms) {
            return MI_THREAD_ERR_TIMEOUT;
        }
        mi_thread_sleep(10);
    }

    if (ret != NULL) {
        *ret = handle->result;
    }
    tx_thread_delete(handle->thread);
    mi_mem_free(handle->stack);
    mi_mem_free(handle->thread);
    handle->stack = NULL;
    handle->thread = NULL;
    return MI_THREAD_OK;
}

mi_thread_err_t mi_thread_cancel(mi_thread_t* handle)
{
    if (handle == NULL || handle->thread == NULL) {
        return MI_THREAD_ERR_INVALID_PARAM;
    }
    if (tx_thread_terminate(handle->thread) != TX_SUCCESS) {
        return MI_THREAD_ERR_CANCEL_FAILED;
    }
    tx_thread_delete(handle->thread);
    mi_mem_free(handle->stack);
    mi_mem_free(handle->thread);
    handle->stack = NULL;
    handle->thread = NULL;
    handle->running = 0;
    handle->finished = 1;
    handle->result = NULL;
    return MI_THREAD_OK;
}

int mi_thread_is_current(const mi_thread_t* handle)
{
    return handle != NULL && handle->thread == tx_thread_identify() ? 1 : 0;
}

uint64_t mi_thread_get_current_id(void)
{
    return (uint64_t)(uintptr_t)tx_thread_identify();
}

void mi_thread_sleep(uint32_t milliseconds)
{
#ifndef MI_BASE_THREADX_TICKS_PER_SECOND
#define MI_BASE_THREADX_TICKS_PER_SECOND 100u
#endif
    ULONG ticks = (ULONG)(((uint64_t)milliseconds * MI_BASE_THREADX_TICKS_PER_SECOND + 999u) / 1000u);
    tx_thread_sleep(ticks == 0 ? 1 : ticks);
}

#elif MI_BASE_OS_TXW82X
#include "typesdef.h"
#include "osal/sleep.h"
#include "osal/task.h"

static uint32_t txw82x_priority_from_mi(int priority)
{
    if (priority >= 90) {
        return OS_TASK_PRIORITY_REALTIME;
    }
    if (priority >= 50) {
        return OS_TASK_PRIORITY_HIGH;
    }
    if (priority > 0) {
        return OS_TASK_PRIORITY_ABOVE_NORMAL;
    }
    if (priority <= -50) {
        return OS_TASK_PRIORITY_LOW;
    }
    return OS_TASK_PRIORITY_NORMAL;
}

static void thread_entry(void* param)
{
    thread_start_ctx* ctx = (thread_start_ctx*)param;
    thread_return_t result = NULL;

    if (ctx != NULL && ctx->func != NULL) {
        result = ctx->func(ctx->arg);
        complete_thread(ctx, result);
    }
}

mi_thread_err_t mi_thread_create(mi_thread_t* handle, mi_thread_func_t func, void* arg, const thread_config_t* config)
{
    thread_start_ctx* ctx;
    uint32_t priority;

    if (handle == NULL || func == NULL) {
        return MI_THREAD_ERR_INVALID_PARAM;
    }

    init_handle(handle, config);
    ctx = create_start_ctx(handle, func, arg);
    if (ctx == NULL) {
        return MI_THREAD_ERR_NO_MEMORY;
    }

    priority = txw82x_priority_from_mi(handle->priority);
    handle->running = 1;
    handle->thread = os_task_create(handle->name[0] ? handle->name : "mi_thread",
                                    thread_entry,
                                    ctx,
                                    priority,
                                    0,
                                    NULL,
                                    handle->stack_size);
    if (handle->thread == NULL) {
        handle->running = 0;
        mi_mem_free(ctx);
        return MI_THREAD_ERR_CREATE_FAILED;
    }
    return MI_THREAD_OK;
}

mi_thread_err_t mi_thread_join(mi_thread_t* handle, thread_return_t* ret, uint32_t timeout_ms)
{
    uint32_t start;

    if (handle == NULL) {
        return MI_THREAD_ERR_INVALID_PARAM;
    }

    start = mi_getTick32();
    while (!handle->finished) {
        if (timeout_ms != 0 && (uint32_t)(mi_getTick32() - start) >= timeout_ms) {
            return MI_THREAD_ERR_TIMEOUT;
        }
        os_sleep_ms(10);
    }

    if (ret != NULL) {
        *ret = handle->result;
    }
    handle->thread = NULL;
    return MI_THREAD_OK;
}

mi_thread_err_t mi_thread_cancel(mi_thread_t* handle)
{
    if (handle == NULL || handle->thread == NULL) {
        return MI_THREAD_ERR_INVALID_PARAM;
    }
    if (os_task_destroy(handle->thread) != RET_OK) {
        return MI_THREAD_ERR_CANCEL_FAILED;
    }
    handle->thread = NULL;
    handle->running = 0;
    handle->finished = 1;
    handle->result = NULL;
    return MI_THREAD_OK;
}

int mi_thread_is_current(const mi_thread_t* handle)
{
    return handle != NULL && handle->thread == os_task_current() ? 1 : 0;
}

uint64_t mi_thread_get_current_id(void)
{
    return (uint64_t)(uintptr_t)os_task_current();
}

void mi_thread_sleep(uint32_t milliseconds)
{
    os_sleep_ms((int32)milliseconds);
}

#else
mi_thread_err_t mi_thread_create(mi_thread_t* handle, mi_thread_func_t func, void* arg, const thread_config_t* config)
{
    (void)handle;
    (void)func;
    (void)arg;
    (void)config;
    return MI_THREAD_ERR_PLATFORM;
}

mi_thread_err_t mi_thread_join(mi_thread_t* handle, thread_return_t* ret, uint32_t timeout_ms)
{
    (void)handle;
    (void)ret;
    (void)timeout_ms;
    return MI_THREAD_ERR_PLATFORM;
}

mi_thread_err_t mi_thread_cancel(mi_thread_t* handle)
{
    (void)handle;
    return MI_THREAD_ERR_PLATFORM;
}

int mi_thread_is_current(const mi_thread_t* handle)
{
    (void)handle;
    return 0;
}

uint64_t mi_thread_get_current_id(void)
{
    return 0;
}

void mi_thread_sleep(uint32_t milliseconds)
{
    (void)milliseconds;
}
#endif

thread_state_t mi_thread_get_state(const mi_thread_t* handle)
{
    if (handle == NULL) {
        return THREAD_STATE_IDLE;
    }
    if (handle->finished) {
        return THREAD_STATE_FINISHED;
    }
    if (handle->running) {
        return THREAD_STATE_RUNNING;
    }
    return THREAD_STATE_IDLE;
}

int mi_thread_get_platform(void)
{
    return CURRENT_PLATFORM;
}

const char* mi_thread_get_platform_name(void)
{
    if (CURRENT_PLATFORM >= 0 && CURRENT_PLATFORM < (int)(sizeof(platform_names) / sizeof(platform_names[0]))) {
        return platform_names[CURRENT_PLATFORM];
    }
    return "Unknown";
}

const char* mi_thread_strerror(mi_thread_err_t error)
{
    switch (error) {
    case MI_THREAD_OK:
        return "Success";
    case MI_THREAD_ERR_INVALID_PARAM:
        return "Invalid parameter";
    case MI_THREAD_ERR_NO_MEMORY:
        return "No memory available";
    case MI_THREAD_ERR_CREATE_FAILED:
        return "Thread creation failed";
    case MI_THREAD_ERR_JOIN_FAILED:
        return "Thread join failed";
    case MI_THREAD_ERR_CANCEL_FAILED:
        return "Thread cancel failed";
    case MI_THREAD_ERR_TIMEOUT:
        return "Operation timeout";
    case MI_THREAD_ERR_NOT_FOUND:
        return "Thread not found";
    case MI_THREAD_ERR_PLATFORM:
        return "Platform not supported";
    case MI_THREAD_ERR_SYSTEM:
        return "System error";
    default:
        return "Unknown error";
    }
}
