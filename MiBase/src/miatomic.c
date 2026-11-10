#include "miatomic.h"

#include <stddef.h>

#if MI_BASE_OS_WINDOWS
#include <Windows.h>
#endif

#if MI_BASE_OS_FREERTOS
#include "FreeRTOS.h"
#include "task.h"
#endif

#if MI_BASE_OS_THREADX
#include "tx_api.h"
#endif

#if !MI_BASE_OS_FREERTOS && !MI_BASE_OS_THREADX && !MI_BASE_OS_TXW82X && \
    (defined(__clang__) || (defined(__GNUC__) && \
    ((__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7))))
#define MI_ATOMIC_HAS_GNU_BUILTINS 1
#else
#define MI_ATOMIC_HAS_GNU_BUILTINS 0
#endif

#if !MI_BASE_OS_FREERTOS && !MI_BASE_OS_THREADX && !MI_BASE_OS_TXW82X && defined(__GNUC__) && !MI_ATOMIC_HAS_GNU_BUILTINS
#define MI_ATOMIC_HAS_GNU_SYNC 1
#else
#define MI_ATOMIC_HAS_GNU_SYNC 0
#endif

#if defined(_MSC_VER) && !MI_BASE_OS_FREERTOS && !MI_BASE_OS_THREADX && !MI_BASE_OS_TXW82X
#define MI_ATOMIC_HAS_MSVC_INTRINSICS 1
#else
#define MI_ATOMIC_HAS_MSVC_INTRINSICS 0
#endif

#if MI_ATOMIC_HAS_GNU_BUILTINS || MI_ATOMIC_HAS_GNU_SYNC || MI_ATOMIC_HAS_MSVC_INTRINSICS
#define MI_ATOMIC_HAS_NATIVE 1
#else
#define MI_ATOMIC_HAS_NATIVE 0
#endif

#if !MI_ATOMIC_HAS_NATIVE && MI_BASE_OS_POSIX
#include <pthread.h>
#endif

#if !MI_ATOMIC_HAS_NATIVE && MI_BASE_OS_VXWORKS
#include <intLib.h>
#endif

#if !MI_ATOMIC_HAS_NATIVE && MI_BASE_OS_TXW82X
#include "typesdef.h"
#include "osal/irq.h"
#endif

#if !MI_ATOMIC_HAS_NATIVE
typedef struct mi_atomic_critical_state {
#if MI_BASE_OS_THREADX
    UINT old_posture;
#elif MI_BASE_OS_VXWORKS
    int int_key;
#elif MI_BASE_OS_TXW82X
    uint32 int_key;
#else
    int unused;
#endif
} mi_atomic_critical_state_t;

#if MI_BASE_OS_POSIX
static pthread_mutex_t g_mi_atomic_posix_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

static void mi_atomic_enter_critical(mi_atomic_critical_state_t* state)
{
    if (state == NULL) {
        return;
    }
#if MI_BASE_OS_FREERTOS
    taskENTER_CRITICAL();
#elif MI_BASE_OS_THREADX
    state->old_posture = tx_interrupt_control(TX_INT_DISABLE);
#elif MI_BASE_OS_VXWORKS
    state->int_key = intLock();
#elif MI_BASE_OS_TXW82X
    state->int_key = disable_irq();
#elif MI_BASE_OS_POSIX
    pthread_mutex_lock(&g_mi_atomic_posix_lock);
#else
    (void)state;
#error "MiBase atomic needs native compiler atomics or a supported platform critical section."
#endif
}

static void mi_atomic_leave_critical(mi_atomic_critical_state_t* state)
{
    if (state == NULL) {
        return;
    }
#if MI_BASE_OS_FREERTOS
    taskEXIT_CRITICAL();
#elif MI_BASE_OS_THREADX
    (void)tx_interrupt_control(state->old_posture);
#elif MI_BASE_OS_VXWORKS
    intUnlock(state->int_key);
#elif MI_BASE_OS_TXW82X
    enable_irq(state->int_key);
#elif MI_BASE_OS_POSIX
    pthread_mutex_unlock(&g_mi_atomic_posix_lock);
#else
    (void)state;
#endif
}
#endif

void mi_atomic32_init(mi_atomic32_t* atomic, int32_t value)
{
    if (atomic != NULL) {
        mi_atomic32_store(atomic, value);
    }
}

int32_t mi_atomic32_load(const mi_atomic32_t* atomic)
{
    if (atomic == NULL) {
        return 0;
    }
#if MI_ATOMIC_HAS_GNU_BUILTINS
    return __atomic_load_n(&atomic->value, __ATOMIC_SEQ_CST);
#elif MI_ATOMIC_HAS_GNU_SYNC
    return __sync_add_and_fetch((volatile int32_t*)&atomic->value, 0);
#elif MI_ATOMIC_HAS_MSVC_INTRINSICS
    return (int32_t)InterlockedCompareExchange((volatile LONG*)&atomic->value, 0, 0);
#else
    {
        mi_atomic_critical_state_t state;
        int32_t value;
        mi_atomic_enter_critical(&state);
        value = atomic->value;
        mi_atomic_leave_critical(&state);
        return value;
    }
#endif
}

void mi_atomic32_store(mi_atomic32_t* atomic, int32_t value)
{
    if (atomic == NULL) {
        return;
    }
#if MI_ATOMIC_HAS_GNU_BUILTINS
    __atomic_store_n(&atomic->value, value, __ATOMIC_SEQ_CST);
#elif MI_ATOMIC_HAS_GNU_SYNC
    (void)__sync_lock_test_and_set((volatile int32_t*)&atomic->value, value);
    __sync_synchronize();
#elif MI_ATOMIC_HAS_MSVC_INTRINSICS
    (void)InterlockedExchange((volatile LONG*)&atomic->value, (LONG)value);
#else
    {
        mi_atomic_critical_state_t state;
        mi_atomic_enter_critical(&state);
        atomic->value = value;
        mi_atomic_leave_critical(&state);
    }
#endif
}

int32_t mi_atomic32_add(mi_atomic32_t* atomic, int32_t value)
{
    if (atomic == NULL) {
        return 0;
    }
#if MI_ATOMIC_HAS_GNU_BUILTINS
    return __atomic_add_fetch(&atomic->value, value, __ATOMIC_SEQ_CST);
#elif MI_ATOMIC_HAS_GNU_SYNC
    return __sync_add_and_fetch((volatile int32_t*)&atomic->value, value);
#elif MI_ATOMIC_HAS_MSVC_INTRINSICS
    return (int32_t)InterlockedAdd((volatile LONG*)&atomic->value, (LONG)value);
#else
    {
        mi_atomic_critical_state_t state;
        int32_t result;
        mi_atomic_enter_critical(&state);
        atomic->value += value;
        result = atomic->value;
        mi_atomic_leave_critical(&state);
        return result;
    }
#endif
}

int32_t mi_atomic32_inc(mi_atomic32_t* atomic)
{
    return mi_atomic32_add(atomic, 1);
}

int32_t mi_atomic32_dec(mi_atomic32_t* atomic)
{
    return mi_atomic32_add(atomic, -1);
}

int32_t mi_atomic32_exchange(mi_atomic32_t* atomic, int32_t value)
{
    if (atomic == NULL) {
        return 0;
    }
#if MI_ATOMIC_HAS_GNU_BUILTINS
    return __atomic_exchange_n(&atomic->value, value, __ATOMIC_SEQ_CST);
#elif MI_ATOMIC_HAS_GNU_SYNC
    return __sync_lock_test_and_set((volatile int32_t*)&atomic->value, value);
#elif MI_ATOMIC_HAS_MSVC_INTRINSICS
    return (int32_t)InterlockedExchange((volatile LONG*)&atomic->value, (LONG)value);
#else
    {
        mi_atomic_critical_state_t state;
        int32_t old_value;
        mi_atomic_enter_critical(&state);
        old_value = atomic->value;
        atomic->value = value;
        mi_atomic_leave_critical(&state);
        return old_value;
    }
#endif
}

int mi_atomic32_compare_exchange(mi_atomic32_t* atomic, int32_t* expected, int32_t desired)
{
    if (atomic == NULL || expected == NULL) {
        return 0;
    }
#if MI_ATOMIC_HAS_GNU_BUILTINS
    return __atomic_compare_exchange_n(&atomic->value,
                                       expected,
                                       desired,
                                       0,
                                       __ATOMIC_SEQ_CST,
                                       __ATOMIC_SEQ_CST) ? 1 : 0;
#elif MI_ATOMIC_HAS_GNU_SYNC
    {
        int32_t old_value = __sync_val_compare_and_swap((volatile int32_t*)&atomic->value,
                                                        *expected,
                                                        desired);
        if (old_value == *expected) {
            return 1;
        }
        *expected = old_value;
        return 0;
    }
#elif MI_ATOMIC_HAS_MSVC_INTRINSICS
    {
        LONG old_value = InterlockedCompareExchange((volatile LONG*)&atomic->value,
                                                    (LONG)desired,
                                                    (LONG)*expected);
        if (old_value == (LONG)*expected) {
            return 1;
        }
        *expected = (int32_t)old_value;
        return 0;
    }
#else
    {
        mi_atomic_critical_state_t state;
        int success = 0;
        mi_atomic_enter_critical(&state);
        if (atomic->value == *expected) {
            atomic->value = desired;
            success = 1;
        } else {
            *expected = atomic->value;
        }
        mi_atomic_leave_critical(&state);
        return success;
    }
#endif
}
