#ifndef MI_THREAD_POOL_H_
#define MI_THREAD_POOL_H_

#include "mibase.h"
#include "mithread.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mi_thread_pool mi_thread_pool_t;
typedef void (*mi_thread_pool_task_fn)(void* user);

typedef struct mi_thread_pool_config {
    const char* name;
    int thread_count;
    int max_queue_size;
    int priority;
    uint32_t stack_size;
} mi_thread_pool_config_t;

typedef struct mi_thread_pool_stats {
    int thread_count;
    int queued_tasks;
    int running_tasks;
    int max_queue_size;
    uint32_t submitted_tasks;
    uint32_t completed_tasks;
    uint32_t dropped_tasks;
} mi_thread_pool_stats_t;

mi_thread_pool_t* mi_thread_pool_create(const mi_thread_pool_config_t* config);
void mi_thread_pool_destroy(mi_thread_pool_t* pool, int wait_pending);
int mi_thread_pool_submit(mi_thread_pool_t* pool, mi_thread_pool_task_fn fn, void* user);
int mi_thread_pool_wait_idle(mi_thread_pool_t* pool, uint32_t timeout_ms);
void mi_thread_pool_get_stats(mi_thread_pool_t* pool, mi_thread_pool_stats_t* stats);
int mi_thread_pool_queued_count(mi_thread_pool_t* pool);
int mi_thread_pool_running_count(mi_thread_pool_t* pool);

#ifdef __cplusplus
}
#endif

#endif
