#ifndef MIIPC_CV610_OSD_H
#define MIIPC_CV610_OSD_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "sample_comm.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MI_CV610_OSD_MAX_ENTRIES 4
#define MI_CV610_OSD_MAX_TEXT 96

typedef struct mi_cv610_osd_entry_s {
    int index;
    hi_rgn_handle handle;
    int created;
    int attached;
    int show;
    int timestamp_mode;
    int font_size;
    int align;
    int pos_x;
    int pos_y;
    int forced_width;
    int forced_height;
    int bmp_width;
    int bmp_height;
    char text[MI_CV610_OSD_MAX_TEXT];
    uint16_t* pixels;
    size_t pixel_capacity;
} mi_cv610_osd_entry_t;

typedef struct mi_cv610_osd_s {
    pthread_mutex_t lock;
    int lock_ready;
    int started;
    int handle_base;
    int venc_chn;
    int video_width;
    int video_height;
    time_t last_update_sec;
    mi_cv610_osd_entry_t entry[MI_CV610_OSD_MAX_ENTRIES];
} mi_cv610_osd_t;

void mi_cv610_osd_init(mi_cv610_osd_t* osd);
void mi_cv610_osd_init_ex(mi_cv610_osd_t* osd, int handle_base);
void mi_cv610_osd_deinit(mi_cv610_osd_t* osd);

int mi_cv610_osd_start(mi_cv610_osd_t* osd, int venc_chn, int video_width, int video_height);
void mi_cv610_osd_stop(mi_cv610_osd_t* osd);
void mi_cv610_osd_tick(mi_cv610_osd_t* osd);
void mi_cv610_osd_update(mi_cv610_osd_t* osd, int index);

int mi_cv610_osd_set_show(mi_cv610_osd_t* osd, int index, int show);
int mi_cv610_osd_get_show(mi_cv610_osd_t* osd, int index);
int mi_cv610_osd_set_text(mi_cv610_osd_t* osd, int index, const char* text);
int mi_cv610_osd_set_font_size(mi_cv610_osd_t* osd, int index, int font_size);
int mi_cv610_osd_get_font_size(mi_cv610_osd_t* osd, int index);
int mi_cv610_osd_reset_size(mi_cv610_osd_t* osd, int index, int pos_x, int pos_y, int width, int height);
int mi_cv610_osd_set_align(mi_cv610_osd_t* osd, int index, int align);

#ifdef __cplusplus
}
#endif

#endif
