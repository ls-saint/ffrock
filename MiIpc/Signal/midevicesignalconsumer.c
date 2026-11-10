#include "midevicesignalconsumer.h"
#include "../../MiBase/include/mibase.h"
#include "../../MiBase/include/mimutex.h"
#include "../../MiBase/include/misemaphore.h"
#include "../../MiBase/include/mithread.h"
#include "../JSON/cJSON.h"
#include "../DevMsg/midevmsg.h"

#include <stdio.h>
#include <string.h>

typedef struct mi_signal_msg_node {
    char* data;
    int len;
    struct mi_signal_msg_node* next;
} mi_signal_msg_node_t;

typedef struct mi_signal_consumer_state {
    mi_Mutex* lock;
    mi_Semaphore* sem;
    mi_thread_t thread;
    mi_signal_msg_node_t* head;
    mi_signal_msg_node_t* tail;
    int started;
    int stopping;
    int queue_count;
    int queue_limit;
    uint32_t posted;
    uint32_t processed;
    uint32_t dropped;
    uint32_t json_ok;
    uint32_t json_fail;
} mi_signal_consumer_state_t;

static mi_signal_consumer_state_t g_signal_consumer;

static void consumer_lock(void)
{
    if (g_signal_consumer.lock != NULL) {
        g_signal_consumer.lock->Lock(g_signal_consumer.lock);
    }
}

static void consumer_unlock(void)
{
    if (g_signal_consumer.lock != NULL) {
        g_signal_consumer.lock->UnLock(g_signal_consumer.lock);
    }
}

static void consumer_free_node(mi_signal_msg_node_t* node)
{
    if (node == NULL) {
        return;
    }
    if (node->data != NULL) {
        mi_mem_free(node->data);
    }
    mi_mem_free(node);
}

static mi_signal_msg_node_t* consumer_pop_nolock(void)
{
    mi_signal_msg_node_t* node = g_signal_consumer.head;
    if (node == NULL) {
        return NULL;
    }
    g_signal_consumer.head = node->next;
    if (g_signal_consumer.tail == node) {
        g_signal_consumer.tail = NULL;
    }
    node->next = NULL;
    if (g_signal_consumer.queue_count > 0) {
        g_signal_consumer.queue_count--;
    }
    return node;
}

static void consumer_drop_oldest_nolock(void)
{
    mi_signal_msg_node_t* node = consumer_pop_nolock();
    if (node != NULL) {
        g_signal_consumer.dropped++;
        consumer_free_node(node);
    }
}

static void consumer_clear_queue(void)
{
    mi_signal_msg_node_t* node;

    consumer_lock();
    node = g_signal_consumer.head;
    g_signal_consumer.head = NULL;
    g_signal_consumer.tail = NULL;
    g_signal_consumer.queue_count = 0;
    consumer_unlock();

    while (node != NULL) {
        mi_signal_msg_node_t* next = node->next;
        consumer_free_node(node);
        node = next;
    }
}

static int consumer_process_message(const char* data, int len)
{
    cJSON* root;
    int ret;

    root = cJSON_ParseWithLength(data, (size_t)len);
    if (root == NULL || !cJSON_IsObject(root)) {
        const char* err = cJSON_GetErrorPtr();
        printf("MiIpc device control json parse failed len=%d err=%s data=%.*s\n",
               len,
               err != NULL ? err : "",
               len > 256 ? 256 : len,
               data != NULL ? data : "");
        if (root != NULL) {
            cJSON_Delete(root);
        }
        fflush(stdout);
        return -1;
    }

    ret = mi_devmsg_dispatch(root);
    cJSON_Delete(root);
    fflush(stdout);
    return ret == 0 ? 0 : -1;
}

static void* consumer_thread_main(void* arg)
{
    (void)arg;

    while (1) {
        mi_signal_msg_node_t* node;

        mi_SemaphoreWait(g_signal_consumer.sem, 200);

        consumer_lock();
        if (g_signal_consumer.stopping && g_signal_consumer.head == NULL) {
            consumer_unlock();
            break;
        }
        node = consumer_pop_nolock();
        consumer_unlock();

        if (node == NULL) {
            continue;
        }

        if (consumer_process_message(node->data, node->len) == 0) {
            consumer_lock();
            g_signal_consumer.json_ok++;
            consumer_unlock();
        } else {
            consumer_lock();
            g_signal_consumer.json_fail++;
            consumer_unlock();
        }
        consumer_lock();
        g_signal_consumer.processed++;
        consumer_unlock();
        consumer_free_node(node);
    }

    return NULL;
}

int mi_device_signal_consumer_start(int queue_limit)
{
    thread_config_t thread_config;

    if (queue_limit <= 0) {
        queue_limit = MIIPC_SIGNAL_CONSUMER_DEFAULT_QUEUE_LIMIT;
    }

    if (g_signal_consumer.lock == NULL) {
        g_signal_consumer.lock = mi_CreateMutex(1);
        if (g_signal_consumer.lock == NULL) {
            return -1;
        }
    }
    if (g_signal_consumer.sem == NULL) {
        g_signal_consumer.sem = mi_CreateSemaphore(0, queue_limit);
        if (g_signal_consumer.sem == NULL) {
            return -1;
        }
    }

    consumer_lock();
    if (g_signal_consumer.started) {
        consumer_unlock();
        return 0;
    }
    g_signal_consumer.stopping = 0;
    g_signal_consumer.queue_limit = queue_limit;
    g_signal_consumer.posted = 0;
    g_signal_consumer.processed = 0;
    g_signal_consumer.dropped = 0;
    g_signal_consumer.json_ok = 0;
    g_signal_consumer.json_fail = 0;
    consumer_unlock();

    memset(&thread_config, 0, sizeof(thread_config));
    thread_config.name = "miipc_signal_consumer";
    thread_config.stack_size = 8192;
    thread_config.detached = 0;
    if (mi_thread_create(&g_signal_consumer.thread, consumer_thread_main, NULL, &thread_config) != MI_THREAD_OK) {
        return -1;
    }

    consumer_lock();
    g_signal_consumer.started = 1;
    consumer_unlock();
    return 0;
}

void mi_device_signal_consumer_stop(void)
{
    int started;

    consumer_lock();
    started = g_signal_consumer.started;
    g_signal_consumer.stopping = 1;
    consumer_unlock();

    if (!started) {
        consumer_clear_queue();
        return;
    }

    if (g_signal_consumer.sem != NULL) {
        mi_SemaphorePost(g_signal_consumer.sem, 1);
    }
    mi_thread_join(&g_signal_consumer.thread, NULL, 3000);

    consumer_lock();
    g_signal_consumer.started = 0;
    consumer_unlock();
    consumer_clear_queue();
}

int mi_device_signal_consumer_post(const char* data, int len)
{
    mi_signal_msg_node_t* node;
    int should_post = 0;

    if (data == NULL || len <= 0) {
        return -1;
    }
    if (len > MIIPC_SIGNAL_CONSUMER_MAX_MESSAGE_SIZE) {
        len = MIIPC_SIGNAL_CONSUMER_MAX_MESSAGE_SIZE;
    }
    if (!g_signal_consumer.started) {
        return -1;
    }

    node = (mi_signal_msg_node_t*)mi_mem_malloc((int)sizeof(mi_signal_msg_node_t));
    if (node == NULL) {
        return -1;
    }
    memset(node, 0, sizeof(*node));
    node->data = (char*)mi_mem_malloc(len + 1);
    if (node->data == NULL) {
        mi_mem_free(node);
        return -1;
    }
    memcpy(node->data, data, (size_t)len);
    node->data[len] = '\0';
    node->len = len;

    consumer_lock();
    if (g_signal_consumer.stopping) {
        consumer_unlock();
        consumer_free_node(node);
        return -1;
    }
    if (g_signal_consumer.queue_count >= g_signal_consumer.queue_limit) {
        consumer_drop_oldest_nolock();
        should_post = 0;
    } else {
        should_post = 1;
    }
    if (g_signal_consumer.tail != NULL) {
        g_signal_consumer.tail->next = node;
    } else {
        g_signal_consumer.head = node;
    }
    g_signal_consumer.tail = node;
    g_signal_consumer.queue_count++;
    g_signal_consumer.posted++;
    consumer_unlock();

    if (should_post && g_signal_consumer.sem != NULL) {
        mi_SemaphorePost(g_signal_consumer.sem, 1);
    }
    return 0;
}

int mi_device_signal_consumer_get_info(char* out, int out_len)
{
    int n;

    if (out == NULL || out_len <= 0) {
        return 0;
    }

    consumer_lock();
    n = snprintf(out,
                 (size_t)out_len,
                 "signal_consumer started=%d queue=%d limit=%d posted=%u processed=%u dropped=%u json_ok=%u json_fail=%u\n",
                 g_signal_consumer.started,
                 g_signal_consumer.queue_count,
                 g_signal_consumer.queue_limit,
                 g_signal_consumer.posted,
                 g_signal_consumer.processed,
                 g_signal_consumer.dropped,
                 g_signal_consumer.json_ok,
                 g_signal_consumer.json_fail);
    consumer_unlock();

    if (n < 0) {
        out[0] = '\0';
        return 0;
    }
    return n >= out_len ? out_len - 1 : n;
}
