#ifndef MiTimer_H_
#define MiTimer_H_

#include "mibase.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef void (*FpTimerFunc)(void* param);

typedef struct heap_timer {
    uint32_t expire;
    int timeout;
    FpTimerFunc cb_func;
    void* user_data;
} heap_timer;

typedef struct h_t_manager {
    struct heap_timer** array;
    int capacity;
    int cur_size;
} h_t_manager;

void        ht_init_manager(h_t_manager* tmanager, int cap);
heap_timer* ht_add_timer(h_t_manager* tmanager, int timeout, FpTimerFunc func, void* userdata);
int         ht_del_timer(h_t_manager* tmanager, heap_timer* timer);
void        ht_tick(h_t_manager* tmanager);

#ifdef __cplusplus
}
#endif

#endif
