#include "miipcsdstorage.h"

#include "../../MiBase/include/mibase.h"
#include "../../MiBase/include/mimutex.h"
#include "../../MiBase/include/misemaphore.h"
#include "../../MiBase/include/mithread.h"

#include "libmp4.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#define MIIPC_SD_STORAGE_MAX_CHANNELS 8
#define MIIPC_SD_STORAGE_PATH_MAX 512
#define MIIPC_SD_STORAGE_WAIT_MS 200
#define MIIPC_SD_STORAGE_SEGMENT_MS 60000
#define MIIPC_SD_STORAGE_DEFAULT_FPS 25
#define MIIPC_SD_STORAGE_MIN_WIDTH 640
#define MIIPC_SD_STORAGE_MIN_HEIGHT 360
#define MIIPC_SD_STORAGE_G711A_CHANNELS 1
#define MIIPC_SD_STORAGE_G711A_SAMPLE_RATE 8000
#define MIIPC_SD_STORAGE_WRITE_CACHE_SIZE 8192
#define MIIPC_SD_STORAGE_MP4_MAX_TRACKS 2
#define MIIPC_SD_STORAGE_VPS_MAX 512
#define MIIPC_SD_STORAGE_SPS_MAX 1024
#define MIIPC_SD_STORAGE_PPS_MAX 512

typedef struct miipc_sd_storage_node {
    int chn;
    int codec;
    int width;
    int height;
    int frame_rate;
    int len;
    uint64_t pts;
    int key_frame;
    uint8_t* data;
    struct miipc_sd_storage_node* next;
} miipc_sd_storage_node_t;

typedef struct miipc_sd_storage_segment {
    int in_use;
    int chn;
    int codec;
    int width;
    int height;
    int frame_rate;
    libmp4_writer_t* writer;
    int video_track;
    int audio_track;
    int sample_count;
    int64_t video_ms;
    int64_t audio_ms;
    uint64_t last_src_pts;
    int have_last_src_pts;
    char tmp_path[MIIPC_SD_STORAGE_PATH_MAX];
    char final_prefix[MIIPC_SD_STORAGE_PATH_MAX];
    uint8_t vps[MIIPC_SD_STORAGE_VPS_MAX];
    uint16_t vps_len;
    uint8_t sps[MIIPC_SD_STORAGE_SPS_MAX];
    uint16_t sps_len;
    uint8_t pps[MIIPC_SD_STORAGE_PPS_MAX];
    uint16_t pps_len;
} miipc_sd_storage_segment_t;

typedef struct miipc_sd_storage_state {
    mi_Mutex* lock;
    mi_Semaphore* sem;
    mi_thread_t thread;
    miipc_sd_storage_node_t* head;
    miipc_sd_storage_node_t* tail;
    miipc_sd_storage_segment_t segments[MIIPC_SD_STORAGE_MAX_CHANNELS];
    char root[MIIPC_SD_STORAGE_PATH_MAX];
    int started;
    int enabled;
    int available;
    int availability_configured;
    int stopping;
    int flush_request;
    int queue_count;
    int queue_limit;
    uint32_t posted;
    uint32_t written;
    uint32_t dropped;
    uint32_t write_failed;
} miipc_sd_storage_state_t;

static miipc_sd_storage_state_t g_sd_storage;

static void sd_storage_lock(void)
{
    if (g_sd_storage.lock != NULL) {
        g_sd_storage.lock->Lock(g_sd_storage.lock);
    }
}

static void sd_storage_unlock(void)
{
    if (g_sd_storage.lock != NULL) {
        g_sd_storage.lock->UnLock(g_sd_storage.lock);
    }
}

static int sd_storage_is_available(void)
{
    int available;

    sd_storage_lock();
    available = g_sd_storage.available;
    sd_storage_unlock();
    return available ? 1 : 0;
}

static void sd_storage_free_node(miipc_sd_storage_node_t* node)
{
    if (node == NULL) {
        return;
    }
    if (node->data != NULL) {
        mi_mem_free_large(node->data);
    }
    mi_mem_free(node);
}

static miipc_sd_storage_node_t* sd_storage_pop_nolock(void)
{
    miipc_sd_storage_node_t* node = g_sd_storage.head;
    if (node == NULL) {
        return NULL;
    }
    g_sd_storage.head = node->next;
    if (g_sd_storage.tail == node) {
        g_sd_storage.tail = NULL;
    }
    if (g_sd_storage.queue_count > 0) {
        g_sd_storage.queue_count--;
    }
    node->next = NULL;
    return node;
}

static void sd_storage_drop_oldest_nolock(void)
{
    miipc_sd_storage_node_t* node = sd_storage_pop_nolock();
    if (node != NULL) {
        g_sd_storage.dropped++;
        sd_storage_free_node(node);
    }
}

static void sd_storage_clear_queue(void)
{
    miipc_sd_storage_node_t* node;

    sd_storage_lock();
    node = g_sd_storage.head;
    g_sd_storage.head = NULL;
    g_sd_storage.tail = NULL;
    g_sd_storage.queue_count = 0;
    sd_storage_unlock();

    while (node != NULL) {
        miipc_sd_storage_node_t* next = node->next;
        sd_storage_free_node(node);
        node = next;
    }
}

static void sd_storage_make_root_dir(const char* root)
{
    if (root == NULL || root[0] == '\0') {
        return;
    }
#if defined(_WIN32)
    (void)_mkdir(root);
#else
    (void)mkdir(root, 0755);
#endif
}

static int sd_storage_localtime(time_t now, struct tm* out)
{
    if (out == NULL) {
        return -1;
    }
#if defined(_WIN32)
    return localtime_s(out, &now) == 0 ? 0 : -1;
#else
    return localtime_r(&now, out) != NULL ? 0 : -1;
#endif
}

static int sd_storage_frame_duration_ms(int frame_rate)
{
    if (frame_rate <= 0 || frame_rate > 120) {
        frame_rate = MIIPC_SD_STORAGE_DEFAULT_FPS;
    }
    return (1000 + frame_rate / 2) / frame_rate;
}

static int sd_storage_is_video_codec(int codec)
{
    return codec == CI_H264 || codec == CI_H265;
}

typedef struct miipc_sd_storage_video_scan {
    int has_vcl;
    int key_frame;
    int config_update;
} miipc_sd_storage_video_scan_t;

static const uint8_t* sd_storage_find_start_code(const uint8_t* p,
                                                 const uint8_t* end,
                                                 int* code_len)
{
    while (p + 3 <= end) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1) {
            if (code_len != NULL) {
                *code_len = 3;
            }
            return p;
        }
        if (p + 4 <= end && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1) {
            if (code_len != NULL) {
                *code_len = 4;
            }
            return p;
        }
        ++p;
    }
    return NULL;
}

static void sd_storage_trim_nal(const uint8_t** nal, size_t* len)
{
    while (*len > 0 && (*nal)[*len - 1u] == 0) {
        --(*len);
    }
}

static int sd_storage_save_param_set(uint8_t* dst,
                                     uint16_t* dst_len,
                                     uint32_t dst_cap,
                                     const uint8_t* nal,
                                     size_t len)
{
    if (dst == NULL || dst_len == NULL || nal == NULL || len == 0 || len > dst_cap) {
        return -1;
    }
    if (*dst_len == (uint16_t)len && memcmp(dst, nal, len) == 0) {
        return 0;
    }
    memcpy(dst, nal, len);
    *dst_len = (uint16_t)len;
    return 1;
}

static void sd_storage_scan_h264_nal(miipc_sd_storage_segment_t* segment,
                                     const uint8_t* nal,
                                     size_t len,
                                     miipc_sd_storage_video_scan_t* scan)
{
    uint8_t type;
    int saved;

    if (segment == NULL || nal == NULL || len == 0 || scan == NULL) {
        return;
    }

    type = (uint8_t)(nal[0] & 0x1fu);
    if (type == 7) {
        saved = sd_storage_save_param_set(segment->sps,
                                          &segment->sps_len,
                                          MIIPC_SD_STORAGE_SPS_MAX,
                                          nal,
                                          len);
        if (saved > 0) {
            scan->config_update = 1;
        }
        return;
    }
    if (type == 8) {
        saved = sd_storage_save_param_set(segment->pps,
                                          &segment->pps_len,
                                          MIIPC_SD_STORAGE_PPS_MAX,
                                          nal,
                                          len);
        if (saved > 0) {
            scan->config_update = 1;
        }
        return;
    }
    if (type == 9) {
        return;
    }
    if (type == 1 || type == 5) {
        scan->has_vcl = 1;
    }
    if (type == 5) {
        scan->key_frame = 1;
    }
}

static void sd_storage_scan_h265_nal(miipc_sd_storage_segment_t* segment,
                                     const uint8_t* nal,
                                     size_t len,
                                     miipc_sd_storage_video_scan_t* scan)
{
    uint8_t type;
    int saved;

    if (segment == NULL || nal == NULL || len < 2 || scan == NULL) {
        return;
    }

    type = (uint8_t)((nal[0] >> 1) & 0x3fu);
    if (type == 32) {
        saved = sd_storage_save_param_set(segment->vps,
                                          &segment->vps_len,
                                          MIIPC_SD_STORAGE_VPS_MAX,
                                          nal,
                                          len);
        if (saved > 0) {
            scan->config_update = 1;
        }
        return;
    }
    if (type == 33) {
        saved = sd_storage_save_param_set(segment->sps,
                                          &segment->sps_len,
                                          MIIPC_SD_STORAGE_SPS_MAX,
                                          nal,
                                          len);
        if (saved > 0) {
            scan->config_update = 1;
        }
        return;
    }
    if (type == 34) {
        saved = sd_storage_save_param_set(segment->pps,
                                          &segment->pps_len,
                                          MIIPC_SD_STORAGE_PPS_MAX,
                                          nal,
                                          len);
        if (saved > 0) {
            scan->config_update = 1;
        }
        return;
    }
    if (type == 35) {
        return;
    }
    if (type <= 31) {
        scan->has_vcl = 1;
    }
    if (type >= 16 && type <= 21) {
        scan->key_frame = 1;
    }
}

static void sd_storage_scan_video_annexb(miipc_sd_storage_segment_t* segment,
                                         const miipc_sd_storage_node_t* node,
                                         miipc_sd_storage_video_scan_t* scan)
{
    const uint8_t* begin;
    const uint8_t* end;
    const uint8_t* sc;
    int code_len = 0;

    if (segment == NULL || node == NULL || node->data == NULL || node->len <= 0 || scan == NULL) {
        return;
    }

    begin = node->data;
    end = begin + node->len;
    sc = sd_storage_find_start_code(begin, end, &code_len);
    if (sc == NULL) {
        const uint8_t* nal = begin;
        size_t len = (size_t)node->len;
        sd_storage_trim_nal(&nal, &len);
        if (node->codec == CI_H264) {
            sd_storage_scan_h264_nal(segment, nal, len, scan);
        } else if (node->codec == CI_H265) {
            sd_storage_scan_h265_nal(segment, nal, len, scan);
        }
        return;
    }

    while (sc != NULL) {
        const uint8_t* nal = sc + code_len;
        const uint8_t* next;
        int next_code_len = 0;
        size_t len;

        next = sd_storage_find_start_code(nal, end, &next_code_len);
        len = (size_t)((next != NULL ? next : end) - nal);
        sd_storage_trim_nal(&nal, &len);
        if (len > 0) {
            if (node->codec == CI_H264) {
                sd_storage_scan_h264_nal(segment, nal, len, scan);
            } else if (node->codec == CI_H265) {
                sd_storage_scan_h265_nal(segment, nal, len, scan);
            }
        }
        sc = next;
        code_len = next_code_len;
    }
}

static void sd_storage_reset_segment_runtime(miipc_sd_storage_segment_t* segment)
{
    if (segment == NULL) {
        return;
    }
    segment->writer = NULL;
    segment->video_track = -1;
    segment->audio_track = -1;
    segment->sample_count = 0;
    segment->video_ms = 0;
    segment->audio_ms = 0;
    segment->last_src_pts = 0;
    segment->have_last_src_pts = 0;
    segment->tmp_path[0] = '\0';
    segment->final_prefix[0] = '\0';
}

static void sd_storage_reset_segment_codec(miipc_sd_storage_segment_t* segment)
{
    if (segment == NULL) {
        return;
    }
    segment->vps_len = 0;
    segment->sps_len = 0;
    segment->pps_len = 0;
    segment->codec = CI_UnDef;
}

static int sd_storage_build_segment_paths(miipc_sd_storage_segment_t* segment)
{
    char date_dir[MIIPC_SD_STORAGE_PATH_MAX];
    const char* root;
    struct tm tmv;
    time_t now;
    int n;

    if (segment == NULL) {
        return -1;
    }

    now = time(NULL);
    if (sd_storage_localtime(now, &tmv) != 0) {
        return -1;
    }

    root = g_sd_storage.root[0] ? g_sd_storage.root : "sd";
    if (!sd_storage_is_available()) {
        return -1;
    }
    n = snprintf(date_dir,
                 sizeof(date_dir),
                 "%s/%04d%02d%02d",
                 root,
                 tmv.tm_year + 1900,
                 tmv.tm_mon + 1,
                 tmv.tm_mday);
    if (n < 0 || n >= (int)sizeof(date_dir)) {
        return -1;
    }
    sd_storage_make_root_dir(root);
    sd_storage_make_root_dir(date_dir);

    n = snprintf(segment->final_prefix,
                 sizeof(segment->final_prefix),
                 "%s/%04d%02d%02d%02d%02d%02d",
                 date_dir,
                 tmv.tm_year + 1900,
                 tmv.tm_mon + 1,
                 tmv.tm_mday,
                 tmv.tm_hour,
                 tmv.tm_min,
                 tmv.tm_sec);
    if (n < 0 || n >= (int)sizeof(segment->final_prefix)) {
        return -1;
    }

    n = snprintf(segment->tmp_path,
                 sizeof(segment->tmp_path),
                 "%s_%d.tmp",
                 segment->final_prefix,
                 segment->chn);
    return (n < 0 || n >= (int)sizeof(segment->tmp_path)) ? -1 : 0;
}

static int sd_storage_segment_duration_sec(const miipc_sd_storage_segment_t* segment)
{
    int64_t duration_ms;
    int duration_sec;

    if (segment == NULL) {
        return 1;
    }

    duration_ms = segment->video_ms > segment->audio_ms ? segment->video_ms : segment->audio_ms;
    duration_sec = (int)((duration_ms + 500) / 1000);
    if (duration_sec <= 0) {
        duration_sec = 1;
    } else if (duration_sec > 999) {
        duration_sec = 999;
    }
    return duration_sec;
}

static void sd_storage_close_segment(miipc_sd_storage_segment_t* segment, int keep_codec_state)
{
    char tmp_path[MIIPC_SD_STORAGE_PATH_MAX];
    char final_prefix[MIIPC_SD_STORAGE_PATH_MAX];
    char final_path[MIIPC_SD_STORAGE_PATH_MAX];
    int sample_count;
    int duration_sec;
    int close_ok = 1;
    int close_ret = LIBMP4_OK;
    int available;

    if (segment == NULL) {
        return;
    }

    tmp_path[0] = '\0';
    final_prefix[0] = '\0';
    snprintf(tmp_path, sizeof(tmp_path), "%s", segment->tmp_path);
    snprintf(final_prefix, sizeof(final_prefix), "%s", segment->final_prefix);
    sample_count = segment->sample_count;
    duration_sec = sd_storage_segment_duration_sec(segment);
    available = sd_storage_is_available();

    if (segment->writer != NULL) {
        if (sample_count > 0 && available) {
            close_ret = libmp4_close(segment->writer);
            if (close_ret != LIBMP4_OK) {
                close_ok = 0;
                printf("MiIpc SD storage close mp4 failed path=%s ret=%d(%s)\n",
                       tmp_path,
                       close_ret,
                       libmp4_error_text(close_ret));
                fflush(stdout);
            }
        } else {
            libmp4_abort(segment->writer);
        }
        segment->writer = NULL;
    }

    if (available && sample_count > 0 && close_ok &&
        tmp_path[0] != '\0' && final_prefix[0] != '\0') {
        int n = snprintf(final_path,
                         sizeof(final_path),
                         "%s_%03d_0.mp4",
                         final_prefix,
                         duration_sec);
        if (n > 0 && n < (int)sizeof(final_path)) {
            (void)remove(final_path);
            if (rename(tmp_path, final_path) != 0) {
                printf("MiIpc SD storage rename failed tmp=%s final=%s errno=%d\n",
                       tmp_path,
                       final_path,
                       errno);
            } else {
                printf("MiIpc SD storage close mp4 path=%s duration=%d samples=%d\n",
                       final_path,
                       duration_sec,
                       sample_count);
            }
            fflush(stdout);
        }
    } else if (available && tmp_path[0] != '\0') {
        (void)remove(tmp_path);
    }

    sd_storage_reset_segment_runtime(segment);
    if (!keep_codec_state) {
        sd_storage_reset_segment_codec(segment);
    }
}

static void sd_storage_release_segment(miipc_sd_storage_segment_t* segment)
{
    if (segment == NULL) {
        return;
    }
    sd_storage_close_segment(segment, 0);
    memset(segment, 0, sizeof(*segment));
    segment->chn = -1;
    segment->codec = CI_UnDef;
    segment->video_track = -1;
    segment->audio_track = -1;
}

static void sd_storage_close_segments(void)
{
    int i;

    for (i = 0; i < MIIPC_SD_STORAGE_MAX_CHANNELS; ++i) {
        sd_storage_release_segment(&g_sd_storage.segments[i]);
    }
}

static miipc_sd_storage_segment_t* sd_storage_get_segment(int chn, int codec)
{
    int index;
    miipc_sd_storage_segment_t* segment;

    if (chn < 0) {
        chn = 0;
    }
    index = chn % MIIPC_SD_STORAGE_MAX_CHANNELS;
    segment = &g_sd_storage.segments[index];

    if (!segment->in_use || segment->chn != chn) {
        sd_storage_release_segment(segment);
        segment->in_use = 1;
        segment->chn = chn;
        segment->codec = CI_UnDef;
    }

    if (segment->codec != CI_UnDef && segment->codec != codec) {
        sd_storage_close_segment(segment, 0);
    }
    if (segment->codec == CI_UnDef) {
        segment->codec = codec;
    }

    return segment;
}

static int sd_storage_has_codec_config(const miipc_sd_storage_segment_t* segment)
{
    if (segment == NULL) {
        return 0;
    }
    if (segment->codec == CI_H264) {
        return segment->sps_len > 0 && segment->pps_len > 0;
    }
    if (segment->codec == CI_H265) {
        return segment->vps_len > 0 && segment->sps_len > 0 && segment->pps_len > 0;
    }
    return 0;
}

static int sd_storage_write_cached_codec_config(miipc_sd_storage_segment_t* segment)
{
    int ret;

    if (segment == NULL || segment->writer == NULL || segment->video_track < 0) {
        return LIBMP4_ERR_ARG;
    }
    if (segment->codec == CI_H264) {
        ret = libmp4_write_h264_annexb_track(segment->writer,
                                             segment->video_track,
                                             segment->sps,
                                             segment->sps_len,
                                             0,
                                             0);
        if (ret != LIBMP4_OK) {
            return ret;
        }
        return libmp4_write_h264_annexb_track(segment->writer,
                                              segment->video_track,
                                              segment->pps,
                                              segment->pps_len,
                                              0,
                                              0);
    }
    if (segment->codec == CI_H265) {
        ret = libmp4_write_h265_annexb_track(segment->writer,
                                             segment->video_track,
                                             segment->vps,
                                             segment->vps_len,
                                             0,
                                             0);
        if (ret != LIBMP4_OK) {
            return ret;
        }
        ret = libmp4_write_h265_annexb_track(segment->writer,
                                             segment->video_track,
                                             segment->sps,
                                             segment->sps_len,
                                             0,
                                             0);
        if (ret != LIBMP4_OK) {
            return ret;
        }
        return libmp4_write_h265_annexb_track(segment->writer,
                                              segment->video_track,
                                              segment->pps,
                                              segment->pps_len,
                                              0,
                                              0);
    }
    return LIBMP4_ERR_ARG;
}

static int sd_storage_open_segment(miipc_sd_storage_segment_t* segment,
                                   const miipc_sd_storage_node_t* node)
{
    int width;
    int height;
    int video_max_samples;
    int audio_max_samples;
    int frame_duration_ms;
    int ret;
    libmp4_config_t cfg;

    if (segment == NULL || node == NULL || !sd_storage_has_codec_config(segment)) {
        return -1;
    }
    if (sd_storage_build_segment_paths(segment) != 0) {
        return -1;
    }

    segment->width = node->width > 0 ? node->width : MIIPC_SD_STORAGE_MIN_WIDTH;
    segment->height = node->height > 0 ? node->height : MIIPC_SD_STORAGE_MIN_HEIGHT;
    segment->frame_rate = (node->frame_rate > 0 && node->frame_rate <= 120) ?
        node->frame_rate : MIIPC_SD_STORAGE_DEFAULT_FPS;
    width = segment->width;
    height = segment->height;
    frame_duration_ms = sd_storage_frame_duration_ms(segment->frame_rate);
    video_max_samples = (MIIPC_SD_STORAGE_SEGMENT_MS / frame_duration_ms) + 32;
    audio_max_samples = (MIIPC_SD_STORAGE_SEGMENT_MS / 20) + 64;

    memset(&cfg, 0, sizeof(cfg));
    cfg.path = segment->tmp_path;
    cfg.write_cache_size = MIIPC_SD_STORAGE_WRITE_CACHE_SIZE;
    cfg.max_tracks = MIIPC_SD_STORAGE_MP4_MAX_TRACKS;
    cfg.max_samples = (uint32_t)audio_max_samples;
    cfg.max_file_bytes = 0;

    segment->writer = libmp4_open(&cfg);
    if (segment->writer == NULL) {
        printf("MiIpc SD storage open tmp failed path=%s errno=%d\n", segment->tmp_path, errno);
        fflush(stdout);
        (void)remove(segment->tmp_path);
        sd_storage_reset_segment_runtime(segment);
        return -1;
    }

    if (segment->codec == CI_H265) {
        segment->video_track = libmp4_add_h265_track(segment->writer,
                                                     (uint32_t)width,
                                                     (uint32_t)height,
                                                     (uint32_t)segment->frame_rate,
                                                     1,
                                                     (uint32_t)video_max_samples);
    } else {
        segment->video_track = libmp4_add_h264_track(segment->writer,
                                                     (uint32_t)width,
                                                     (uint32_t)height,
                                                     (uint32_t)segment->frame_rate,
                                                     1,
                                                     (uint32_t)video_max_samples);
    }
    segment->audio_track = libmp4_add_g711a_track(segment->writer,
                                                  MIIPC_SD_STORAGE_G711A_SAMPLE_RATE,
                                                  MIIPC_SD_STORAGE_G711A_CHANNELS,
                                                  (uint32_t)audio_max_samples);
    if (segment->video_track < 0) {
        printf("MiIpc SD storage add video track failed path=%s ret=%d\n",
               segment->tmp_path,
               segment->video_track);
        fflush(stdout);
        sd_storage_close_segment(segment, 1);
        return -1;
    }
    if (segment->audio_track < 0) {
        printf("MiIpc SD storage add G711 audio track failed path=%s\n",
               segment->tmp_path);
        fflush(stdout);
        sd_storage_close_segment(segment, 1);
        return -1;
    }

    ret = sd_storage_write_cached_codec_config(segment);
    if (ret != LIBMP4_OK) {
        printf("MiIpc SD storage write cached codec config failed path=%s ret=%d(%s)\n",
               segment->tmp_path,
               ret,
               libmp4_error_text(ret));
        fflush(stdout);
        sd_storage_close_segment(segment, 1);
        return -1;
    }

    printf("MiIpc SD storage open mp4 tmp=%s codec=%d size=%dx%d fps=%d\n",
           segment->tmp_path,
           segment->codec,
           segment->width,
           segment->height,
           segment->frame_rate);
    fflush(stdout);
    return 0;
}

static int sd_storage_next_video_duration_ms(miipc_sd_storage_segment_t* segment,
                                             const miipc_sd_storage_node_t* node)
{
    int64_t delta_ms;
    int expected_ms;

    if (segment == NULL || node == NULL) {
        return 0;
    }
    expected_ms = sd_storage_frame_duration_ms(node->frame_rate > 0 ? node->frame_rate : segment->frame_rate);

    if (!segment->have_last_src_pts) {
        segment->have_last_src_pts = 1;
        segment->last_src_pts = node->pts;
        segment->video_ms += expected_ms;
        return expected_ms;
    }

    delta_ms = expected_ms;
    if (node->pts > segment->last_src_pts) {
        uint64_t raw_delta = node->pts - segment->last_src_pts;
        if (raw_delta > 0 && raw_delta <= 1000) {
            delta_ms = (int64_t)raw_delta;
        } else if (raw_delta >= 90 && raw_delta / 90 <= 1000) {
            delta_ms = (int64_t)((raw_delta + 45) / 90);
        } else if (raw_delta >= 1000 && raw_delta / 1000 <= 1000) {
            delta_ms = (int64_t)((raw_delta + 500) / 1000);
        }
    }

    if (delta_ms <= 0 || delta_ms > 1000) {
        delta_ms = expected_ms;
    }
    segment->last_src_pts = node->pts;
    segment->video_ms += delta_ms;
    return (int)delta_ms;
}

static int sd_storage_write_video_node(const miipc_sd_storage_node_t* node)
{
    miipc_sd_storage_segment_t* segment;
    miipc_sd_storage_video_scan_t scan;
    int key_frame;
    int duration_ms;
    int flags;
    int ret;

    if (node == NULL || node->data == NULL || node->len <= 0 || !sd_storage_is_video_codec(node->codec)) {
        return 0;
    }

    segment = sd_storage_get_segment(node->chn, node->codec);
    if (segment == NULL) {
        return -1;
    }

    memset(&scan, 0, sizeof(scan));
    sd_storage_scan_video_annexb(segment, node, &scan);
    key_frame = (scan.key_frame || node->key_frame != 0) ? 1 : 0;
    if (segment->writer != NULL &&
        ((scan.config_update && key_frame) ||
         (key_frame && segment->video_ms >= MIIPC_SD_STORAGE_SEGMENT_MS))) {
        sd_storage_close_segment(segment, 1);
    }

    if (!scan.has_vcl) {
        return 0;
    }
    if (segment->writer == NULL) {
        if (!key_frame) {
            return 0;
        }
        if (!sd_storage_has_codec_config(segment)) {
            printf("MiIpc SD storage wait codec config codec=%d chn=%d len=%d\n",
                   node->codec,
                   node->chn,
                   node->len);
            fflush(stdout);
            return 0;
        }
        if (sd_storage_open_segment(segment, node) != 0) {
            return -1;
        }
    }

    duration_ms = sd_storage_next_video_duration_ms(segment, node);
    flags = key_frame ? LIBMP4_FLAG_KEYFRAME : 0;
    if (node->codec == CI_H265) {
        ret = libmp4_write_h265_annexb_track(segment->writer,
                                             segment->video_track,
                                             node->data,
                                             (size_t)node->len,
                                             (uint32_t)duration_ms,
                                             (uint32_t)flags);
    } else {
        ret = libmp4_write_h264_annexb_track(segment->writer,
                                             segment->video_track,
                                             node->data,
                                             (size_t)node->len,
                                             (uint32_t)duration_ms,
                                             (uint32_t)flags);
    }
    if (ret != LIBMP4_OK) {
        printf("MiIpc SD storage mp4 write video failed path=%s len=%d duration=%d ret=%d(%s)\n",
               segment->tmp_path,
               node->len,
               duration_ms,
               ret,
               libmp4_error_text(ret));
        fflush(stdout);
        sd_storage_close_segment(segment, 1);
        return -1;
    }
    segment->sample_count++;
    return 0;
}

static int sd_storage_audio_to_video_chn(int chn)
{
    /* Audio channel 100+n belongs to video channel n. */
    if (chn >= 100) {
        return chn - 100;
    }
    return chn;
}

static miipc_sd_storage_segment_t* sd_storage_find_audio_segment(int audio_chn)
{
    int i;
    int video_chn = sd_storage_audio_to_video_chn(audio_chn);

    for (i = 0; i < MIIPC_SD_STORAGE_MAX_CHANNELS; ++i) {
        miipc_sd_storage_segment_t* segment = &g_sd_storage.segments[i];
        if (segment->writer != NULL && segment->audio_track >= 0) {
            if (segment->chn == video_chn) {
                return segment;
            }
        }
    }
    return NULL;
}

static int sd_storage_write_audio_node(const miipc_sd_storage_node_t* node)
{
    miipc_sd_storage_segment_t* segment;
    int duration_ms;
    int ret;

    if (node == NULL || node->data == NULL || node->len <= 0) {
        return -1;
    }
    if (node->codec != CI_G711) {
        return 0;
    }
    if (node->frame_rate > 0 &&
        node->frame_rate != MIIPC_SD_STORAGE_G711A_SAMPLE_RATE) {
        printf("MiIpc SD storage drop G711 audio sample_rate=%d expect=%d len=%d\n",
               node->frame_rate,
               MIIPC_SD_STORAGE_G711A_SAMPLE_RATE,
               node->len);
        fflush(stdout);
        return -1;
    }

    segment = sd_storage_find_audio_segment(node->chn);
    if (segment == NULL) {
        return 0;
    }

    duration_ms = (node->len * 1000) /
                  (MIIPC_SD_STORAGE_G711A_SAMPLE_RATE *
                   MIIPC_SD_STORAGE_G711A_CHANNELS);
    if (duration_ms <= 0) {
        duration_ms = 20;
    }

    ret = libmp4_write_g711_track(segment->writer,
                                  segment->audio_track,
                                  node->data,
                                  (size_t)node->len,
                                  (uint32_t)duration_ms);
    if (ret != LIBMP4_OK) {
        printf("MiIpc SD storage mp4 write audio failed path=%s len=%d duration=%d ret=%d(%s)\n",
               segment->tmp_path,
               node->len,
               duration_ms,
               ret,
               libmp4_error_text(ret));
        fflush(stdout);
        return -1;
    }

    segment->audio_ms += duration_ms;
    return 0;
}

static int sd_storage_write_node(const miipc_sd_storage_node_t* node)
{
    if (node == NULL || node->data == NULL || node->len <= 0) {
        return -1;
    }
    if (sd_storage_is_video_codec(node->codec)) {
        return sd_storage_write_video_node(node);
    }
    return sd_storage_write_audio_node(node);
}

static void* sd_storage_thread_main(void* arg)
{
    (void)arg;

    while (1) {
        miipc_sd_storage_node_t* node;
        int flush_request;
        int stopping;

        mi_SemaphoreWait(g_sd_storage.sem, MIIPC_SD_STORAGE_WAIT_MS);

        sd_storage_lock();
        flush_request = g_sd_storage.flush_request;
        if (flush_request) {
            g_sd_storage.flush_request = 0;
        }
        stopping = g_sd_storage.stopping;
        if (stopping && g_sd_storage.head == NULL) {
            sd_storage_unlock();
            break;
        }
        node = flush_request ? NULL : sd_storage_pop_nolock();
        sd_storage_unlock();

        if (flush_request) {
            sd_storage_clear_queue();
            sd_storage_close_segments();
            continue;
        }
        if (node == NULL) {
            continue;
        }

        if (!sd_storage_is_available()) {
            sd_storage_lock();
            g_sd_storage.dropped++;
            sd_storage_unlock();
            sd_storage_free_node(node);
            continue;
        }

        if (sd_storage_write_node(node) == 0) {
            sd_storage_lock();
            g_sd_storage.written++;
            sd_storage_unlock();
        } else {
            sd_storage_lock();
            g_sd_storage.write_failed++;
            sd_storage_unlock();
        }
        sd_storage_free_node(node);
    }

    sd_storage_close_segments();
    return NULL;
}

int miipc_sd_storage_start(const char* root_path, int queue_limit)
{
    thread_config_t thread_config;
    int i;

    if (queue_limit <= 0) {
        queue_limit = MIIPC_SD_STORAGE_DEFAULT_QUEUE_LIMIT;
    }

    if (g_sd_storage.lock == NULL) {
        g_sd_storage.lock = mi_CreateMutex(1);
        if (g_sd_storage.lock == NULL) {
            return -1;
        }
    }
    if (g_sd_storage.sem == NULL) {
        g_sd_storage.sem = mi_CreateSemaphore(0, queue_limit);
        if (g_sd_storage.sem == NULL) {
            return -1;
        }
    }

    sd_storage_lock();
    if (g_sd_storage.started) {
        sd_storage_unlock();
        return 0;
    }

    memset(g_sd_storage.segments, 0, sizeof(g_sd_storage.segments));
    for (i = 0; i < MIIPC_SD_STORAGE_MAX_CHANNELS; ++i) {
        g_sd_storage.segments[i].chn = -1;
        g_sd_storage.segments[i].codec = CI_UnDef;
        g_sd_storage.segments[i].video_track = -1;
        g_sd_storage.segments[i].audio_track = -1;
    }
    snprintf(g_sd_storage.root,
             sizeof(g_sd_storage.root),
             "%s",
             (root_path != NULL && root_path[0] != '\0') ? root_path : "sd");
    g_sd_storage.stopping = 0;
    g_sd_storage.enabled = 1;
    if (!g_sd_storage.availability_configured) {
        g_sd_storage.available = 1;
    }
    g_sd_storage.flush_request = 0;
    g_sd_storage.queue_count = 0;
    g_sd_storage.queue_limit = queue_limit;
    g_sd_storage.posted = 0;
    g_sd_storage.written = 0;
    g_sd_storage.dropped = 0;
    g_sd_storage.write_failed = 0;
    sd_storage_unlock();

    memset(&thread_config, 0, sizeof(thread_config));
    thread_config.name = "miipc_sd_storage";
    thread_config.stack_size = 8192;
    thread_config.detached = 0;
    if (mi_thread_create(&g_sd_storage.thread, sd_storage_thread_main, NULL, &thread_config) != MI_THREAD_OK) {
        return -1;
    }

    sd_storage_lock();
    g_sd_storage.started = 1;
    sd_storage_unlock();
    printf("MiIpc SD storage started root=%s queue_limit=%d\n", g_sd_storage.root, queue_limit);
    fflush(stdout);
    return 0;
}

void miipc_sd_storage_stop(void)
{
    int started;

    sd_storage_lock();
    started = g_sd_storage.started;
    g_sd_storage.stopping = 1;
    sd_storage_unlock();

    if (!started) {
        sd_storage_clear_queue();
        sd_storage_close_segments();
        return;
    }

    if (g_sd_storage.sem != NULL) {
        mi_SemaphorePost(g_sd_storage.sem, 1);
    }
    mi_thread_join(&g_sd_storage.thread, NULL, 3000);

    sd_storage_lock();
    g_sd_storage.started = 0;
    sd_storage_unlock();
    sd_storage_clear_queue();
    sd_storage_close_segments();
}

int miipc_sd_storage_set_enabled(int enabled)
{
    int next_enabled = enabled ? 1 : 0;

    sd_storage_lock();
    if (!g_sd_storage.started) {
        g_sd_storage.enabled = next_enabled;
        sd_storage_unlock();
        return 0;
    }
    g_sd_storage.enabled = next_enabled;
    if (!next_enabled) {
        g_sd_storage.flush_request = 1;
    }
    sd_storage_unlock();

    if (!next_enabled && g_sd_storage.sem != NULL) {
        mi_SemaphorePost(g_sd_storage.sem, 1);
    }
    printf("MiIpc SD storage enabled=%d\n", next_enabled);
    fflush(stdout);
    return 0;
}

int miipc_sd_storage_set_available(int available)
{
    int next_available = available ? 1 : 0;
    int notify = 0;

    sd_storage_lock();
    g_sd_storage.availability_configured = 1;
    g_sd_storage.available = next_available;
    if (g_sd_storage.started && !next_available) {
        g_sd_storage.flush_request = 1;
        notify = 1;
    }
    sd_storage_unlock();

    if (notify && g_sd_storage.sem != NULL) {
        mi_SemaphorePost(g_sd_storage.sem, 1);
    }
    printf("MiIpc SD storage available=%d\n", next_available);
    fflush(stdout);
    return 0;
}

int miipc_sd_storage_is_enabled(void)
{
    int enabled;

    sd_storage_lock();
    enabled = g_sd_storage.enabled;
    sd_storage_unlock();
    return enabled;
}

int miipc_sd_storage_post(int chn, const struct Stream_Info* info, int count)
{
    int i;
    int accepted = 0;
    int started;
    int enabled;
    int available;
    int stopping;

    if (info == NULL || count <= 0) {
        return -1;
    }
    sd_storage_lock();
    started = g_sd_storage.started;
    enabled = g_sd_storage.enabled;
    available = g_sd_storage.available;
    sd_storage_unlock();
    if (!started) {
        return -1;
    }
    if (!enabled) {
        return 0;
    }
    if (!available) {
        sd_storage_lock();
        g_sd_storage.dropped += (uint32_t)count;
        sd_storage_unlock();
        return 0;
    }

    for (i = 0; i < count; ++i) {
        const struct Stream_Info* src = &info[i];
        miipc_sd_storage_node_t* node;

        if (src->buffer == NULL || src->buflen <= 0) {
            continue;
        }
        if (src->buflen > MIIPC_SD_STORAGE_MAX_PACKET_SIZE) {
            sd_storage_lock();
            g_sd_storage.dropped++;
            sd_storage_unlock();
            continue;
        }

        node = (miipc_sd_storage_node_t*)mi_mem_malloc((int)sizeof(*node));
        if (node == NULL) {
            return accepted > 0 ? accepted : -1;
        }
        memset(node, 0, sizeof(*node));
        node->data = (uint8_t*)mi_mem_malloc_large(src->buflen);
        if (node->data == NULL) {
            mi_mem_free(node);
            return accepted > 0 ? accepted : -1;
        }

        memcpy(node->data, src->buffer, (size_t)src->buflen);
        node->chn = chn;
        node->codec = src->enType;
        node->width = src->width;
        node->height = src->height;
        node->frame_rate = src->nFrameRate;
        node->len = src->buflen;
        node->pts = src->pts;
        node->key_frame = src->Ikey;

        sd_storage_lock();
        stopping = g_sd_storage.stopping;
        if (stopping || !g_sd_storage.available || !g_sd_storage.enabled) {
            if (!g_sd_storage.available) {
                g_sd_storage.dropped++;
            }
            sd_storage_unlock();
            sd_storage_free_node(node);
            return stopping && accepted == 0 ? -1 : 0;
        }
        if (g_sd_storage.queue_count >= g_sd_storage.queue_limit) {
            sd_storage_drop_oldest_nolock();
        }
        if (g_sd_storage.tail != NULL) {
            g_sd_storage.tail->next = node;
        } else {
            g_sd_storage.head = node;
        }
        g_sd_storage.tail = node;
        g_sd_storage.queue_count++;
        g_sd_storage.posted++;
        sd_storage_unlock();

        if (g_sd_storage.sem != NULL) {
            mi_SemaphorePost(g_sd_storage.sem, 1);
        }
        accepted++;
    }

    return accepted > 0 ? 0 : -1;
}

void miipc_sd_storage_get_stats(miipc_sd_storage_stats_t* stats)
{
    if (stats == NULL) {
        return;
    }

    sd_storage_lock();
    stats->started = g_sd_storage.started;
    stats->enabled = g_sd_storage.enabled;
    stats->available = g_sd_storage.available;
    stats->queue_count = g_sd_storage.queue_count;
    stats->queue_limit = g_sd_storage.queue_limit;
    stats->posted = g_sd_storage.posted;
    stats->written = g_sd_storage.written;
    stats->dropped = g_sd_storage.dropped;
    stats->write_failed = g_sd_storage.write_failed;
    sd_storage_unlock();
}

int miipc_sd_storage_get_info(char* out, int out_len)
{
    int n;
    miipc_sd_storage_stats_t stats;

    if (out == NULL || out_len <= 0) {
        return 0;
    }

    memset(&stats, 0, sizeof(stats));
    miipc_sd_storage_get_stats(&stats);
    n = snprintf(out,
                 (size_t)out_len,
                 "sd_storage started=%d enabled=%d available=%d queue=%d limit=%d posted=%u written=%u dropped=%u write_failed=%u\n",
                 stats.started,
                 stats.enabled,
                 stats.available,
                 stats.queue_count,
                 stats.queue_limit,
                 stats.posted,
                 stats.written,
                 stats.dropped,
                 stats.write_failed);
    if (n < 0) {
        out[0] = '\0';
        return 0;
    }
    return n >= out_len ? out_len - 1 : n;
}
