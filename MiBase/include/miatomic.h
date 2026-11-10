#ifndef MI_ATOMIC_H_
#define MI_ATOMIC_H_

#include "mibase.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mi_atomic32 {
    volatile int32_t value;
} mi_atomic32_t;

/*
 * 32-bit atomic helper for Windows/Linux/RTOS builds.
 * add/inc/dec return the new value. compare_exchange returns 1 on success;
 * on failure it stores the current value back to *expected.
 */
void mi_atomic32_init(mi_atomic32_t* atomic, int32_t value);
int32_t mi_atomic32_load(const mi_atomic32_t* atomic);
void mi_atomic32_store(mi_atomic32_t* atomic, int32_t value);
int32_t mi_atomic32_add(mi_atomic32_t* atomic, int32_t value);
int32_t mi_atomic32_inc(mi_atomic32_t* atomic);
int32_t mi_atomic32_dec(mi_atomic32_t* atomic);
int32_t mi_atomic32_exchange(mi_atomic32_t* atomic, int32_t value);
int mi_atomic32_compare_exchange(mi_atomic32_t* atomic, int32_t* expected, int32_t desired);

#ifdef __cplusplus
}
#endif

#endif
