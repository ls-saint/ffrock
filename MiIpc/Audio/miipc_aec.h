#ifndef MIIPC_AEC_H_
#define MIIPC_AEC_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct miipc_aec_t miipc_aec_t;

typedef struct miipc_aec_config_t {
    int sample_rate_hz;
    size_t frame_samples;
    int enable_aecm;
    int enable_ns;
    int ns_policy;
    int aecm_echo_mode;
    int aecm_cng_mode;
    int stream_delay_ms;
} miipc_aec_config_t;

typedef struct miipc_aec_stats_t {
    uint64_t process_calls;
    uint64_t farend_calls;
    uint64_t farend_input_samples;
    uint64_t processed_samples;
    uint64_t farend_samples;
    uint64_t farend_pending_samples;
    uint64_t bypass_samples;
    uint64_t process_errors;
    uint64_t farend_errors;
    uint64_t reset_count;
    int last_error;
    int sample_rate_hz;
    int frame_samples;
    int enable_aecm;
    int enable_ns;
    int ns_policy;
    int aecm_echo_mode;
    int aecm_cng_mode;
    int stream_delay_ms;
} miipc_aec_stats_t;

void miipc_aec_default_config(miipc_aec_config_t* config);
int miipc_aec_create(const miipc_aec_config_t* config, miipc_aec_t** aec);
void miipc_aec_destroy(miipc_aec_t* aec);
int miipc_aec_buffer_farend(miipc_aec_t* aec, const int16_t* farend, size_t samples);
int miipc_aec_process(miipc_aec_t* aec, const int16_t* nearend, int16_t* output, size_t samples);
int miipc_aec_reset(miipc_aec_t* aec);
int miipc_aec_set_stream_delay_ms(miipc_aec_t* aec, int delay_ms);
int miipc_aec_get_stats(miipc_aec_t* aec, miipc_aec_stats_t* stats);

#ifdef __cplusplus
}
#endif

#endif
