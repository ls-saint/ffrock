#include "miipcrtspserver.h"

#include "mibase.h"
#include "mirtspserver.h"

#include <stdio.h>
#include <string.h>

#if defined(TX_5110)
#define MIIPC_RTSP_DEFAULT_SOURCE_CHN 1
#else
#define MIIPC_RTSP_DEFAULT_SOURCE_CHN 0
#endif
#define MIIPC_RTSP_DEFAULT_SUB_SOURCE_CHN 1

typedef struct miipc_rtsp_runtime {
    MiRtspServer* server;
    int started;
    int stream_index;
    int source_chn;
    int codec;
    int last_playing_count;
    uint32_t last_log_ms;
    uint32_t input_frames;
    uint32_t input_bytes;
    uint32_t send_fail;
    miipc_rtsp_request_idr_cb request_idr;
    void* request_idr_user;
    char url[256];
    miipc_rtsp_info_t info;
} miipc_rtsp_runtime_t;

static miipc_rtsp_runtime_t g_rtsp[MIIPC_RTSP_STREAM_COUNT];

static void rtsp_copy_text(char* dst, int dst_len, const char* src)
{
    if (dst == NULL || dst_len <= 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, (size_t)dst_len - 1u);
    dst[dst_len - 1] = '\0';
}

static int rtsp_default_source_chn(void)
{
    return MIIPC_RTSP_DEFAULT_SOURCE_CHN;
}

static int rtsp_default_sub_source_chn(void)
{
    return MIIPC_RTSP_DEFAULT_SUB_SOURCE_CHN;
}

static int rtsp_codec_from_video_cfg(const struct SVideoCfg* video_cfg, int source_chn)
{
    enum CodecInfo codec;

    if (video_cfg == NULL) {
        return MI_RTSP_VIDEO_CODEC_H264;
    }
    codec = source_chn == 1 ? video_cfg->chn2Encoder : video_cfg->chn1Encoder;
    if (codec == CI_H265) {
        return MI_RTSP_VIDEO_CODEC_H265;
    }
    return MI_RTSP_VIDEO_CODEC_H264;
}

static void rtsp_pic_wh(enum EVideoPicWh pic_wh, int* width, int* height)
{
    int w = 1280;
    int h = 720;

    switch (pic_wh) {
    case H640_360:
        w = 640;
        h = 360;
        break;
    case H1920_1080:
        w = 1920;
        h = 1080;
        break;
    case H1280_720:
    default:
        w = 1280;
        h = 720;
        break;
    }
    if (width != NULL) {
        *width = w;
    }
    if (height != NULL) {
        *height = h;
    }
}

static void rtsp_video_cfg_info(const struct SVideoCfg* video_cfg,
                                int source_chn,
                                int* width,
                                int* height,
                                int* fps,
                                int* bitrate_kbps)
{
    enum EVideoPicWh pic_wh;
    int local_fps;
    int local_bitrate;

    pic_wh = H1280_720;
    local_fps = 25;
    local_bitrate = 1024;
    if (video_cfg != NULL) {
        if (source_chn == 1) {
            pic_wh = video_cfg->chn2PicWh;
            local_fps = video_cfg->chn2FrameRate;
            local_bitrate = video_cfg->chn2CodeRate;
        } else {
            pic_wh = video_cfg->chn1PicWh;
            local_fps = video_cfg->chn1MaxFrameRate;
            local_bitrate = video_cfg->chn1MaxCodeRate;
        }
    }
    rtsp_pic_wh(pic_wh, width, height);
    if (fps != NULL) {
        *fps = local_fps > 0 ? local_fps : 25;
    }
    if (bitrate_kbps != NULL) {
        *bitrate_kbps = local_bitrate > 0 ? local_bitrate : 1024;
    }
}

static int rtsp_codec_from_frame_type(int en_type)
{
    if (en_type == CI_H265) {
        return MI_RTSP_VIDEO_CODEC_H265;
    }
    if (en_type == CI_H264) {
        return MI_RTSP_VIDEO_CODEC_H264;
    }
    return 0;
}

static int rtsp_is_g711_frame(const struct Stream_Info* frame)
{
    return frame != NULL && frame->enType == CI_G711;
}

static uint32_t rtsp_audio_rtp_timestamp_from_pts(uint64_t pts_ms)
{
    return (uint32_t)((pts_ms * 8ULL) & 0xffffffffu);
}

static const char* rtsp_frame_codec_name(const struct Stream_Info* frame)
{
    if (frame == NULL) {
        return "unknown";
    }
    if (frame->enType == CI_G711) {
        return "G711A";
    }
    if (frame->enType == CI_H265) {
        return "H265";
    }
    if (frame->enType == CI_H264) {
        return "H264";
    }
    return "unknown";
}

void miipc_rtsp_default_config(miipc_rtsp_config_t* cfg)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->enable = 1;
    cfg->listen_ip = MIIPC_RTSP_DEFAULT_LISTEN_IP;
    cfg->listen_port = MIIPC_RTSP_DEFAULT_PORT;
    cfg->advertise_ip = NULL;
    cfg->stream_path = MIIPC_RTSP_DEFAULT_PATH;
    cfg->max_clients = MIIPC_RTSP_DEFAULT_MAX_CLIENTS;
    cfg->source_chn = rtsp_default_source_chn();
    cfg->enable_udp = 1;
    cfg->enable_tcp_interleaved = 1;
    cfg->enable_sub = 1;
    cfg->sub_listen_port = MIIPC_RTSP_DEFAULT_SUB_PORT;
    cfg->sub_stream_path = MIIPC_RTSP_DEFAULT_SUB_PATH;
    cfg->sub_source_chn = rtsp_default_sub_source_chn();
}

static int rtsp_is_started_any(void)
{
    int i;

    for (i = 0; i < MIIPC_RTSP_STREAM_COUNT; ++i) {
        if (g_rtsp[i].started || g_rtsp[i].server != NULL) {
            return 1;
        }
    }
    return 0;
}

static void rtsp_stop_instance(miipc_rtsp_runtime_t* runtime)
{
    if (runtime == NULL) {
        return;
    }
    if (runtime->server != NULL) {
        mi_rtsp_server_stop(runtime->server);
        mi_rtsp_server_destroy(runtime->server);
    }
    memset(runtime, 0, sizeof(*runtime));
}

static int rtsp_start_instance(miipc_rtsp_runtime_t* runtime,
                               int stream_index,
                               const char* stream_name,
                               const char* listen_ip,
                               uint16_t listen_port,
                               const char* advertise_ip,
                               const char* stream_path,
                               int max_clients,
                               int source_chn,
                               int enable_udp,
                               int enable_tcp_interleaved,
                               uint32_t video_ssrc,
                               uint32_t audio_ssrc,
                               const miipc_rtsp_config_t* cfg,
                               const struct SVideoCfg* video_cfg)
{
    MiRtspServerCfg rtsp_cfg;
    int codec;
    int ret;

    if (runtime == NULL || cfg == NULL) {
        return -1;
    }
    if (runtime->started) {
        return 0;
    }

    memset(&rtsp_cfg, 0, sizeof(rtsp_cfg));
    rtsp_cfg.listen_ip = listen_ip != NULL ? listen_ip : MIIPC_RTSP_DEFAULT_LISTEN_IP;
    rtsp_cfg.listen_port = listen_port;
    rtsp_cfg.advertise_ip = advertise_ip;
    rtsp_cfg.stream_path = stream_path != NULL ? stream_path : MIIPC_RTSP_DEFAULT_PATH;
    rtsp_cfg.stream_name = stream_name != NULL ? stream_name : "MiIpc";
    rtsp_cfg.max_clients = max_clients > 0 ? max_clients : MIIPC_RTSP_DEFAULT_MAX_CLIENTS;
    rtsp_cfg.payload_type = 96;
    rtsp_cfg.ssrc = video_ssrc;
    rtsp_cfg.rtp_packet_size = MI_RTSP_DEFAULT_RTP_PACKET_SIZE;
    rtsp_cfg.enable_audio = 1;
    rtsp_cfg.audio_codec = MI_RTSP_AUDIO_CODEC_G711A;
    rtsp_cfg.audio_payload_type = 8;
    rtsp_cfg.audio_clock_rate = 8000u;
    rtsp_cfg.audio_ssrc = audio_ssrc;
    rtsp_cfg.audio_rtp_packet_size = MI_RTSP_DEFAULT_RTP_PACKET_SIZE;
    rtsp_cfg.enable_udp = enable_udp;
    rtsp_cfg.enable_tcp_interleaved = enable_tcp_interleaved;

    runtime->stream_index = stream_index;
    runtime->source_chn = source_chn;
    codec = rtsp_codec_from_video_cfg(video_cfg, runtime->source_chn);
    rtsp_cfg.video_codec = codec;
    runtime->codec = codec;
    runtime->request_idr = cfg->request_idr;
    runtime->request_idr_user = cfg->request_idr_user;

    runtime->server = mi_rtsp_server_create(&rtsp_cfg);
    if (runtime->server == NULL) {
        printf("MiIpc RTSP server create failed stream=%d\n", stream_index);
        fflush(stdout);
        return -1;
    }
    ret = mi_rtsp_server_start(runtime->server);
    if (ret != 0) {
        printf("MiIpc RTSP server start failed stream=%d ret=%d\n", stream_index, ret);
        fflush(stdout);
        rtsp_stop_instance(runtime);
        return ret;
    }

    runtime->started = 1;
    (void)mi_rtsp_server_get_url(runtime->server, runtime->url, sizeof(runtime->url));
    memset(&runtime->info, 0, sizeof(runtime->info));
    rtsp_copy_text(runtime->info.url, sizeof(runtime->info.url), runtime->url);
    runtime->info.stream_index = stream_index;
    runtime->info.source_chn = runtime->source_chn;
    runtime->info.video_codec = runtime->codec == MI_RTSP_VIDEO_CODEC_H265 ?
        MIIPC_RTSP_VIDEO_CODEC_H265 : MIIPC_RTSP_VIDEO_CODEC_H264;
    rtsp_video_cfg_info(video_cfg,
                        runtime->source_chn,
                        &runtime->info.video_width,
                        &runtime->info.video_height,
                        &runtime->info.video_fps,
                        &runtime->info.video_bitrate_kbps);
    runtime->info.audio_enabled = 1;
    runtime->info.audio_codec = MIIPC_RTSP_AUDIO_CODEC_G711A;
    runtime->info.audio_sample_rate = 8000;
    runtime->info.audio_channels = 1;
    printf("MiIpc RTSP server started stream=%d url=%s source_chn=%d codec=%s %dx%d fps=%d bitrate=%d audio=G711A\n",
           stream_index,
           runtime->url,
           runtime->source_chn,
           runtime->codec == MI_RTSP_VIDEO_CODEC_H265 ? "H265" : "H264",
           runtime->info.video_width,
           runtime->info.video_height,
           runtime->info.video_fps,
           runtime->info.video_bitrate_kbps);
    fflush(stdout);
    return 0;
}

int miipc_rtsp_server_start(const miipc_rtsp_config_t* cfg, const struct SVideoCfg* video_cfg)
{
    miipc_rtsp_config_t local_cfg;
    const char* listen_ip;
    const char* advertise_ip;
    int max_clients;
    int enable_udp;
    int enable_tcp_interleaved;
    uint16_t main_port;
    uint16_t sub_port;
    int main_chn;
    int sub_chn;
    int ret;

    if (cfg == NULL) {
        miipc_rtsp_default_config(&local_cfg);
        cfg = &local_cfg;
    }
    if (!cfg->enable) {
        return 0;
    }
    if (rtsp_is_started_any()) {
        return 0;
    }

    listen_ip = cfg->listen_ip != NULL ? cfg->listen_ip : MIIPC_RTSP_DEFAULT_LISTEN_IP;
    advertise_ip = cfg->advertise_ip;
    max_clients = cfg->max_clients > 0 ? cfg->max_clients : MIIPC_RTSP_DEFAULT_MAX_CLIENTS;
    enable_udp = cfg->enable_udp;
    enable_tcp_interleaved = cfg->enable_tcp_interleaved;
    main_port = cfg->listen_port != 0 ? cfg->listen_port : MIIPC_RTSP_DEFAULT_PORT;
    sub_port = cfg->sub_listen_port != 0 ? cfg->sub_listen_port : MIIPC_RTSP_DEFAULT_SUB_PORT;
    main_chn = cfg->source_chn >= 0 ? cfg->source_chn : rtsp_default_source_chn();
    sub_chn = cfg->sub_source_chn >= 0 ? cfg->sub_source_chn : rtsp_default_sub_source_chn();

    ret = rtsp_start_instance(&g_rtsp[MIIPC_RTSP_STREAM_MAIN],
                              MIIPC_RTSP_STREAM_MAIN,
                              "MiIpcMain",
                              listen_ip,
                              main_port,
                              advertise_ip,
                              cfg->stream_path != NULL ? cfg->stream_path : MIIPC_RTSP_DEFAULT_PATH,
                              max_clients,
                              main_chn,
                              enable_udp,
                              enable_tcp_interleaved,
                              0x4d495043u,
                              0x4d495041u,
                              cfg,
                              video_cfg);
    if (ret != 0) {
        return ret;
    }

    if (cfg->enable_sub) {
        ret = rtsp_start_instance(&g_rtsp[MIIPC_RTSP_STREAM_SUB],
                                  MIIPC_RTSP_STREAM_SUB,
                                  "MiIpcSub",
                                  listen_ip,
                                  sub_port,
                                  advertise_ip,
                                  cfg->sub_stream_path != NULL ? cfg->sub_stream_path : MIIPC_RTSP_DEFAULT_SUB_PATH,
                                  max_clients,
                                  sub_chn,
                                  enable_udp,
                                  enable_tcp_interleaved,
                                  0x4d495053u,
                                  0x4d495055u,
                                  cfg,
                                  video_cfg);
        if (ret != 0) {
            miipc_rtsp_server_stop();
            return ret;
        }
    }

    return 0;
}

void miipc_rtsp_server_stop(void)
{
    int i;

    if (!rtsp_is_started_any()) {
        return;
    }
    for (i = 0; i < MIIPC_RTSP_STREAM_COUNT; ++i) {
        rtsp_stop_instance(&g_rtsp[i]);
    }
    printf("MiIpc RTSP server stopped\n");
    fflush(stdout);
}

static void rtsp_maybe_request_idr(miipc_rtsp_runtime_t* runtime, const MiRtspServerStats* stats)
{
    int playing_count;

    if (runtime == NULL || stats == NULL) {
        return;
    }
    playing_count = (int)stats->playing_count;
    if (playing_count > 0 && runtime->last_playing_count == 0 &&
        runtime->request_idr != NULL) {
        runtime->request_idr(runtime->source_chn, runtime->request_idr_user);
    }
    runtime->last_playing_count = playing_count;
}

static void rtsp_log_stats_if_needed(miipc_rtsp_runtime_t* runtime,
                                     const MiRtspServerStats* stats,
                                     int chn,
                                     const struct Stream_Info* frame,
                                     int ret)
{
    uint32_t now_ms;

    if (runtime == NULL || stats == NULL || frame == NULL) {
        return;
    }
    now_ms = mi_getTick32();
    if (runtime->last_log_ms != 0 &&
        (uint32_t)(now_ms - runtime->last_log_ms) < 3000u) {
        return;
    }
    runtime->last_log_ms = now_ms;
    printf("MiIpc RTSP input stream=%d chn=%d source_chn=%d codec=%s bytes=%d key=%d pts=%u clients=%u playing=%u frames=%u packets=%u drops=%u ret=%d\n",
           runtime->stream_index,
           frame->enType == CI_G711 ? chn : runtime->source_chn,
           runtime->source_chn,
           rtsp_frame_codec_name(frame),
           frame->buflen,
           frame->Ikey,
           (unsigned int)(frame->pts & 0xffffffffu),
           (unsigned int)stats->client_count,
           (unsigned int)stats->playing_count,
           (unsigned int)runtime->input_frames,
           (unsigned int)stats->packet_count,
           (unsigned int)stats->dropped_packet_count,
           ret);
    fflush(stdout);
}

static void rtsp_input_one_runtime(miipc_rtsp_runtime_t* runtime,
                                   int chn,
                                   const struct Stream_Info* frame)
{
    MiRtspServerStats stats;
    int codec;
    int is_audio;
    int ret;

    if (runtime == NULL || !runtime->started || runtime->server == NULL ||
        frame == NULL || frame->buffer == NULL || frame->buflen <= 0) {
        return;
    }

    is_audio = rtsp_is_g711_frame(frame);
    codec = rtsp_codec_from_frame_type(frame->enType);
    if (!is_audio && chn != runtime->source_chn) {
        return;
    }
    if (!is_audio && (codec == 0 || codec != runtime->codec)) {
        return;
    }

    memset(&stats, 0, sizeof(stats));
    (void)mi_rtsp_server_get_stats(runtime->server, &stats);
    if (!is_audio) {
        rtsp_maybe_request_idr(runtime, &stats);
        ret = mi_rtsp_server_input_video(runtime->server,
                                         frame->buffer,
                                         frame->buflen,
                                         (uint32_t)(frame->pts & 0xffffffffu),
                                         frame->Ikey);
    } else {
        ret = mi_rtsp_server_input_audio(runtime->server,
                                         frame->buffer,
                                         frame->buflen,
                                         rtsp_audio_rtp_timestamp_from_pts(frame->pts));
    }
    if (ret == 0) {
        ++runtime->input_frames;
        runtime->input_bytes += (uint32_t)frame->buflen;
    } else {
        ++runtime->send_fail;
    }
    memset(&stats, 0, sizeof(stats));
    (void)mi_rtsp_server_get_stats(runtime->server, &stats);
    rtsp_log_stats_if_needed(runtime, &stats, chn, frame, ret);
}

int miipc_rtsp_server_input_av_stream(int chn, struct Stream_Info* info, int count)
{
    int i;

    if (!rtsp_is_started_any() || info == NULL || count <= 0) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        struct Stream_Info* frame = &info[i];
        int j;

        if (frame->buffer == NULL || frame->buflen <= 0) {
            continue;
        }
        for (j = 0; j < MIIPC_RTSP_STREAM_COUNT; ++j) {
            rtsp_input_one_runtime(&g_rtsp[j], chn, frame);
        }
    }
    return 0;
}

int miipc_rtsp_server_get_url_by_index(int stream_index, char* url, int url_len)
{
    miipc_rtsp_runtime_t* runtime;

    if (url == NULL || url_len <= 0) {
        return -1;
    }
    url[0] = '\0';
    if (stream_index < 0 || stream_index >= MIIPC_RTSP_STREAM_COUNT) {
        return -1;
    }
    runtime = &g_rtsp[stream_index];
    if (!runtime->started || runtime->server == NULL) {
        return -1;
    }
    if (runtime->url[0] != '\0') {
        rtsp_copy_text(url, url_len, runtime->url);
        return 0;
    }
    return mi_rtsp_server_get_url(runtime->server, url, url_len);
}

int miipc_rtsp_server_get_url(char* url, int url_len)
{
    return miipc_rtsp_server_get_url_by_index(MIIPC_RTSP_STREAM_MAIN, url, url_len);
}

int miipc_rtsp_server_get_info_by_index(int stream_index, miipc_rtsp_info_t* info)
{
    miipc_rtsp_runtime_t* runtime;

    if (info == NULL) {
        return -1;
    }
    memset(info, 0, sizeof(*info));
    if (stream_index < 0 || stream_index >= MIIPC_RTSP_STREAM_COUNT) {
        return -1;
    }
    runtime = &g_rtsp[stream_index];
    if (!runtime->started || runtime->server == NULL) {
        return -1;
    }
    *info = runtime->info;
    if (info->url[0] == '\0') {
        (void)miipc_rtsp_server_get_url_by_index(stream_index, info->url, sizeof(info->url));
    }
    return 0;
}

int miipc_rtsp_server_get_info(miipc_rtsp_info_t* info)
{
    return miipc_rtsp_server_get_info_by_index(MIIPC_RTSP_STREAM_MAIN, info);
}
