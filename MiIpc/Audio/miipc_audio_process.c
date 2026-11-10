#include "miipc_audio_process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miipc_aec.h"
#include "../Config/miipccfgdb.h"
#include "mimutex.h"

struct miipc_audio_process_t {
    miipc_aec_t* aec;
    miipc_audio_process_config_t config;
    miipc_audio_capture_stats_t stats;
    mi_Mutex* lock;
    uint64_t last_log_ms;
    int consecutive_errors;
    int initialized;
};

static int audio_process_clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static int audio_process_read_int2(const char* key,
    const char* fallback_key,
    int default_value)
{
    int value = default_value;

    if (miipc_cfg_db_read_int("audio", key, default_value, &value) == 0) {
        return value;
    }
    if (fallback_key != NULL &&
        miipc_cfg_db_read_int("audio", fallback_key, default_value, &value) == 0) {
        return value;
    }
    return default_value;
}

static int audio_process_average_abs(const int16_t* pcm, size_t samples)
{
    int64_t sum = 0;
    size_t i;

    if (pcm == NULL || samples == 0) {
        return 0;
    }
    for (i = 0; i < samples; ++i) {
        int value = (int)pcm[i];
        sum += value < 0 ? -value : value;
    }
    return (int)(sum / (int64_t)samples);
}

static void audio_process_lock(miipc_audio_process_t* process)
{
    if (process != NULL && process->lock != NULL) {
        process->lock->Lock(process->lock);
    }
}

static void audio_process_unlock(miipc_audio_process_t* process)
{
    if (process != NULL && process->lock != NULL) {
        process->lock->UnLock(process->lock);
    }
}

void miipc_audio_process_default_config(miipc_audio_process_config_t* config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->sample_rate_hz = 8000;
    config->frame_samples = 160;
    config->enable_aecm = 1;
    config->enable_ns = 1;
    config->ns_policy = 1;
    config->aecm_echo_mode = 3;
    config->aecm_cng_mode = 1;
    config->stream_delay_ms = 40;
    config->log_ms = 5000;
    config->reset_error_limit = 5;
    config->log_tag = "MiIpc audio";
}

void miipc_audio_process_load_config(miipc_audio_process_config_t* config)
{
    if (config == NULL) {
        return;
    }
    miipc_audio_process_default_config(config);
    config->enable_aecm = audio_process_read_int2(
        "aec_enable", "pc_aec_enable", 1) ? 1 : 0;
    config->enable_ns = audio_process_read_int2(
        "ns_enable", "pc_ns_enable", 1) ? 1 : 0;
    config->ns_policy = audio_process_clamp_int(audio_process_read_int2(
        "ns_policy", "pc_ns_policy", 1), 0, 3);
    config->aecm_echo_mode = audio_process_clamp_int(audio_process_read_int2(
        "aec_echo_mode", "pc_aec_echo_mode", 3), 0, 4);
    config->aecm_cng_mode = audio_process_read_int2(
        "aec_cng_mode", "pc_aec_cng_mode", 1) ? 1 : 0;
    config->stream_delay_ms = audio_process_clamp_int(audio_process_read_int2(
        "aec_delay_ms", "pc_aec_delay_ms", 40), 0, 500);
    config->log_ms = audio_process_clamp_int(audio_process_read_int2(
        "aec_log_ms", "pc_aec_log_ms", 5000), 0, 600000);
    config->reset_error_limit = audio_process_clamp_int(audio_process_read_int2(
        "aec_reset_error_limit", "pc_aec_reset_error_limit", 5), 0, 100);
}

static int audio_process_normalize_config(miipc_audio_process_config_t* config)
{
    if (config == NULL) {
        return -1;
    }
    if (config->sample_rate_hz != 8000 && config->sample_rate_hz != 16000) {
        return -1;
    }
    if (config->frame_samples == 0) {
        config->frame_samples = (size_t)(config->sample_rate_hz / 50);
    }
    if (config->frame_samples < (size_t)(config->sample_rate_hz / 100) ||
        config->frame_samples > (size_t)(config->sample_rate_hz / 25)) {
        return -1;
    }
    config->enable_aecm = config->enable_aecm ? 1 : 0;
    config->enable_ns = config->enable_ns ? 1 : 0;
    config->ns_policy = audio_process_clamp_int(config->ns_policy, 0, 3);
    config->aecm_echo_mode = audio_process_clamp_int(config->aecm_echo_mode, 0, 4);
    config->aecm_cng_mode = config->aecm_cng_mode ? 1 : 0;
    config->stream_delay_ms = audio_process_clamp_int(config->stream_delay_ms, 0, 500);
    config->log_ms = audio_process_clamp_int(config->log_ms, 0, 600000);
    config->reset_error_limit = audio_process_clamp_int(config->reset_error_limit, 0, 100);
    if (config->log_tag == NULL) {
        config->log_tag = "MiIpc audio";
    }
    return 0;
}

int miipc_audio_process_create(const miipc_audio_process_config_t* config,
    miipc_audio_process_t** process)
{
    miipc_audio_process_config_t local_config;
    miipc_aec_config_t aec_config;
    miipc_audio_process_t* self;

    if (process == NULL) {
        return -1;
    }
    *process = NULL;
    if (config != NULL) {
        local_config = *config;
    } else {
        miipc_audio_process_load_config(&local_config);
    }
    if (audio_process_normalize_config(&local_config) != 0) {
        return -1;
    }

    self = (miipc_audio_process_t*)calloc(1, sizeof(*self));
    if (self == NULL) {
        return -1;
    }
    self->config = local_config;
    self->lock = mi_CreateMutex(1);
    if (self->lock == NULL) {
        miipc_audio_process_destroy(self);
        return -1;
    }

    self->stats.aec_config_enabled =
        (local_config.enable_aecm || local_config.enable_ns) ? 1 : 0;
    self->stats.last_suppression_q8 = 256;
    self->stats.aec.sample_rate_hz = local_config.sample_rate_hz;
    self->stats.aec.frame_samples = (int)(local_config.sample_rate_hz / 100);
    self->stats.aec.enable_aecm = local_config.enable_aecm;
    self->stats.aec.enable_ns = local_config.enable_ns;
    self->stats.aec.ns_policy = local_config.ns_policy;
    self->stats.aec.aecm_echo_mode = local_config.aecm_echo_mode;
    self->stats.aec.aecm_cng_mode = local_config.aecm_cng_mode;
    self->stats.aec.stream_delay_ms = local_config.stream_delay_ms;
    if (self->stats.aec_config_enabled) {
        miipc_aec_default_config(&aec_config);
        aec_config.sample_rate_hz = local_config.sample_rate_hz;
        aec_config.frame_samples = (size_t)(local_config.sample_rate_hz / 100);
        aec_config.enable_aecm = local_config.enable_aecm;
        aec_config.enable_ns = local_config.enable_ns;
        aec_config.ns_policy = local_config.ns_policy;
        aec_config.aecm_echo_mode = local_config.aecm_echo_mode;
        aec_config.aecm_cng_mode = local_config.aecm_cng_mode;
        aec_config.stream_delay_ms = local_config.stream_delay_ms;
        if (miipc_aec_create(&aec_config, &self->aec) != 0) {
            printf("%s AEC init failed, use raw microphone audio\n",
                local_config.log_tag);
            fflush(stdout);
        }
    }
    self->stats.aec_active = self->aec != NULL ? 1 : 0;
    self->initialized = 1;
    printf("%s process ready aecm=%d ns=%d ns_policy=%d echo_mode=%d cng=%d delay_ms=%d log_ms=%d reset_limit=%d active=%d\n",
        local_config.log_tag,
        local_config.enable_aecm,
        local_config.enable_ns,
        local_config.ns_policy,
        local_config.aecm_echo_mode,
        local_config.aecm_cng_mode,
        local_config.stream_delay_ms,
        local_config.log_ms,
        local_config.reset_error_limit,
        self->stats.aec_active);
    fflush(stdout);

    *process = self;
    return 0;
}

void miipc_audio_process_destroy(miipc_audio_process_t* process)
{
    if (process == NULL) {
        return;
    }
    miipc_aec_destroy(process->aec);
    if (process->lock != NULL) {
        mi_DelMutex(process->lock);
    }
    free(process);
}

void miipc_audio_process_set_source_format(miipc_audio_process_t* process,
    int active,
    int sample_rate_hz,
    int channels,
    int bits_per_sample)
{
    if (process == NULL) {
        return;
    }
    audio_process_lock(process);
    process->stats.active = active ? 1 : 0;
    process->stats.sample_rate_hz = sample_rate_hz;
    process->stats.channels = channels;
    process->stats.bits_per_sample = bits_per_sample;
    audio_process_unlock(process);
}

static void audio_process_maybe_reset_after_error(miipc_audio_process_t* process,
    int ret)
{
    int should_reset = 0;
    int reset_ret;

    if (process == NULL || process->aec == NULL ||
        process->config.reset_error_limit <= 0) {
        return;
    }

    audio_process_lock(process);
    should_reset = process->consecutive_errors >= process->config.reset_error_limit;
    audio_process_unlock(process);
    if (!should_reset) {
        return;
    }

    reset_ret = miipc_aec_reset(process->aec);
    audio_process_lock(process);
    if (reset_ret == 0) {
        ++process->stats.aec_error_resets;
        process->consecutive_errors = 0;
    }
    process->stats.aec_last_process_ret = reset_ret == 0 ? ret : reset_ret;
    audio_process_unlock(process);

    printf("%s AEC reset after consecutive errors limit=%d last_ret=%d reset_ret=%d\n",
        process->config.log_tag,
        process->config.reset_error_limit,
        ret,
        reset_ret);
    fflush(stdout);
}

static void audio_process_maybe_log(miipc_audio_process_t* process,
    uint64_t pts_ms,
    int processed)
{
    miipc_audio_capture_stats_t stats;

    if (process == NULL || process->config.log_ms <= 0) {
        return;
    }
    if (process->last_log_ms != 0 &&
        pts_ms - process->last_log_ms < (uint64_t)process->config.log_ms) {
        return;
    }
    process->last_log_ms = pts_ms;
    memset(&stats, 0, sizeof(stats));
    if (miipc_audio_process_get_stats(process, &stats) != 0) {
        return;
    }

    printf("%s AEC stats active=%d processed=%d packets=%llu ok=%llu bypass=%llu err=%llu resets=%llu farend_calls=%llu farend_input=%llu farend_ready=%llu farend_pending=%llu in_level=%d out_level=%d suppress_q8=%d aecm=%d echo_mode=%d cng=%d ns=%d delay=%d last_proc=%d last_err=%d\n",
        process->config.log_tag,
        stats.aec_active,
        processed,
        (unsigned long long)stats.audio_packets,
        (unsigned long long)stats.aec_processed_packets,
        (unsigned long long)stats.aec_bypass_packets,
        (unsigned long long)stats.aec_process_errors,
        (unsigned long long)stats.aec_error_resets,
        (unsigned long long)stats.aec.farend_calls,
        (unsigned long long)stats.aec.farend_input_samples,
        (unsigned long long)stats.aec.farend_samples,
        (unsigned long long)stats.aec.farend_pending_samples,
        stats.last_input_level,
        stats.last_output_level,
        stats.last_suppression_q8,
        stats.aec.enable_aecm,
        stats.aec.aecm_echo_mode,
        stats.aec.aecm_cng_mode,
        stats.aec.enable_ns,
        stats.aec.stream_delay_ms,
        stats.aec_last_process_ret,
        stats.aec.last_error);
    fflush(stdout);
}

int miipc_audio_process_buffer_farend(miipc_audio_process_t* process,
    const int16_t* pcm,
    size_t samples,
    int sample_rate_hz)
{
    if (process == NULL || pcm == NULL || samples == 0) {
        return -1;
    }
    if (sample_rate_hz != process->config.sample_rate_hz) {
        return -2;
    }
    if (process->aec == NULL) {
        return 0;
    }
    return miipc_aec_buffer_farend(process->aec, pcm, samples);
}

int miipc_audio_process_capture(miipc_audio_process_t* process,
    const int16_t* input,
    int16_t* output,
    size_t samples,
    uint64_t pts_ms)
{
    int process_ret = 0;
    int processed = 0;
    int in_level;
    int out_level;
    int suppression_q8 = 256;

    if (process == NULL || input == NULL || output == NULL || samples == 0) {
        return -1;
    }

    if (process->aec != NULL) {
        process_ret = miipc_aec_process(process->aec, input, output, samples);
        if (process_ret == (int)samples) {
            processed = 1;
            process_ret = 0;
        }
    }
    if (!processed) {
        if (input != output) {
            memcpy(output, input, sizeof(int16_t) * samples);
        }
    }

    in_level = audio_process_average_abs(input, samples);
    out_level = audio_process_average_abs(output, samples);
    if (in_level > 0) {
        suppression_q8 = audio_process_clamp_int((out_level * 256) / in_level,
            0,
            4096);
    }

    audio_process_lock(process);
    ++process->stats.audio_packets;
    if (processed) {
        ++process->stats.aec_processed_packets;
        process->consecutive_errors = 0;
    } else {
        if (process->aec != NULL) {
            ++process->stats.aec_process_errors;
            ++process->consecutive_errors;
        }
        ++process->stats.aec_bypass_packets;
    }
    process->stats.last_input_level = in_level;
    process->stats.last_output_level = out_level;
    process->stats.last_suppression_q8 = suppression_q8;
    process->stats.aec_last_process_ret = process_ret;
    process->stats.aec_active = process->aec != NULL ? 1 : 0;
    audio_process_unlock(process);

    if (!processed && process->aec != NULL) {
        audio_process_maybe_reset_after_error(process, process_ret);
    }
    audio_process_maybe_log(process, pts_ms, processed);
    return (int)samples;
}

int miipc_audio_process_get_stats(miipc_audio_process_t* process,
    miipc_audio_capture_stats_t* stats)
{
    if (process == NULL || stats == NULL) {
        return -1;
    }
    memset(stats, 0, sizeof(*stats));
    audio_process_lock(process);
    *stats = process->stats;
    audio_process_unlock(process);
    if (process->aec != NULL) {
        miipc_aec_get_stats(process->aec, &stats->aec);
    }
    return 0;
}
