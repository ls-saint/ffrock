#include "mihtimer.h"

#include <string.h>

static int ht_empty(h_t_manager* tmanager)
{
    return tmanager == NULL || tmanager->cur_size <= 0;
}

static int resize(h_t_manager* tmanager)
{
    int new_capacity;
    heap_timer** temp;

    if (tmanager == NULL) {
        return -1;
    }

    new_capacity = tmanager->capacity > 0 ? tmanager->capacity * 2 : 4;
    temp = (heap_timer**)mi_mem_malloc((int)(new_capacity * sizeof(heap_timer*)));
    if (temp == NULL) {
        return -1;
    }

    memset(temp, 0, new_capacity * sizeof(heap_timer*));
    if (tmanager->array != NULL && tmanager->cur_size > 0) {
        memcpy(temp, tmanager->array, tmanager->cur_size * sizeof(heap_timer*));
    }

    mi_mem_free(tmanager->array);
    tmanager->array = temp;
    tmanager->capacity = new_capacity;
    return 0;
}

static void percolate_down(h_t_manager* tmanager, int hole)
{
    heap_timer* timer;
    int child;

    if (tmanager == NULL || tmanager->array == NULL || hole < 0 || hole >= tmanager->cur_size) {
        return;
    }

    timer = tmanager->array[hole];
    while ((hole * 2 + 1) < tmanager->cur_size) {
        child = hole * 2 + 1;
        if (child + 1 < tmanager->cur_size &&
            tmanager->array[child + 1]->expire < tmanager->array[child]->expire) {
            ++child;
        }

        if (tmanager->array[child]->expire >= timer->expire) {
            break;
        }

        tmanager->array[hole] = tmanager->array[child];
        hole = child;
    }
    tmanager->array[hole] = timer;
}

void ht_init_manager(h_t_manager* tmanager, int cap)
{
    if (tmanager == NULL) {
        return;
    }

    memset(tmanager, 0, sizeof(*tmanager));
    tmanager->capacity = cap > 0 ? cap : 4;
    tmanager->array = (heap_timer**)mi_mem_malloc((int)(tmanager->capacity * sizeof(heap_timer*)));
    if (tmanager->array == NULL) {
        tmanager->capacity = 0;
        return;
    }

    memset(tmanager->array, 0, tmanager->capacity * sizeof(heap_timer*));
}

heap_timer* ht_add_timer(h_t_manager* tmanager, int timeout, FpTimerFunc func, void* userdata)
{
    int hole;
    int parent;
    heap_timer* timer;

    if (tmanager == NULL || timeout <= 0 || func == NULL) {
        return NULL;
    }

    if (tmanager->cur_size >= tmanager->capacity && resize(tmanager) != 0) {
        return NULL;
    }

    timer = (heap_timer*)mi_mem_malloc((int)sizeof(heap_timer));
    if (timer == NULL) {
        return NULL;
    }

    timer->expire = mi_getTick32() + (uint32_t)timeout * 1000u;
    timer->timeout = timeout;
    timer->user_data = userdata;
    timer->cb_func = func;

    hole = tmanager->cur_size++;
    while (hole > 0) {
        parent = (hole - 1) / 2;
        if (tmanager->array[parent]->expire <= timer->expire) {
            break;
        }
        tmanager->array[hole] = tmanager->array[parent];
        hole = parent;
    }

    tmanager->array[hole] = timer;
    return timer;
}

int ht_del_timer(h_t_manager* tmanager, heap_timer* timer)
{
    (void)tmanager;
    if (timer == NULL) {
        return -1;
    }

    timer->cb_func = NULL;
    return 0;
}

static int pop_timer(h_t_manager* tmanager)
{
    heap_timer* top;

    if (ht_empty(tmanager) || tmanager->array == NULL) {
        return -1;
    }

    top = tmanager->array[0];
    tmanager->array[0] = tmanager->array[--tmanager->cur_size];
    tmanager->array[tmanager->cur_size] = NULL;
    mi_mem_free(top);

    if (tmanager->cur_size > 0) {
        percolate_down(tmanager, 0);
    }
    return 0;
}

void ht_tick(h_t_manager* tmanager)
{
    uint32_t cur;

    if (ht_empty(tmanager) || tmanager->array == NULL) {
        return;
    }

    cur = mi_getTick32();
    while (!ht_empty(tmanager) && tmanager->array[0] != NULL) {
        heap_timer* timer = tmanager->array[0];
        if ((int32_t)(timer->expire - cur) > 0) {
            break;
        }

        if (timer->cb_func != NULL) {
            timer->cb_func(timer->user_data);
        }
        pop_timer(tmanager);
    }
}
