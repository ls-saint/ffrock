#ifndef MIIPC_WEBRTC_BRIDGE_H_
#define MIIPC_WEBRTC_BRIDGE_H_

#include <stddef.h>
#include <stdint.h>

#include "../Soc/CommInfo.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MI_WEBRTC_CANDIDATE_LIST_MAX 16
#define MI_WEBRTC_CANDIDATE_TEXT_MAX 2048

typedef struct mi_webrtc_answer {
    int status;
    char content_type[64];
    char* body;
    int body_len;
} mi_webrtc_answer_t;

typedef struct mi_webrtc_resource_cfg {
    uint16_t video_nack_history;
    uint16_t video_nack_max_missing;
    uint16_t audio_nack_history;
    uint16_t audio_nack_max_missing;
    uint16_t nack_max_packet_size;
    uint16_t paced_queue_capacity;
    uint16_t paced_max_packet_size;
} mi_webrtc_resource_cfg_t;

typedef struct mi_webrtc_offer_prepare_cfg {
    int source_chn;
    int audio_enable;
    int video_enable;
    int send_audio_mode;
    int data_channel;
    int close_all;
    const char* codec;
    const char* ice_url;
    const char* ice_username;
    const char* ice_password;
    const char* ice_realm;
} mi_webrtc_offer_prepare_cfg_t;

typedef struct mi_webrtc_offer_prepare_result {
    uint32_t dialogue_id;
    char candidate[2048];
    char sdp_offer[16384];
} mi_webrtc_offer_prepare_result_t;

const char* mi_webrtc_page_html(void);
void mi_webrtc_set_request_idr_callback(void (*callback)(int chn, void* user_data), void* user_data);
void mi_webrtc_set_encoder_bitrate_callback(void (*callback)(int chn, uint32_t bitrate_bps, void* user_data), void* user_data);
int mi_webrtc_set_resource_config(const mi_webrtc_resource_cfg_t* cfg);
void mi_webrtc_get_resource_config(mi_webrtc_resource_cfg_t* cfg);
int mi_webrtc_prepare_sdp_offer(const mi_webrtc_offer_prepare_cfg_t* cfg, mi_webrtc_offer_prepare_result_t* result);
int mi_webrtc_handle_offer(const char* offer, int offer_len, mi_webrtc_answer_t* answer);
int mi_webrtc_handle_offer_for_channel(const char* offer, int offer_len, mi_webrtc_answer_t* answer, int source_chn);
int mi_webrtc_apply_sdp_answer(uint32_t session_id, const char* answer, int answer_len);
int mi_webrtc_add_candidate(uint32_t session_id, const char* candidate, int candidate_len);
int mi_webrtc_candidates_json(uint32_t session_id, char* out, int out_size);
int mi_webrtc_get_local_candidates(uint32_t session_id,
                                   char candidates[][MI_WEBRTC_CANDIDATE_TEXT_MAX],
                                   int max_count,
                                   int* out_count,
                                   int* ice_status);
int mi_webrtc_close_session(uint32_t session_id);
void mi_webrtc_answer_free(mi_webrtc_answer_t* answer);
void mi_webrtc_process(void);
void mi_webrtc_input_av_stream(int chn, struct Stream_Info* info, int count);
int mi_webrtc_stats_json(uint32_t session_id, char* out, int out_size);
void mi_webrtc_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
