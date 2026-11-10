#ifndef MIIPC_RTSP_SERVER_H_
#define MIIPC_RTSP_SERVER_H_

#include <stdint.h>

#include "../Soc/CommInfo.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MIIPC_RTSP_DEFAULT_LISTEN_IP "0.0.0.0"
#define MIIPC_RTSP_DEFAULT_PORT 8554
#define MIIPC_RTSP_DEFAULT_SUB_PORT 8555
#define MIIPC_RTSP_DEFAULT_PATH "/live"
#define MIIPC_RTSP_DEFAULT_SUB_PATH "/live/sub"
#define MIIPC_RTSP_DEFAULT_MAX_CLIENTS 4
#define MIIPC_RTSP_PATH_MAX 96
#define MIIPC_RTSP_VIDEO_CODEC_H264 1
#define MIIPC_RTSP_VIDEO_CODEC_H265 2
#define MIIPC_RTSP_AUDIO_CODEC_G711A 1
#define MIIPC_RTSP_STREAM_MAIN 0
#define MIIPC_RTSP_STREAM_SUB 1
#define MIIPC_RTSP_STREAM_COUNT 2

typedef void (*miipc_rtsp_request_idr_cb)(int chn, void* user_data);

typedef struct miipc_rtsp_config {
    int enable;
    const char* listen_ip;
    uint16_t listen_port;
    const char* advertise_ip;
    const char* stream_path;
    int max_clients;
    int source_chn;
    int enable_udp;
    int enable_tcp_interleaved;
    int enable_sub;
    uint16_t sub_listen_port;
    const char* sub_stream_path;
    int sub_source_chn;
    miipc_rtsp_request_idr_cb request_idr;
    void* request_idr_user;
} miipc_rtsp_config_t;

typedef struct miipc_rtsp_info {
    char url[256];
    int stream_index;
    int source_chn;
    int video_codec;
    int video_width;
    int video_height;
    int video_fps;
    int video_bitrate_kbps;
    int audio_enabled;
    int audio_codec;
    int audio_sample_rate;
    int audio_channels;
} miipc_rtsp_info_t;

void miipc_rtsp_default_config(miipc_rtsp_config_t* cfg);
int miipc_rtsp_server_start(const miipc_rtsp_config_t* cfg, const struct SVideoCfg* video_cfg);
void miipc_rtsp_server_stop(void);
int miipc_rtsp_server_input_av_stream(int chn, struct Stream_Info* info, int count);
int miipc_rtsp_server_get_url(char* url, int url_len);
int miipc_rtsp_server_get_info(miipc_rtsp_info_t* info);
int miipc_rtsp_server_get_url_by_index(int stream_index, char* url, int url_len);
int miipc_rtsp_server_get_info_by_index(int stream_index, miipc_rtsp_info_t* info);

#ifdef __cplusplus
}
#endif

#endif
