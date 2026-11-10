#include "mithreadpool.h"
#include "misemaphore.h"
#include "mimutex.h"

#include <stdio.h>
#include <string.h>

#define MI_THREAD_POOL_DEFAULT_THREADS 2
#define MI_THREAD_POOL_DEFAULT_QUEUE 64
#define MI_THREAD_POOL_MAX_THREADS 32
#define MI_THREAD_POOL_UNLIMITED_QUEUE_SEM_MAX 32767

typedef struct mi_thread_pool_task {
    mi_thread_pool_task_fn fn;
    void* user;
    struct mi_thread_pool_task* next;
} mi_thread_pool_task_t;

struct mi_thread_pool {
    mi_Mutex* lock;
    mi_Semaphore* task_sem;
    mi_Semaphore* idle_sem;
    mi_thread_t* threads;
    int thread_count;
    int max_queue_size;
    volatile int stopping;
    int accept_tasks;
    int queued_tasks;
    int running_tasks;
    mi_thread_pool_task_t* head;
    mi_thread_pool_task_t* tail;
    uint32_t submitted_tasks;
    uint32_t completed_tasks;
    uint32_t dropped_tasks;
    char name[48];
};

static void pool_lock(mi_thread_pool_t* pool)
{
    if (pool != NULL && pool->lock != NULL) {
        pool->lock->Lock(pool->lock);
    }
}

static void pool_unlock(mi_thread_pool_t* pool)
{
    if (pool != NULL && pool->lock != NULL) {
        pool->lock->UnLock(pool->lock);
    }
}

static void task_free_list(mi_thread_pool_task_t* task)
{
    while (task != NULL) {
        mi_thread_pool_task_t* next = task->next;
        mi_mem_free(task);
        task = next;
    }
}

static mi_thread_pool_task_t* pop_task_nolock(mi_thread_pool_t* pool)
{
    mi_thread_pool_task_t* task;
    if (pool == NULL || pool->head == NULL) {
        return NULL;
    }
    task = pool->head;
    pool->head = task->next;
    if (pool->head == NULL) {
        pool->tail = NULL;
    }
    task->next = NULL;
    if (pool->queued_tasks > 0) {
        pool->queued_tasks--;
    }
    pool->running_tasks++;
    return task;
}

static void worker_finish_task(mi_thread_pool_t* pool)
{
    int idle;

    pool_lock(pool);
    if (pool->running_tasks > 0) {
        pool->running_tasks--;
    }
    pool->completed_tasks++;
    idle = (pool->queued_tasks == 0 && pool->running_tasks == 0);
    pool_unlock(pool);

    if (idle) {
        mi_SemaphorePost(pool->idle_sem, 1);
    }
}

static void* thread_pool_worker(void* arg)
{
    mi_thread_pool_t* pool = (mi_thread_pool_t*)arg;

    while (pool != NULL) {
        mi_thread_pool_task_t* task = NULL;

        mi_SemaphoreWait(pool->task_sem, 0);

        pool_lock(pool);
        if (!pool->stopping || pool->head != NULL) {
            task = pop_task_nolock(pool);
        }
        if (pool->stopping && task == NULL) {
            pool_unlock(pool);
            break;
        }
        pool_unlock(pool);

        if (task != NULL) {
            mi_thread_pool_task_fn fn = task->fn;
            void* user = task->user;
            mi_mem_free(task);
            if (fn != NULL) {
                fn(user);
            }
            worker_finish_task(pool);
        }
    }

    return NULL;
}

static int normalize_thread_count(int count)
{
    if (count <= 0) {
        return MI_THREAD_POOL_DEFAULT_THREADS;
    }
    if (count > MI_THREAD_POOL_MAX_THREADS) {
        return MI_THREAD_POOL_MAX_THREADS;
    }
    return count;
}

static int normalize_queue_size(int count)
{
    if (count < 0) {
        return 0;
    }
    if (count == 0) {
        return MI_THREAD_POOL_DEFAULT_QUEUE;
    }
    return count;
}

static int task_sem_max_count(int max_queue_size, int thread_count)
{
    int max_count;
    if (max_queue_size <= 0) {
        return MI_THREAD_POOL_UNLIMITED_QUEUE_SEM_MAX;
    }
    max_count = max_queue_size + thread_count;
    return max_count > MI_THREAD_POOL_UNLIMITED_QUEUE_SEM_MAX ? MI_THREAD_POOL_UNLIMITED_QUEUE_SEM_MAX : max_count;
}

mi_thread_pool_t* mi_thread_pool_create(const mi_thread_pool_config_t* config)
{
    mi_thread_pool_t* pool;
    int i;
    int ok = 1;
    int sem_max;

    pool = (mi_thread_pool_t*)mi_mem_malloc((int)sizeof(mi_thread_pool_t));
    if (pool == NULL) {
        return NULL;
    }
    memset(pool, 0, sizeof(*pool));
    pool->thread_count = normalize_thread_count(config != NULL ? config->thread_count : 0);
    pool->max_queue_size = normalize_queue_size(config != NULL ? config->max_queue_size : 0);
    pool->accept_tasks = 1;
    if (config != NULL && config->name != NULL && config->name[0] != '\0') {
        strncpy(pool->name, config->name, sizeof(pool->name) - 1);
        pool->name[sizeof(pool->name) - 1] = '\0';
    } else {
        strncpy(pool->name, "mi_pool", sizeof(pool->name) - 1);
    }

    pool->lock = mi_CreateMutex(1);
    sem_max = task_sem_max_count(pool->max_queue_size, pool->thread_count);
    pool->task_sem = mi_CreateSemaphore(0, sem_max);
    pool->idle_sem = mi_CreateSemaphore(0, pool->thread_count + 1);
    pool->threads = (mi_thread_t*)mi_mem_malloc((int)(sizeof(mi_thread_t) * (size_t)pool->thread_count));
    if (pool->lock == NULL || pool->task_sem == NULL || pool->idle_sem == NULL || pool->threads == NULL) {
        mi_thread_pool_destroy(pool, 0);
        return NULL;
    }
    memset(pool->threads, 0, sizeof(mi_thread_t) * (size_t)pool->thread_count);

    for (i = 0; i < pool->thread_count; ++i) {
        thread_config_t thread_config;
        char thread_name[64];
        memset(&thread_config, 0, sizeof(thread_config));
        snprintf(thread_name, sizeof(thread_name), "%s_%d", pool->name, i);
        thread_config.name = thread_name;
        thread_config.priority = config != NULL ? config->priority : 0;
        thread_config.stack_size = config != NULL ? config->stack_size : 0;
        thread_config.detached = 0;
        if (mi_thread_create(&pool->threads[i], thread_pool_worker, pool, &thread_config) != MI_THREAD_OK) {
            ok = 0;
            pool->thread_count = i;
            break;
        }
    }

    if (!ok) {
        mi_thread_pool_destroy(pool, 0);
        return NULL;
    }
    return pool;
}

void mi_thread_pool_destroy(mi_thread_pool_t* pool, int wait_pending)
{
    int i;
    mi_thread_pool_task_t* drop = NULL;
    int signal_idle = 0;

    if (pool == NULL) {
        return;
    }

    pool_lock(pool);
    pool->accept_tasks = 0;
    if (!wait_pending) {
        drop = pool->head;
        pool->head = NULL;
        pool->tail = NULL;
        pool->dropped_tasks += (uint32_t)pool->queued_tasks;
        pool->queued_tasks = 0;
    }
    pool->stopping = 1;
    signal_idle = (pool->queued_tasks == 0 && pool->running_tasks == 0);
    pool_unlock(pool);

    mi_SemaphorePost(pool->task_sem, pool->thread_count);
    if (signal_idle) {
        mi_SemaphorePost(pool->idle_sem, 1);
    }

    task_free_list(drop);

    if (pool->threads != NULL) {
        for (i = 0; i < pool->thread_count; ++i) {
            mi_thread_join(&pool->threads[i], NULL, 0);
        }
    }

    if (pool->threads != NULL) {
        mi_mem_free(pool->threads);
    }

    pool_lock(pool);
    drop = pool->head;
    pool->head = NULL;
    pool->tail = NULL;
    pool->queued_tasks = 0;
    pool_unlock(pool);
    task_free_list(drop);

    mi_DelSemaphore(pool->task_sem);
    mi_DelSemaphore(pool->idle_sem);
    mi_DelMutex(pool->lock);
    mi_mem_free(pool);
}

int mi_thread_pool_submit(mi_thread_pool_t* pool, mi_thread_pool_task_fn fn, void* user)
{
    mi_thread_pool_task_t* task;

    if (pool == NULL || fn == NULL) {
        return -1;
    }
    task = (mi_thread_pool_task_t*)mi_mem_malloc((int)sizeof(mi_thread_pool_task_t));
    if (task == NULL) {
        return -2;
    }
    task->fn = fn;
    task->user = user;
    task->next = NULL;

    pool_lock(pool);
    if (!pool->accept_tasks || pool->stopping) {
        pool->dropped_tasks++;
        pool_unlock(pool);
        mi_mem_free(task);
        return -3;
    }
    if (pool->max_queue_size > 0 && pool->queued_tasks >= pool->max_queue_size) {
        pool->dropped_tasks++;
        pool_unlock(pool);
        mi_mem_free(task);
        return -4;
    }
    if (pool->tail != NULL) {
        pool->tail->next = task;
    } else {
        pool->head = task;
    }
    pool->tail = task;
    pool->queued_tasks++;
    pool->submitted_tasks++;
    pool_unlock(pool);
    mi_SemaphorePost(pool->task_sem, 1);
    return 0;
}

int mi_thread_pool_wait_idle(mi_thread_pool_t* pool, uint32_t timeout_ms)
{
    uint32_t start;
    if (pool == NULL) {
        return -1;
    }
    start = mi_getTick32();
    for (;;) {
        int idle;
        uint32_t remain = 0;
        pool_lock(pool);
        idle = pool->queued_tasks == 0 && pool->running_tasks == 0;
        pool_unlock(pool);
        if (idle) {
            return 0;
        }
        if (timeout_ms != 0 && (uint32_t)(mi_getTick32() - start) >= timeout_ms) {
            return -2;
        }
        if (timeout_ms != 0) {
            uint32_t elapsed = (uint32_t)(mi_getTick32() - start);
            remain = elapsed >= timeout_ms ? 1u : (timeout_ms - elapsed);
        }
        if (mi_SemaphoreWait(pool->idle_sem, remain) == MI_SEMAPHORE_ERR_TIMEOUT) {
            return -2;
        }
    }
}

void mi_thread_pool_get_stats(mi_thread_pool_t* pool, mi_thread_pool_stats_t* stats)
{
    if (stats == NULL) {
        return;
    }
    memset(stats, 0, sizeof(*stats));
    if (pool == NULL) {
        return;
    }
    pool_lock(pool);
    stats->thread_count = pool->thread_count;
    stats->queued_tasks = pool->queued_tasks;
    stats->running_tasks = pool->running_tasks;
    stats->max_queue_size = pool->max_queue_size;
    stats->submitted_tasks = pool->submitted_tasks;
    stats->completed_tasks = pool->completed_tasks;
    stats->dropped_tasks = pool->dropped_tasks;
    pool_unlock(pool);
}

int mi_thread_pool_queued_count(mi_thread_pool_t* pool)
{
    int count;
    if (pool == NULL) {
        return 0;
    }
    pool_lock(pool);
    count = pool->queued_tasks;
    pool_unlock(pool);
    return count;
}

int mi_thread_pool_running_count(mi_thread_pool_t* pool)
{
    int count;
    if (pool == NULL) {
        return 0;
    }
    pool_lock(pool);
    count = pool->running_tasks;
    pool_unlock(pool);
    return count;
}
