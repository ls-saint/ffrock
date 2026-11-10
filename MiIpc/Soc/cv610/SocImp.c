#include "SocImp.h"

#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>

#include "higpio.h"
#include "mibase.h"
#include "mithread.h"
#include "micv610osd.h"
#include "sample_comm.h"

#define MI_CV610_MAX_VENC 2
#define MI_CV610_MAIN_CHN 0
#define MI_CV610_SUB_CHN 1
#define MI_CV610_DEFAULT_FPS 15
#define MI_CV610_DEFAULT_BITRATE_KBPS 1200
#define MI_CV610_MIN_BITRATE_KBPS 100
#define MI_CV610_MAX_BITRATE_KBPS 4096
#define MI_CV610_MPP_READY_FILE "/tmp/mppd"
#define MI_CV610_STREAM_LOG_INTERVAL 150
#define MI_CV610_H264_PARAMSET_MAX 2048

#ifndef MI_CV610_REBOOT_CMD
#define MI_CV610_REBOOT_CMD "sync; reboot -f"
#endif

typedef struct mi_cv610_venc_s {
    int chn;
    int vpss_grp;
    int vpss_chn;
    int started;
    volatile int running;
    int width;
    int height;
    int frame_rate;
    int max_bitrate_kbps;
    int cur_bitrate_kbps;
    hi_payload_type payload;
    mi_thread_t thread;
    uint8_t* frame_buf;
    int frame_capacity;
    uint8_t h264_paramset[MI_CV610_H264_PARAMSET_MAX];
    int h264_paramset_len;
    uint32_t frame_count;
    uint32_t keyframe_count;
    uint32_t last_log_frame_count;
} mi_cv610_venc_t;

typedef struct mi_cv610_soc_s {
    struct IHardSoc ISoc;
    FpOnAVStream cllAV;
    FpOnGetAudio readAudio;
    struct SVideoCfg vcfg;
    struct SCaptureCfg ccfg;
    mi_cv610_venc_t venc[MI_CV610_MAX_VENC];
    mi_cv610_osd_t osd[MI_CV610_MAX_VENC];
    int mpp_started;
    int mpp_sys_attached;
    int hard_started;
} mi_cv610_soc_t;

static mi_cv610_soc_t* G_pHardSoc = NULL;

static int cv610_clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void cv610_default_video_cfg(struct SVideoCfg* cfg)
{
    if (cfg == NULL) {
        return;
    }

    if (cfg->chn1PicWh == HV_UnDef || cfg->chn1PicWh == 0) {
        cfg->chn1PicWh = H1920_1080;
    }
    if (cfg->chn1MaxFrameRate <= 0) {
        cfg->chn1MaxFrameRate = MI_CV610_DEFAULT_FPS;
    }
    if (cfg->chn1MaxCodeRate <= 0) {
        cfg->chn1MaxCodeRate = MI_CV610_DEFAULT_BITRATE_KBPS;
    }
    cfg->chn1MaxFrameRate = cv610_clamp_int(cfg->chn1MaxFrameRate, 5, 30);
    cfg->chn1MaxCodeRate = cv610_clamp_int(cfg->chn1MaxCodeRate,
        MI_CV610_MIN_BITRATE_KBPS, MI_CV610_MAX_BITRATE_KBPS);
    cfg->chn1Encoder = CI_H264;

    if (cfg->chn2PicWh == HV_UnDef || cfg->chn2PicWh == 0) {
        cfg->chn2PicWh = H640_360;
    }
    if (cfg->chn2FrameRate <= 0) {
        cfg->chn2FrameRate = 5;
    }
    if (cfg->chn2CodeRate <= 0) {
        cfg->chn2CodeRate = 512;
    }
    cfg->chn2Encoder = CI_H264;
}

static void cv610_pic_size(enum EVideoPicWh pic, int* width, int* height)
{
    int w = 1920;
    int h = 1080;

    if (pic == H1280_720) {
        w = 1280;
        h = 720;
    } else if (pic == H640_360) {
        w = 640;
        h = 360;
    }

    if (width != NULL) {
        *width = w;
    }
    if (height != NULL) {
        *height = h;
    }
}

static int cv610_payload_to_codec(hi_payload_type payload)
{
    if (payload == HI_PT_H264) {
        return CI_H264;
    }
    if (payload == HI_PT_H265) {
        return CI_H265;
    }
    return CI_UnDef;
}

static int cv610_find_start_code(const uint8_t* data, int len, int offset, int* code_len)
{
    int i;

    if (data == NULL || code_len == NULL || len < 4) {
        return -1;
    }

    for (i = offset; i + 3 < len; ++i) {
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            *code_len = 3;
            return i;
        }
        if (i + 4 <= len &&
            data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 && data[i + 3] == 1) {
            *code_len = 4;
            return i;
        }
    }

    return -1;
}

static int cv610_is_keyframe(const uint8_t* data, int len, hi_payload_type payload)
{
    int pos = 0;

    if (data == NULL || len <= 0) {
        return 0;
    }

    for (;;) {
        int code_len = 0;
        int start = cv610_find_start_code(data, len, pos, &code_len);
        int nal_pos;

        if (start < 0) {
            nal_pos = pos == 0 ? 0 : -1;
        } else {
            nal_pos = start + code_len;
        }
        if (nal_pos < 0 || nal_pos >= len) {
            break;
        }

        if (payload == HI_PT_H264) {
            int nal_type = data[nal_pos] & 0x1f;
            if (nal_type == 5) {
                return 1;
            }
        } else if (payload == HI_PT_H265 && nal_pos + 1 < len) {
            int nal_type = (data[nal_pos] >> 1) & 0x3f;
            if (nal_type == 19 || nal_type == 20) {
                return 1;
            }
        }

        if (start < 0) {
            break;
        }
        pos = nal_pos + 1;
    }

    return 0;
}

static int cv610_stream_is_keyframe(const mi_cv610_venc_t* venc, const hi_venc_stream* stream)
{
    if (venc == NULL || stream == NULL) {
        return 0;
    }
    if (venc->payload == HI_PT_H264 && stream->h264_info.ref_type == HI_VENC_BASE_IDR_SLICE) {
        return 1;
    }
    if (venc->payload == HI_PT_H265 && stream->h265_info.ref_type == HI_VENC_BASE_IDR_SLICE) {
        return 1;
    }
    return 0;
}

static int cv610_h264_nal_bounds(const uint8_t* data,
    int len,
    int offset,
    int* nal_start,
    int* nal_len,
    int* prefix_start,
    int* prefix_len)
{
    int code_len = 0;
    int start;
    int next_code_len = 0;
    int next;
    int end;

    if (data == NULL || len <= 0 || nal_start == NULL || nal_len == NULL ||
        prefix_start == NULL || prefix_len == NULL) {
        return -1;
    }

    start = cv610_find_start_code(data, len, offset, &code_len);
    if (start < 0) {
        if (offset != 0) {
            return -1;
        }
        *prefix_start = 0;
        *prefix_len = 0;
        *nal_start = 0;
        *nal_len = len;
        return 0;
    }

    *prefix_start = start;
    *prefix_len = code_len;
    *nal_start = start + code_len;
    if (*nal_start >= len) {
        return -1;
    }

    next = cv610_find_start_code(data, len, *nal_start + 1, &next_code_len);
    (void)next_code_len;
    end = next >= 0 ? next : len;
    while (end > *nal_start && data[end - 1] == 0) {
        --end;
    }
    *nal_len = end - *nal_start;
    return *nal_len > 0 ? 0 : -1;
}

static int cv610_h264_frame_has_paramset(const uint8_t* data, int len)
{
    int offset = 0;

    while (offset < len) {
        int nal_start = 0;
        int nal_len = 0;
        int prefix_start = 0;
        int prefix_len = 0;
        int nal_type;

        if (cv610_h264_nal_bounds(data, len, offset,
                &nal_start, &nal_len, &prefix_start, &prefix_len) != 0) {
            break;
        }
        nal_type = data[nal_start] & 0x1f;
        if (nal_type == 7 || nal_type == 8) {
            return 1;
        }
        offset = nal_start + nal_len;
        if (prefix_len == 0) {
            break;
        }
    }

    return 0;
}

static void cv610_update_h264_paramset(mi_cv610_venc_t* venc, const uint8_t* data, int len)
{
    uint8_t cache[MI_CV610_H264_PARAMSET_MAX];
    int cache_len = 0;
    int have_sps = 0;
    int have_pps = 0;
    int offset = 0;

    if (venc == NULL || data == NULL || len <= 0) {
        return;
    }

    while (offset < len) {
        int nal_start = 0;
        int nal_len = 0;
        int prefix_start = 0;
        int prefix_len = 0;
        int nal_type;
        int copy_start;
        int copy_len;

        if (cv610_h264_nal_bounds(data, len, offset,
                &nal_start, &nal_len, &prefix_start, &prefix_len) != 0) {
            break;
        }

        nal_type = data[nal_start] & 0x1f;
        if (nal_type == 7 || nal_type == 8) {
            copy_start = prefix_len > 0 ? prefix_start : nal_start;
            copy_len = prefix_len > 0 ? (nal_start + nal_len - prefix_start) : nal_len;
            if (prefix_len == 0) {
                if (cache_len + 4 + copy_len <= (int)sizeof(cache)) {
                    cache[cache_len++] = 0;
                    cache[cache_len++] = 0;
                    cache[cache_len++] = 0;
                    cache[cache_len++] = 1;
                    memcpy(cache + cache_len, data + copy_start, (size_t)copy_len);
                    cache_len += copy_len;
                }
            } else if (cache_len + copy_len <= (int)sizeof(cache)) {
                memcpy(cache + cache_len, data + copy_start, (size_t)copy_len);
                cache_len += copy_len;
            }
            if (nal_type == 7) {
                have_sps = 1;
            } else {
                have_pps = 1;
            }
        }

        offset = nal_start + nal_len;
        if (prefix_len == 0) {
            break;
        }
    }

    if (have_sps && have_pps && cache_len > 0) {
        memcpy(venc->h264_paramset, cache, (size_t)cache_len);
        venc->h264_paramset_len = cache_len;
    }
}

static int cv610_ensure_frame_buf(mi_cv610_venc_t* venc, int bytes)
{
    uint8_t* next_buf;

    if (venc == NULL || bytes <= 0) {
        return -1;
    }
    if (venc->frame_capacity >= bytes) {
        return 0;
    }

    next_buf = (uint8_t*)realloc(venc->frame_buf, (size_t)bytes);
    if (next_buf == NULL) {
        return -1;
    }
    venc->frame_buf = next_buf;
    venc->frame_capacity = bytes;
    return 0;
}

static uint32_t cv610_pts_to_rtp_timestamp(uint64_t pts)
{
    return (uint32_t)((pts / 100u) * 9u);
}

static int cv610_file_exists(const char* path)
{
    return path != NULL && access(path, F_OK) == 0;
}

static int cv610_read_mpp_pid(void)
{
    FILE* fp;
    long pid = -1;

    fp = fopen(MI_CV610_MPP_READY_FILE, "r");
    if (fp == NULL) {
        return -1;
    }
    if (fscanf(fp, "%ld", &pid) != 1) {
        pid = -1;
    }
    fclose(fp);

    if (pid <= 1) {
        return -1;
    }
    return (int)pid;
}

static int cv610_is_pid_zombie(int pid)
{
    char path[64];
    char stat_buf[256];
    char* end_name;
    FILE* fp;

    if (pid <= 1) {
        return 1;
    }
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    fp = fopen(path, "r");
    if (fp == NULL) {
        return 1;
    }
    if (fgets(stat_buf, sizeof(stat_buf), fp) == NULL) {
        fclose(fp);
        return 1;
    }
    fclose(fp);

    end_name = strrchr(stat_buf, ')');
    if (end_name == NULL || end_name[1] != ' ' || end_name[2] == '\0') {
        return 0;
    }
    return end_name[2] == 'Z';
}

static int cv610_is_pid_name(int pid, const char* name)
{
    char path[64];
    char comm[64];
    char* nl;
    FILE* fp;

    if (pid <= 1 || name == NULL) {
        return 0;
    }
    if (cv610_is_pid_zombie(pid)) {
        return 0;
    }
    snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    fp = fopen(path, "r");
    if (fp == NULL) {
        return 0;
    }
    if (fgets(comm, sizeof(comm), fp) == NULL) {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    nl = strchr(comm, '\n');
    if (nl != NULL) {
        *nl = '\0';
    }
    return strcmp(comm, name) == 0;
}

static int cv610_write_mpp_pid(int pid)
{
    FILE* fp;

    if (pid <= 1) {
        return -1;
    }
    fp = fopen(MI_CV610_MPP_READY_FILE, "w");
    if (fp == NULL) {
        return -1;
    }
    fprintf(fp, "%d\n", pid);
    fclose(fp);
    return 0;
}

static int cv610_find_mpp_process(void)
{
    DIR* dir;
    struct dirent* ent;

    dir = opendir("/proc");
    if (dir == NULL) {
        return -1;
    }
    while ((ent = readdir(dir)) != NULL) {
        char* end = NULL;
        long pid = strtol(ent->d_name, &end, 10);

        if (end == ent->d_name || *end != '\0' || pid <= 1) {
            continue;
        }
        if (cv610_is_pid_name((int)pid, "mpp") &&
            (kill((pid_t)pid, 0) == 0 || errno == EPERM)) {
            closedir(dir);
            return (int)pid;
        }
    }
    closedir(dir);
    return -1;
}

static int cv610_reuse_existing_mpp(void)
{
    int pid = cv610_find_mpp_process();

    if (pid <= 1) {
        return 0;
    }
    (void)cv610_write_mpp_pid(pid);
    printf("CV610 reuse existing mpp pid=%d\n", pid);
    return 1;
}

static int cv610_is_mpp_alive(int allow_scan)
{
    int pid;

    if (!cv610_file_exists(MI_CV610_MPP_READY_FILE)) {
        return allow_scan ? cv610_reuse_existing_mpp() : 0;
    }

    pid = cv610_read_mpp_pid();
    if (pid <= 1) {
        unlink(MI_CV610_MPP_READY_FILE);
        return allow_scan ? cv610_reuse_existing_mpp() : 0;
    }

    if ((kill((pid_t)pid, 0) == 0 || errno == EPERM) && cv610_is_pid_name(pid, "mpp")) {
        return 1;
    }

    unlink(MI_CV610_MPP_READY_FILE);
    return allow_scan ? cv610_reuse_existing_mpp() : 0;
}

static int cv610_require_external_mpp(void)
{
    if (cv610_is_mpp_alive(1)) {
        printf("CV610 external mpp is ready\n");
        return 0;
    }

    printf("CV610 external mpp is not running; start mpp before APP\n");
    return -1;
}

static int cv610_attach_mpp_sys(void)
{
    hi_s32 ret;

    if (G_pHardSoc != NULL && G_pHardSoc->mpp_sys_attached) {
        return 0;
    }
    ret = hi_mpi_sys_init();
    if (ret != HI_SUCCESS && ret != HI_ERR_SYS_BUSY) {
        printf("CV610 hi_mpi_sys_init attach failed ret=%#x\n", ret);
        return -1;
    }
    if (G_pHardSoc != NULL) {
        G_pHardSoc->mpp_sys_attached = 1;
    }
    printf("CV610 mpp sys attached ret=%#x\n", ret);
    return 0;
}

static void cv610_dispatch_stream(mi_cv610_venc_t* venc, hi_venc_stream* stream)
{
    struct Stream_Info info;
    int i;
    int total = 0;
    int copied = 0;
    int key = 0;
    int has_paramset = 0;
    int ref_type = -1;
    uint64_t pts = 0;

    if (G_pHardSoc == NULL || G_pHardSoc->cllAV == NULL || venc == NULL || stream == NULL) {
        return;
    }
    if (stream->pack == NULL || stream->pack_cnt <= 0) {
        return;
    }

    for (i = 0; i < (int)stream->pack_cnt; ++i) {
        int len = (int)stream->pack[i].len - (int)stream->pack[i].offset;
        if (len > 0) {
            total += len;
        }
    }
    if (total <= 0 || cv610_ensure_frame_buf(venc, total) != 0) {
        return;
    }

    pts = stream->pack[0].pts;
    key = cv610_stream_is_keyframe(venc, stream);
    if (venc->payload == HI_PT_H264) {
        ref_type = (int)stream->h264_info.ref_type;
    } else if (venc->payload == HI_PT_H265) {
        ref_type = (int)stream->h265_info.ref_type;
    }
    for (i = 0; i < (int)stream->pack_cnt; ++i) {
        uint8_t* src = stream->pack[i].addr + stream->pack[i].offset;
        int len = (int)stream->pack[i].len - (int)stream->pack[i].offset;
        if (src == NULL || len <= 0 || copied + len > total) {
            continue;
        }
        memcpy(venc->frame_buf + copied, src, (size_t)len);
        copied += len;
    }
    if (copied <= 0) {
        return;
    }
    if (!key && cv610_is_keyframe(venc->frame_buf, copied, venc->payload)) {
        key = 1;
    }
    if (venc->payload == HI_PT_H264) {
        has_paramset = cv610_h264_frame_has_paramset(venc->frame_buf, copied);
        if (has_paramset) {
            cv610_update_h264_paramset(venc, venc->frame_buf, copied);
        } else if (key && venc->h264_paramset_len > 0) {
            int next_size = copied + venc->h264_paramset_len;
            if (cv610_ensure_frame_buf(venc, next_size) == 0) {
                memmove(venc->frame_buf + venc->h264_paramset_len,
                    venc->frame_buf,
                    (size_t)copied);
                memcpy(venc->frame_buf,
                    venc->h264_paramset,
                    (size_t)venc->h264_paramset_len);
                copied = next_size;
                has_paramset = 1;
            }
        }
    }

    ++venc->frame_count;
    if (key) {
        ++venc->keyframe_count;
    }
    if (venc->frame_count == 1 ||
        key ||
        (uint32_t)(venc->frame_count - venc->last_log_frame_count) >= MI_CV610_STREAM_LOG_INTERVAL) {
        venc->last_log_frame_count = venc->frame_count;
        printf("CV610 VENC frame chn=%d frames=%u keyframes=%u bytes=%d key=%d ref=%d ps=%d cache=%d pts=%u:%u packs=%u\n",
            venc->chn,
            (unsigned int)venc->frame_count,
            (unsigned int)venc->keyframe_count,
            copied,
            key,
            ref_type,
            has_paramset,
            venc->h264_paramset_len,
            (unsigned int)(pts >> 32),
            (unsigned int)(pts & 0xffffffffu),
            (unsigned int)stream->pack_cnt);
        fflush(stdout);
    }

    memset(&info, 0, sizeof(info));
    info.enType = cv610_payload_to_codec(venc->payload);
    info.width = venc->width;
    info.height = venc->height;
    info.nFrameRate = venc->frame_rate;
    info.buffer = venc->frame_buf;
    info.buflen = copied;
    info.pts = cv610_pts_to_rtp_timestamp(pts);
    info.Ikey = key;
    G_pHardSoc->cllAV(venc->chn, &info, 1);
}

static int cv610_venc_get_one_frame(mi_cv610_venc_t* venc)
{
    hi_s32 ret;
    hi_venc_chn_status stat;
    hi_venc_stream stream;

    if (venc == NULL || !venc->started) {
        return -1;
    }

    memset(&stat, 0, sizeof(stat));
    ret = hi_mpi_venc_query_status((hi_venc_chn)venc->chn, &stat);
    if (ret != HI_SUCCESS) {
        mi_thread_sleep(20);
        return -1;
    }
    if (stat.cur_packs <= 0) {
        mi_thread_sleep(5);
        return 0;
    }

    memset(&stream, 0, sizeof(stream));
    stream.pack = (hi_venc_pack*)malloc(sizeof(hi_venc_pack) * (size_t)stat.cur_packs);
    if (stream.pack == NULL) {
        mi_thread_sleep(20);
        return -1;
    }
    stream.pack_cnt = stat.cur_packs;

    ret = hi_mpi_venc_get_stream((hi_venc_chn)venc->chn, &stream, 1000);
    if (ret == HI_SUCCESS) {
        cv610_dispatch_stream(venc, &stream);
        ret = hi_mpi_venc_release_stream((hi_venc_chn)venc->chn, &stream);
    }

    free(stream.pack);
    return ret == HI_SUCCESS ? 0 : -1;
}

static void* cv610_venc_thread(void* arg)
{
    mi_cv610_venc_t* venc = (mi_cv610_venc_t*)arg;

    while (venc != NULL && venc->running) {
        if (G_pHardSoc != NULL &&
            venc->chn >= 0 &&
            venc->chn < MI_CV610_MAX_VENC) {
            mi_cv610_osd_tick(&G_pHardSoc->osd[venc->chn]);
        }
        cv610_venc_get_one_frame(venc);
    }

    return NULL;
}

static int cv610_set_vpss_size(int grp, int chn, int width, int height, int frame_rate)
{
    hi_vpss_chn_attr attr;
    hi_s32 ret;

    memset(&attr, 0, sizeof(attr));
    ret = hi_mpi_vpss_get_chn_attr((hi_vpss_grp)grp, (hi_vpss_chn)chn, &attr);
    if (ret != HI_SUCCESS) {
        printf("CV610 get VPSS attr failed grp=%d chn=%d ret=%#x\n", grp, chn, ret);
        return -1;
    }

    attr.width = (hi_u32)width;
    attr.height = (hi_u32)height;
    attr.frame_rate.src_frame_rate = frame_rate;
    attr.frame_rate.dst_frame_rate = frame_rate;

    ret = hi_mpi_vpss_set_chn_attr((hi_vpss_grp)grp, (hi_vpss_chn)chn, &attr);
    if (ret != HI_SUCCESS) {
        printf("CV610 set VPSS attr failed grp=%d chn=%d ret=%#x\n", grp, chn, ret);
        return -1;
    }

    return 0;
}

static void cv610_init_venc_param(mi_cv610_venc_t* venc, sample_comm_venc_chn_param* param)
{
    hi_venc_gop_attr gop_attr;

    memset(param, 0, sizeof(*param));
    memset(&gop_attr, 0, sizeof(gop_attr));
    sample_comm_venc_get_gop_attr(HI_VENC_GOP_MODE_NORMAL_P, &gop_attr);

    param->frame_rate = (hi_u32)venc->frame_rate;
    param->gop = (hi_u32)(venc->frame_rate * 2);
    if (param->gop == 0) {
        param->gop = 30;
    }
    param->stats_time = 2;
    param->venc_size.width = (hi_u32)venc->width;
    param->venc_size.height = (hi_u32)venc->height;
    param->size = venc->width >= 1920 ? PIC_1080P : (venc->width >= 1280 ? PIC_720P : PIC_480P);
    param->profile = 0;
    param->is_rcn_ref_share_buf = HI_TRUE;
    param->gop_attr = gop_attr;
    param->type = venc->payload;
    param->rc_mode = SAMPLE_RC_AVBR;
}

static void cv610_apply_bitrate_to_attr(hi_venc_chn_attr* attr, int bitrate_kbps)
{
    hi_u32 rate = (hi_u32)cv610_clamp_int(bitrate_kbps,
        MI_CV610_MIN_BITRATE_KBPS, MI_CV610_MAX_BITRATE_KBPS);

    switch (attr->rc_attr.rc_mode) {
    case HI_VENC_RC_MODE_H264_CBR:
        attr->rc_attr.h264_cbr.bit_rate = rate;
        break;
    case HI_VENC_RC_MODE_H264_VBR:
        attr->rc_attr.h264_vbr.max_bit_rate = rate;
        break;
    case HI_VENC_RC_MODE_H264_AVBR:
        attr->rc_attr.h264_avbr.max_bit_rate = rate;
        break;
    case HI_VENC_RC_MODE_H264_QVBR:
        attr->rc_attr.h264_qvbr.target_bit_rate = rate;
        break;
    case HI_VENC_RC_MODE_H264_CVBR:
        attr->rc_attr.h264_cvbr.max_bit_rate = rate;
        attr->rc_attr.h264_cvbr.long_term_max_bit_rate = rate;
        attr->rc_attr.h264_cvbr.long_term_min_bit_rate = rate / 2;
        break;
    case HI_VENC_RC_MODE_H265_CBR:
        attr->rc_attr.h265_cbr.bit_rate = rate;
        break;
    case HI_VENC_RC_MODE_H265_VBR:
        attr->rc_attr.h265_vbr.max_bit_rate = rate;
        break;
    case HI_VENC_RC_MODE_H265_AVBR:
        attr->rc_attr.h265_avbr.max_bit_rate = rate;
        break;
    case HI_VENC_RC_MODE_H265_QVBR:
        attr->rc_attr.h265_qvbr.target_bit_rate = rate;
        break;
    case HI_VENC_RC_MODE_H265_CVBR:
        attr->rc_attr.h265_cvbr.max_bit_rate = rate;
        attr->rc_attr.h265_cvbr.long_term_max_bit_rate = rate;
        attr->rc_attr.h265_cvbr.long_term_min_bit_rate = rate / 2;
        break;
    default:
        break;
    }
}

static void cv610_venc_force_release(mi_cv610_venc_t* venc, const char* reason)
{
    hi_venc_chn chn;
    hi_venc_chn_status stat;
    hi_s32 ret;

    if (venc == NULL) {
        return;
    }

    chn = (hi_venc_chn)venc->chn;
    venc->running = 0;
    venc->started = 0;
    memset(&stat, 0, sizeof(stat));
    ret = hi_mpi_venc_query_status(chn, &stat);
    if (ret == HI_SUCCESS) {
        printf("CV610 release stale venc chn=%d reason=%s packs=%u\n",
            venc->chn,
            reason != NULL ? reason : "",
            (unsigned int)stat.cur_packs);
    } else {
        printf("CV610 release stale venc chn=%d reason=%s query_ret=%#x\n",
            venc->chn,
            reason != NULL ? reason : "",
            ret);
    }

    (void)sample_comm_vpss_un_bind_venc((hi_vpss_grp)venc->vpss_grp,
        (hi_vpss_chn)venc->vpss_chn,
        chn);
    (void)hi_mpi_venc_stop_chn(chn);
    (void)hi_mpi_venc_destroy_chn(chn);
    mi_thread_sleep(50);
}

static void cv610_reboot_after_venc_create_failed(int chn, hi_s32 ret)
{
    printf("CV610 create venc failed chn=%d ret=%#x, reboot device\n", chn, ret);
    fflush(stdout);
    (void)system(MI_CV610_REBOOT_CMD);
}

static int cv610_venc_start(mi_cv610_venc_t* venc)
{
    sample_comm_venc_chn_param param;
    hi_venc_chn_attr attr;
    hi_venc_start_param start_param;
    thread_config_t thread_cfg;
    hi_s32 ret;

    if (venc == NULL || venc->started) {
        return 0;
    }

    if (cv610_set_vpss_size(venc->vpss_grp, venc->vpss_chn,
            venc->width, venc->height, venc->frame_rate) != 0) {
        return -1;
    }

    cv610_init_venc_param(venc, &param);
    memset(&attr, 0, sizeof(attr));
    ret = sample_comm_venc_channel_param_init(&param, &attr);
    if (ret != HI_SUCCESS) {
        printf("CV610 venc param init failed chn=%d ret=%#x\n", venc->chn, ret);
        return -1;
    }

    cv610_apply_bitrate_to_attr(&attr, venc->max_bitrate_kbps);
    cv610_venc_force_release(venc, "before create");
    ret = hi_mpi_venc_create_chn((hi_venc_chn)venc->chn, &attr);
    if (ret != HI_SUCCESS) {
        cv610_reboot_after_venc_create_failed(venc->chn, ret);
        return -1;
    }

    ret = sample_comm_venc_close_reencode((hi_venc_chn)venc->chn);
    if (ret != HI_SUCCESS) {
        printf("CV610 close reencode failed chn=%d ret=%#x\n", venc->chn, ret);
        hi_mpi_venc_destroy_chn((hi_venc_chn)venc->chn);
        return -1;
    }

    memset(&start_param, 0, sizeof(start_param));
    start_param.recv_pic_num = -1;
    ret = hi_mpi_venc_start_chn((hi_venc_chn)venc->chn, &start_param);
    if (ret != HI_SUCCESS) {
        printf("CV610 start venc failed chn=%d ret=%#x\n", venc->chn, ret);
        hi_mpi_venc_destroy_chn((hi_venc_chn)venc->chn);
        return -1;
    }

    ret = sample_comm_vpss_bind_venc((hi_vpss_grp)venc->vpss_grp,
        (hi_vpss_chn)venc->vpss_chn,
        (hi_venc_chn)venc->chn);
    if (ret != HI_SUCCESS) {
        printf("CV610 bind VPSS->VENC failed chn=%d ret=%#x\n", venc->chn, ret);
        sample_comm_venc_stop((hi_venc_chn)venc->chn);
        return -1;
    }

    venc->cur_bitrate_kbps = venc->max_bitrate_kbps;
    venc->started = 1;
    if (hi_mpi_venc_request_idr((hi_venc_chn)venc->chn, HI_TRUE) != HI_SUCCESS) {
        printf("CV610 initial request IDR failed chn=%d\n", venc->chn);
    }
    venc->running = 1;
    memset(&thread_cfg, 0, sizeof(thread_cfg));
    thread_cfg.name = "cv610_venc";
    thread_cfg.detached = 1;
    thread_cfg.stack_size = 96 * 1024;
    if (mi_thread_create(&venc->thread, cv610_venc_thread, venc, &thread_cfg) != MI_THREAD_OK) {
        printf("CV610 start stream thread failed chn=%d\n", venc->chn);
        venc->running = 0;
        sample_comm_vpss_un_bind_venc((hi_vpss_grp)venc->vpss_grp,
            (hi_vpss_chn)venc->vpss_chn,
            (hi_venc_chn)venc->chn);
        if (G_pHardSoc != NULL &&
            venc->chn >= 0 &&
            venc->chn < MI_CV610_MAX_VENC) {
            mi_cv610_osd_stop(&G_pHardSoc->osd[venc->chn]);
        }
        sample_comm_venc_stop((hi_venc_chn)venc->chn);
        venc->started = 0;
        return -1;
    }

    printf("CV610 VENC started chn=%d %dx%d fps=%d bitrate=%d kbps\n",
        venc->chn, venc->width, venc->height, venc->frame_rate, venc->max_bitrate_kbps);
    return 0;
}

static int cv610_venc_set_bitrate(mi_cv610_venc_t* venc, int bitrate_kbps)
{
    hi_venc_chn_attr attr;
    hi_s32 ret;

    if (venc == NULL || !venc->started) {
        return -1;
    }

    bitrate_kbps = cv610_clamp_int(bitrate_kbps,
        MI_CV610_MIN_BITRATE_KBPS, venc->max_bitrate_kbps > 0 ? venc->max_bitrate_kbps : MI_CV610_MAX_BITRATE_KBPS);
    memset(&attr, 0, sizeof(attr));
    ret = hi_mpi_venc_get_chn_attr((hi_venc_chn)venc->chn, &attr);
    if (ret != HI_SUCCESS) {
        return -1;
    }
    cv610_apply_bitrate_to_attr(&attr, bitrate_kbps);
    ret = hi_mpi_venc_set_chn_attr((hi_venc_chn)venc->chn, &attr);
    if (ret != HI_SUCCESS) {
        printf("CV610 set bitrate failed chn=%d kbps=%d ret=%#x\n", venc->chn, bitrate_kbps, ret);
        return -1;
    }
    venc->cur_bitrate_kbps = bitrate_kbps;
    printf("CV610 set bitrate chn=%d kbps=%d\n", venc->chn, bitrate_kbps);
    return 0;
}

static int cv610_venc_get_bitrate(mi_cv610_venc_t* venc, int* bitrate_kbps)
{
    if (venc == NULL || bitrate_kbps == NULL) {
        return -1;
    }
    *bitrate_kbps = venc->cur_bitrate_kbps;
    return 0;
}

static void cv610_init_channels(mi_cv610_soc_t* self)
{
    struct SVideoCfg* cfg;

    if (self == NULL) {
        return;
    }
    cfg = &self->vcfg;

    memset(self->venc, 0, sizeof(self->venc));
    self->venc[0].chn = 0;
    self->venc[0].vpss_grp = 0;
    self->venc[0].vpss_chn = 0;
    self->venc[0].frame_rate = cfg->chn1MaxFrameRate;
    self->venc[0].max_bitrate_kbps = cfg->chn1MaxCodeRate;
    self->venc[0].payload = HI_PT_H264;
    cv610_pic_size(cfg->chn1PicWh, &self->venc[0].width, &self->venc[0].height);

    self->venc[1].chn = 1;
    self->venc[1].vpss_grp = 0;
    self->venc[1].vpss_chn = 1;
    self->venc[1].frame_rate = cfg->chn2FrameRate > 0 ? cfg->chn2FrameRate : 5;
    self->venc[1].max_bitrate_kbps = cfg->chn2CodeRate > 0 ? cfg->chn2CodeRate : 512;
    self->venc[1].payload = HI_PT_H264;
    cv610_pic_size(cfg->chn2PicWh, &self->venc[1].width, &self->venc[1].height);
}

static mi_cv610_venc_t* cv610_find_venc(int chn)
{
    if (G_pHardSoc == NULL) {
        return NULL;
    }
    if (chn < 0 || chn >= MI_CV610_MAX_VENC) {
        chn = MI_CV610_MAIN_CHN;
    }
    return &G_pHardSoc->venc[chn];
}

static int cv610_osd_ensure_started(void)
{
    mi_cv610_venc_t* venc;
    int any_started = 0;
    int ret = 0;
    int i;

    if (G_pHardSoc == NULL) {
        return -1;
    }
    for (i = 0; i < MI_CV610_MAX_VENC; ++i) {
        venc = cv610_find_venc(i);
        if (venc == NULL || !venc->started) {
            continue;
        }
        any_started = 1;
        if (!G_pHardSoc->osd[i].started &&
            mi_cv610_osd_start(&G_pHardSoc->osd[i], venc->chn, venc->width, venc->height) != 0) {
            ret = -1;
        }
    }
    if (!any_started) {
        return -1;
    }
    return ret;
}

static int cv610_osd_update_all(int index)
{
    int ret = 0;
    int i;

    if (G_pHardSoc == NULL) {
        return -1;
    }
    for (i = 0; i < MI_CV610_MAX_VENC; ++i) {
        mi_cv610_osd_update(&G_pHardSoc->osd[i], index);
    }
    return ret;
}

static int cv610_osd_set_show_all(int index, int show)
{
    int ret = 0;
    int i;

    if (G_pHardSoc == NULL) {
        return -1;
    }
    for (i = 0; i < MI_CV610_MAX_VENC; ++i) {
        if (mi_cv610_osd_set_show(&G_pHardSoc->osd[i], index, show) != 0) {
            ret = -1;
        }
    }
    return ret;
}

static int cv610_osd_set_font_size_all(int index, int font_size)
{
    int ret = 0;
    int i;

    if (G_pHardSoc == NULL) {
        return -1;
    }
    for (i = 0; i < MI_CV610_MAX_VENC; ++i) {
        if (mi_cv610_osd_set_font_size(&G_pHardSoc->osd[i], index, font_size) != 0) {
            ret = -1;
        }
    }
    return ret;
}

static int cv610_osd_reset_size_all(int index, int posx, int posy, int width, int height)
{
    int ret = 0;
    int i;

    if (G_pHardSoc == NULL) {
        return -1;
    }
    for (i = 0; i < MI_CV610_MAX_VENC; ++i) {
        if (mi_cv610_osd_reset_size(&G_pHardSoc->osd[i], index, posx, posy, width, height) != 0) {
            ret = -1;
        }
    }
    return ret;
}

static int cv610_osd_set_align_all(int index, int align)
{
    int ret = 0;
    int i;

    if (G_pHardSoc == NULL) {
        return -1;
    }
    for (i = 0; i < MI_CV610_MAX_VENC; ++i) {
        if (mi_cv610_osd_set_align(&G_pHardSoc->osd[i], index, align) != 0) {
            ret = -1;
        }
    }
    return ret;
}

static int cv610_osd_set_text_all(int index, const char* text)
{
    int ret = 0;
    int i;

    if (G_pHardSoc == NULL) {
        return -1;
    }
    for (i = 0; i < MI_CV610_MAX_VENC; ++i) {
        if (mi_cv610_osd_set_text(&G_pHardSoc->osd[i], index, text) != 0) {
            ret = -1;
        }
    }
    return ret;
}

static void GetVPCfg(struct IHardSoc* warp, struct SVideoCfg* vcfg, struct SCaptureCfg* ccfg)
{
    (void)warp;
    if (G_pHardSoc == NULL) {
        return;
    }
    if (vcfg != NULL) {
        *vcfg = G_pHardSoc->vcfg;
    }
    if (ccfg != NULL) {
        *ccfg = G_pHardSoc->ccfg;
    }
}

static int GetLowlight(struct IHardSoc* warp)
{
    (void)warp;
    return 0;
}

static void OnCheckIrCut(struct IHardSoc* warp)
{
    (void)warp;
}

static void OnInMoveMotor(struct IHardSoc* warp, int x, int y)
{
    (void)warp;
    (void)x;
    (void)y;
}

static void SetAiAoVol(struct IHardSoc* warp, int isAo, int vol)
{
    (void)warp;
    (void)isAo;
    (void)vol;
}

static void SetGpioValue(struct IHardSoc* warp, enum DefGPIONum num, int value)
{
    (void)warp;
    if (num == IO_LgWLED) {
        if (value) {
            gpio_pull_up(GPIO_IRCUT_CTL1);
        } else {
            gpio_pull_down(GPIO_IRCUT_CTL1);
        }
    }
}

static void SetGpioDir(struct IHardSoc* warp, enum DefGPIONum num, int value)
{
    (void)warp;
    (void)num;
    (void)value;
}

static int GetGpioValue(struct IHardSoc* warp, enum DefGPIONum nGpioNo)
{
    (void)warp;
    if (nGpioNo == IO_LgWLED) {
        return gpio_get_value(GPIO_IRCUT_CTL1);
    }
    return 0;
}

static int GetResetKeyValue(struct IHardSoc* warp)
{
    (void)warp;
    return get_gpio_reset_value();
}

static void OpWRLed(struct IHardSoc* warp, int op)
{
    (void)warp;
    if (op) {
        gpio_pull_up(GPIO_IRCUT_CTL1);
    } else {
        gpio_pull_down(GPIO_IRCUT_CTL1);
    }
}

static void GetOsdChnSize(struct IHardSoc* warp, int OsdId, int* width, int* height)
{
    (void)warp;
    (void)OsdId;
    if (width != NULL) {
        *width = G_pHardSoc != NULL ? G_pHardSoc->venc[0].width : 0;
    }
    if (height != NULL) {
        *height = G_pHardSoc != NULL ? G_pHardSoc->venc[0].height : 0;
    }
}

static int GetStream(struct IHardSoc* warp)
{
    (void)warp;
    mi_thread_sleep(10);
    return 0;
}

static int CapturePic(struct IHardSoc* warp, const char* strPath)
{
    (void)warp;
    (void)strPath;
    return 0;
}

static int GetEnTypeByChn(struct IHardSoc* warp, int chn)
{
    mi_cv610_venc_t* venc;

    (void)warp;
    venc = cv610_find_venc(chn);
    return venc != NULL ? cv610_payload_to_codec(venc->payload) : CI_UnDef;
}

static int SetEnTypeByChn(struct IHardSoc* warp, int chn, int EnType)
{
    mi_cv610_venc_t* venc;

    (void)warp;
    venc = cv610_find_venc(chn);
    if (venc == NULL || venc->started) {
        return -1;
    }
    if (EnType != CI_H264) {
        return -1;
    }
    venc->payload = HI_PT_H264;
    return 0;
}

static int SetVEncodeInfo(struct IHardSoc* warp,
    int EncodeIndex,
    enum EVideoPicWh PicWh,
    int MaxRate,
    int u32MaxBitRate)
{
    mi_cv610_venc_t* venc;

    (void)warp;
    venc = cv610_find_venc(EncodeIndex);
    if (venc == NULL) {
        return -1;
    }
    if (MaxRate > 0) {
        venc->frame_rate = cv610_clamp_int(MaxRate, 5, 30);
    }
    if (u32MaxBitRate > 0) {
        venc->max_bitrate_kbps = cv610_clamp_int(u32MaxBitRate,
            MI_CV610_MIN_BITRATE_KBPS, MI_CV610_MAX_BITRATE_KBPS);
    }
    if (PicWh != HV_UnDef && PicWh != 0) {
        cv610_pic_size(PicWh, &venc->width, &venc->height);
    }
    if (venc->started) {
        return cv610_venc_set_bitrate(venc, venc->max_bitrate_kbps);
    }
    return 0;
}

static int AdjustEncoderRate(struct IHardSoc* warp, int EncodeIndex, int u32MaxBitRate)
{
    (void)warp;
    return cv610_venc_set_bitrate(cv610_find_venc(EncodeIndex), u32MaxBitRate);
}

static int GetEncoderRate(struct IHardSoc* warp, int EncodeIndex, int* nDefMaxBitRate)
{
    (void)warp;
    return cv610_venc_get_bitrate(cv610_find_venc(EncodeIndex), nDefMaxBitRate);
}

static int SetPicEnInfo(struct IHardSoc* warp, enum EVideoPicWh PicWh, int Qp)
{
    (void)warp;
    (void)PicWh;
    (void)Qp;
    return 0;
}

static void ReqIDR(struct IHardSoc* warp, int chn)
{
    mi_cv610_venc_t* venc;

    (void)warp;
    venc = cv610_find_venc(chn);
    if (venc == NULL || !venc->started) {
        return;
    }
    if (hi_mpi_venc_request_idr((hi_venc_chn)venc->chn, HI_TRUE) != HI_SUCCESS) {
        printf("CV610 request IDR failed chn=%d\n", venc->chn);
    } else {
        printf("CV610 request IDR chn=%d\n", venc->chn);
    }
}

static int SetMirror(struct IHardSoc* warp, int VpssGrp, int VpssChn, int value)
{
    hi_vpss_chn_attr attr;
    hi_s32 ret;

    (void)warp;
    memset(&attr, 0, sizeof(attr));
    ret = hi_mpi_vpss_get_chn_attr((hi_vpss_grp)VpssGrp, (hi_vpss_chn)VpssChn, &attr);
    if (ret != HI_SUCCESS) {
        return -1;
    }
    attr.mirror_en = value ? HI_TRUE : HI_FALSE;
    ret = hi_mpi_vpss_set_chn_attr((hi_vpss_grp)VpssGrp, (hi_vpss_chn)VpssChn, &attr);
    return ret == HI_SUCCESS ? 0 : -1;
}

static int GetMirror(struct IHardSoc* warp, int VpssGrp, int VpssChn)
{
    hi_vpss_chn_attr attr;

    (void)warp;
    memset(&attr, 0, sizeof(attr));
    if (hi_mpi_vpss_get_chn_attr((hi_vpss_grp)VpssGrp, (hi_vpss_chn)VpssChn, &attr) != HI_SUCCESS) {
        return 0;
    }
    return attr.mirror_en ? 1 : 0;
}

static int ZeroStruct(void* ptr, int bytes)
{
    if (ptr != NULL && bytes > 0) {
        memset(ptr, 0, (size_t)bytes);
    }
    return 0;
}

static int GetIspInfo(struct IHardSoc* warp, struct CommIspInfo* ispInfo)
{
    (void)warp;
    return ZeroStruct(ispInfo, (int)sizeof(*ispInfo));
}

static int SetIspInfo(struct IHardSoc* warp, struct CommIspInfo* ispInfo)
{
    (void)warp;
    (void)ispInfo;
    return 0;
}

static int SetCommLDCH(struct IHardSoc* warp, const struct CommLDCH* ldch)
{
    (void)warp;
    (void)ldch;
    return 0;
}

static int GetCommLDCH(struct IHardSoc* warp, struct CommLDCH* ldch)
{
    (void)warp;
    return ZeroStruct(ldch, (int)sizeof(*ldch));
}

static int SetCommHDR(struct IHardSoc* warp, const struct CommHDR* hdr)
{
    (void)warp;
    (void)hdr;
    return 0;
}

static int GetCommHDR(struct IHardSoc* warp, struct CommHDR* hdr)
{
    (void)warp;
    return ZeroStruct(hdr, (int)sizeof(*hdr));
}

static int SetExposTime(struct IHardSoc* warp, const struct ExposTime* exposTime)
{
    (void)warp;
    (void)exposTime;
    return 0;
}

static int GetExposTime(struct IHardSoc* warp, struct ExposTime* exposTime)
{
    (void)warp;
    return ZeroStruct(exposTime, (int)sizeof(*exposTime));
}

static int SetBacklightComp(struct IHardSoc* warp, const struct BacklightComp* bk)
{
    (void)warp;
    (void)bk;
    return 0;
}

static int GetBacklightComp(struct IHardSoc* warp, struct BacklightComp* bk)
{
    (void)warp;
    return ZeroStruct(bk, (int)sizeof(*bk));
}

static int SetAwbMode(struct IHardSoc* warp, const struct AwbMode* bk)
{
    (void)warp;
    (void)bk;
    return 0;
}

static int GetAwbMode(struct IHardSoc* warp, struct AwbMode* bk)
{
    (void)warp;
    return ZeroStruct(bk, (int)sizeof(*bk));
}

static int SetAntiFlicker(struct IHardSoc* warp, const struct AntiFlicker* bk)
{
    (void)warp;
    (void)bk;
    return 0;
}

static int GetAntiFlicker(struct IHardSoc* warp, struct AntiFlicker* bk)
{
    (void)warp;
    return ZeroStruct(bk, (int)sizeof(*bk));
}

static int SetHLC(struct IHardSoc* warp, const struct CommHLC* bk)
{
    (void)warp;
    (void)bk;
    return 0;
}

static int GetHLC(struct IHardSoc* warp, struct CommHLC* bk)
{
    (void)warp;
    return ZeroStruct(bk, (int)sizeof(*bk));
}

static int GetAf(struct IHardSoc* warp, unsigned int* Fv1, unsigned int* Fv2)
{
    (void)warp;
    if (Fv1 != NULL) {
        *Fv1 = 0;
    }
    if (Fv2 != NULL) {
        *Fv2 = 0;
    }
    return 0;
}

static void UpdateOsd(struct IHardSoc* warp, int index)
{
    (void)warp;
    if (G_pHardSoc != NULL) {
        (void)cv610_osd_ensure_started();
        (void)cv610_osd_update_all(index);
    }
}

static int SetOsdShow(struct IHardSoc* warp, int Index, int show)
{
    (void)warp;
    if (show && cv610_osd_ensure_started() != 0) {
        return -1;
    }
    return cv610_osd_set_show_all(Index, show);
}

static int GetOsdShow(struct IHardSoc* warp, int Index)
{
    (void)warp;
    return G_pHardSoc != NULL ? mi_cv610_osd_get_show(&G_pHardSoc->osd[MI_CV610_MAIN_CHN], Index) : 0;
}

static int SetFontSize(struct IHardSoc* warp, int Index, int nFontSize)
{
    (void)warp;
    (void)cv610_osd_ensure_started();
    return cv610_osd_set_font_size_all(Index, nFontSize);
}

static int GetFontSize(struct IHardSoc* warp, int Index)
{
    (void)warp;
    return G_pHardSoc != NULL ? mi_cv610_osd_get_font_size(&G_pHardSoc->osd[MI_CV610_MAIN_CHN], Index) : 0;
}

static int ResetOsdSize(struct IHardSoc* warp, int index, int posx, int posy, int width, int height)
{
    (void)warp;
    (void)cv610_osd_ensure_started();
    return cv610_osd_reset_size_all(index, posx, posy, width, height);
}

static int Cv610SetTextAlign(struct IHardSoc* warp, int index, int nAlign)
{
    (void)warp;
    (void)cv610_osd_ensure_started();
    return cv610_osd_set_align_all(index, nAlign);
}

static int SetOsdText(struct IHardSoc* warp, int index, const char* text)
{
    (void)warp;
    if (text != NULL && text[0] != '\0' && cv610_osd_ensure_started() != 0) {
        return -1;
    }
    return cv610_osd_set_text_all(index, text);
}

static void OnCheckStatus(struct IHardSoc* warp)
{
    (void)warp;
}

static void OnAiHandle(struct SAlarmCfg* AlarmCfg,
    const struct SAlarmAreaInfo* AlarmAreas,
    int AreaCount,
    FpAlarmNotify notify)
{
    (void)AlarmCfg;
    (void)AlarmAreas;
    (void)AreaCount;
    (void)notify;
}

static void ReLoadHardCfg(struct IHardSoc* warp)
{
    (void)warp;
}

static void InitHard(struct IHardSoc* warp,
    FpOnAVStream cllAV,
    FpOnGetAudio readAudio,
    const struct SVideoCfg* vcfg,
    const struct SCaptureCfg* ccfg)
{
    mi_cv610_soc_t* self = G_pHardSoc;
    hi_s32 ret;

    (void)warp;
    if (self == NULL) {
        return;
    }

    self->cllAV = cllAV;
    self->readAudio = readAudio;
    if (vcfg != NULL) {
        self->vcfg = *vcfg;
    } else {
        memset(&self->vcfg, 0, sizeof(self->vcfg));
    }
    if (ccfg != NULL) {
        self->ccfg = *ccfg;
    } else {
        memset(&self->ccfg, 0, sizeof(self->ccfg));
    }
    cv610_default_video_cfg(&self->vcfg);
    cv610_init_channels(self);

    if (!self->mpp_started) {
        ret = cv610_require_external_mpp();
        if (ret != 0) {
            printf("CV610 MPP process is not ready\n");
            return;
        }
        self->mpp_started = 1;
    }

    if (cv610_attach_mpp_sys() != 0) {
        printf("CV610 attach MPP SYS failed\n");
        return;
    }

    gpio_int();
    if (cv610_venc_start(&self->venc[MI_CV610_MAIN_CHN]) != 0) {
        printf("CV610 main venc start failed\n");
        return;
    }
    if (cv610_venc_start(&self->venc[MI_CV610_SUB_CHN]) != 0) {
        printf("CV610 sub venc start failed\n");
    }

    self->hard_started = 1;
    printf("CV610 hard init done\n");
}

static void cv610_install_methods(mi_cv610_soc_t* self)
{
    IHardSoc* ISoc;

    if (self == NULL) {
        return;
    }

    ISoc = &self->ISoc;
    ISoc->InitHard = InitHard;
    ISoc->ReLoadHardCfg = ReLoadHardCfg;
    ISoc->GetVPCfg = GetVPCfg;
    ISoc->GetLowlight = GetLowlight;
    ISoc->OnCheckIrCut = OnCheckIrCut;
    ISoc->OnInMoveMotor = OnInMoveMotor;
    ISoc->SetAiAoVol = SetAiAoVol;
    ISoc->SetGpioValue = SetGpioValue;
    ISoc->SetGpioDir = SetGpioDir;
    ISoc->GetGpioValue = GetGpioValue;
    ISoc->GetResetKeyValue = GetResetKeyValue;
    ISoc->OpWRLed = OpWRLed;
    ISoc->GetOsdChnSize = GetOsdChnSize;
    ISoc->GetStream = GetStream;
    ISoc->CapturePic = CapturePic;
    ISoc->GetEnTypeByChn = GetEnTypeByChn;
    ISoc->SetEnTypeByChn = SetEnTypeByChn;
    ISoc->SetVEncodeInfo = SetVEncodeInfo;
    ISoc->AdjustEncoderRate = AdjustEncoderRate;
    ISoc->GetEncoderRate = GetEncoderRate;
    ISoc->SetPicEnInfo = SetPicEnInfo;
    ISoc->ReqIDR = ReqIDR;
    ISoc->SetMirror = SetMirror;
    ISoc->GetMirror = GetMirror;
    ISoc->GetIspInfo = GetIspInfo;
    ISoc->SetIspInfo = SetIspInfo;
    ISoc->SetCommLDCH = SetCommLDCH;
    ISoc->GetCommLDCH = GetCommLDCH;
    ISoc->SetCommHDR = SetCommHDR;
    ISoc->GetCommHDR = GetCommHDR;
    ISoc->SetExposTime = SetExposTime;
    ISoc->GetExposTime = GetExposTime;
    ISoc->SetBacklightComp = SetBacklightComp;
    ISoc->GetBacklightComp = GetBacklightComp;
    ISoc->SetAwbMode = SetAwbMode;
    ISoc->GetAwbMode = GetAwbMode;
    ISoc->SetAntiFlicker = SetAntiFlicker;
    ISoc->GetAntiFlicker = GetAntiFlicker;
    ISoc->SetHLC = SetHLC;
    ISoc->GetHLC = GetHLC;
    ISoc->GetAf = GetAf;
    ISoc->UpdateOsd = UpdateOsd;
    ISoc->SetOsdShow = SetOsdShow;
    ISoc->GetOsdShow = GetOsdShow;
    ISoc->SetFontSize = SetFontSize;
    ISoc->GetFontSize = GetFontSize;
    ISoc->ResetOsdSize = ResetOsdSize;
    ISoc->SetTextAlign = Cv610SetTextAlign;
    ISoc->SetOsdText = SetOsdText;
    ISoc->OnCheckStatus = OnCheckStatus;
    ISoc->OnAiHandle = OnAiHandle;
}

IHardSoc* CreateHardImp(void)
{
    if (G_pHardSoc != NULL) {
        return &G_pHardSoc->ISoc;
    }

    G_pHardSoc = (mi_cv610_soc_t*)malloc(sizeof(mi_cv610_soc_t));
    if (G_pHardSoc == NULL) {
        return NULL;
    }
    memset(G_pHardSoc, 0, sizeof(*G_pHardSoc));
    mi_cv610_osd_init_ex(&G_pHardSoc->osd[MI_CV610_MAIN_CHN], 0);
    mi_cv610_osd_init_ex(&G_pHardSoc->osd[MI_CV610_SUB_CHN], MI_CV610_OSD_MAX_ENTRIES);
    cv610_install_methods(G_pHardSoc);
    return &G_pHardSoc->ISoc;
}
