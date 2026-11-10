#ifndef MIIPC_AUDIO_PROCESS_H_
#define MIIPC_AUDIO_PROCESS_H_

#include <stddef.h>
#include <stdint.h>

#include "miipc_audio_capture_stats.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct miipc_audio_process_t miipc_audio_process_t;

typedef struct miipc_audio_process_config_t {
    int sample_rate_hz;
    size_t frame_samples;
    int enable_aecm;
    int enable_ns;
    int ns_policy;
    int aecm_echo_mode;
    int aecm_cng_mode;
    int stream_delay_ms;
    int log_ms;
    int reset_error_limit;
    const char* log_tag;
} miipc_audio_process_config_t;

void miipc_audio_process_default_config(miipc_audio_process_config_t* config);
void miipc_audio_process_load_config(miipc_audio_process_config_t* config);
int miipc_audio_process_create(const miipc_audio_process_config_t* config,
    miipc_audio_process_t** process);
void miipc_audio_process_destroy(miipc_audio_process_t* process);
void miipc_audio_process_set_source_format(miipc_audio_process_t* process,
    int active,
    int sample_rate_hz,
    int channels,
    int bits_per_sample);
int miipc_audio_process_buffer_farend(miipc_audio_process_t* process,
    const int16_t* pcm,
    size_t samples,
    int sample_rate_hz);
int miipc_audio_process_capture(miipc_audio_process_t* process,
    const int16_t* input,
    int16_t* output,
    size_t samples,
    uint64_t pts_ms);
int miipc_audio_process_get_stats(miipc_audio_process_t* process,
    miipc_audio_capture_stats_t* stats);

#ifdef __cplusplus
}
#endif

#endif
