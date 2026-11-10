#ifndef MIIPC_AUDIO_CAPTURE_STATS_H_
#define MIIPC_AUDIO_CAPTURE_STATS_H_

#include <stdint.h>

#include "miipc_aec.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct miipc_audio_capture_stats_t {
    int active;
    int sample_rate_hz;
    int channels;
    int bits_per_sample;
    int aec_config_enabled;
    int aec_active;
    int last_input_level;
    int last_output_level;
    int last_suppression_q8;
    int aec_last_process_ret;
    uint64_t audio_packets;
    uint64_t aec_processed_packets;
    uint64_t aec_bypass_packets;
    uint64_t aec_process_errors;
    uint64_t aec_error_resets;
    miipc_aec_stats_t aec;
} miipc_audio_capture_stats_t;

#ifdef __cplusplus
}
#endif

#endif
