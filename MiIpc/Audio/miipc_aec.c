#include "miipc_aec.h"

#include <stdlib.h>
#include <string.h>

#include "mi_audio_processor.h"
#include "mimutex.h"

struct miipc_aec_t {
    mi_audio_processor_t* processor;
    miipc_aec_config_t config;
    miipc_aec_stats_t stats;
    mi_Mutex* lock;
    int16_t* frame_out;
    int16_t* farend_pending;
    size_t farend_pending_samples;
};

static void miipc_aec_lock(miipc_aec_t* aec)
{
    if (aec != NULL && aec->lock != NULL) {
        aec->lock->Lock(aec->lock);
    }
}

static void miipc_aec_unlock(miipc_aec_t* aec)
{
    if (aec != NULL && aec->lock != NULL) {
        aec->lock->UnLock(aec->lock);
    }
}

void miipc_aec_default_config(miipc_aec_config_t* config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->sample_rate_hz = 8000;
    config->frame_samples = 80;
    config->enable_aecm = 1;
    config->enable_ns = 1;
    config->ns_policy = 1;
    config->aecm_echo_mode = 3;
    config->aecm_cng_mode = 1;
    config->stream_delay_ms = 40;
}

static int miipc_aec_normalize_config(miipc_aec_config_t* config)
{
    if (config == NULL) {
        return -1;
    }
    if (config->sample_rate_hz != 8000 && config->sample_rate_hz != 16000) {
        return -1;
    }
    if (config->frame_samples == 0) {
        config->frame_samples = (size_t)(config->sample_rate_hz / 100);
    }
    if (config->frame_samples != (size_t)(config->sample_rate_hz / 100)) {
        return -1;
    }
    if (config->ns_policy < 0 || config->ns_policy > 3) {
        config->ns_policy = 1;
    }
    if (config->aecm_echo_mode < 0 || config->aecm_echo_mode > 4) {
        config->aecm_echo_mode = 3;
    }
    config->aecm_cng_mode = config->aecm_cng_mode ? 1 : 0;
    if (config->stream_delay_ms < 0) {
        config->stream_delay_ms = 0;
    }
    config->enable_aecm = config->enable_aecm ? 1 : 0;
    config->enable_ns = config->enable_ns ? 1 : 0;
    return 0;
}

int miipc_aec_create(const miipc_aec_config_t* config, miipc_aec_t** aec)
{
    miipc_aec_config_t local_config;
    mi_audio_processor_config_t processor_config;
    miipc_aec_t* self;
    int ret;

    if (aec == NULL) {
        return -1;
    }
    *aec = NULL;

    if (config != NULL) {
        local_config = *config;
    } else {
        miipc_aec_default_config(&local_config);
    }
    if (miipc_aec_normalize_config(&local_config) != 0) {
        return -1;
    }

    self = (miipc_aec_t*)calloc(1, sizeof(*self));
    if (self == NULL) {
        return -1;
    }
    self->config = local_config;
    self->stats.sample_rate_hz = local_config.sample_rate_hz;
    self->stats.frame_samples = (int)local_config.frame_samples;
    self->stats.enable_aecm = local_config.enable_aecm;
    self->stats.enable_ns = local_config.enable_ns;
    self->stats.ns_policy = local_config.ns_policy;
    self->stats.aecm_echo_mode = local_config.aecm_echo_mode;
    self->stats.aecm_cng_mode = local_config.aecm_cng_mode;
    self->stats.stream_delay_ms = local_config.stream_delay_ms;
    self->lock = mi_CreateMutex(1);
    self->frame_out = (int16_t*)malloc(sizeof(int16_t) * local_config.frame_samples);
    self->farend_pending = (int16_t*)malloc(sizeof(int16_t) * local_config.frame_samples);
    if (self->lock == NULL || self->frame_out == NULL || self->farend_pending == NULL) {
        miipc_aec_destroy(self);
        return -1;
    }

    mi_audio_processor_config_init(&processor_config);
    processor_config.sample_rate_hz = local_config.sample_rate_hz;
    processor_config.frame_samples = local_config.frame_samples;
    processor_config.enable_aecm = local_config.enable_aecm;
    processor_config.ns_mode = local_config.enable_ns ?
        MI_AUDIO_PROCESSOR_NS_FIXED : MI_AUDIO_PROCESSOR_NS_NONE;
    processor_config.ns_policy = local_config.ns_policy;
    processor_config.aecm_echo_mode = local_config.aecm_echo_mode;
    processor_config.aecm_cng_mode = local_config.aecm_cng_mode;
    processor_config.stream_delay_ms = local_config.stream_delay_ms;

    ret = mi_audio_processor_create(&processor_config, &self->processor);
    if (ret == MI_AUDIO_PROCESSOR_ERR_UNSUPPORTED &&
        processor_config.ns_mode != MI_AUDIO_PROCESSOR_NS_NONE) {
        processor_config.ns_mode = MI_AUDIO_PROCESSOR_NS_NONE;
        local_config.enable_ns = 0;
        self->config.enable_ns = 0;
        self->stats.enable_ns = 0;
        self->stats.ns_policy = 0;
        ret = mi_audio_processor_create(&processor_config, &self->processor);
    }
    if (ret != MI_AUDIO_PROCESSOR_OK) {
        miipc_aec_destroy(self);
        return ret;
    }

    *aec = self;
    return 0;
}

void miipc_aec_destroy(miipc_aec_t* aec)
{
    if (aec == NULL) {
        return;
    }
    mi_audio_processor_destroy(aec->processor);
    if (aec->lock != NULL) {
        mi_DelMutex(aec->lock);
    }
    free(aec->frame_out);
    free(aec->farend_pending);
    free(aec);
}

int miipc_aec_buffer_farend(miipc_aec_t* aec, const int16_t* farend, size_t samples)
{
    size_t pos = 0;
    size_t frame_samples;
    size_t delivered_samples = 0;

    if (aec == NULL || aec->processor == NULL || farend == NULL) {
        return -1;
    }

    frame_samples = aec->config.frame_samples;
    if (frame_samples == 0) {
        return -1;
    }

    miipc_aec_lock(aec);
    ++aec->stats.farend_calls;
    aec->stats.farend_input_samples += (uint64_t)samples;

    if (aec->farend_pending_samples > 0) {
        size_t need = frame_samples - aec->farend_pending_samples;
        size_t copy_samples = samples < need ? samples : need;

        memcpy(aec->farend_pending + aec->farend_pending_samples,
            farend,
            sizeof(int16_t) * copy_samples);
        aec->farend_pending_samples += copy_samples;
        pos += copy_samples;

        if (aec->farend_pending_samples == frame_samples) {
            int ret = mi_audio_processor_buffer_farend(aec->processor,
                aec->farend_pending,
                frame_samples);
            if (ret != MI_AUDIO_PROCESSOR_OK) {
                aec->stats.farend_samples += (uint64_t)delivered_samples;
                ++aec->stats.farend_errors;
                aec->stats.last_error = ret;
                miipc_aec_unlock(aec);
                return ret;
            }
            delivered_samples += frame_samples;
            aec->farend_pending_samples = 0;
        } else {
            aec->stats.farend_pending_samples =
                (uint64_t)aec->farend_pending_samples;
            miipc_aec_unlock(aec);
            return (int)samples;
        }
    }

    while (pos + frame_samples <= samples) {
        int ret = mi_audio_processor_buffer_farend(aec->processor,
            farend + pos,
            frame_samples);
        if (ret != MI_AUDIO_PROCESSOR_OK) {
            aec->stats.farend_samples += (uint64_t)delivered_samples;
            ++aec->stats.farend_errors;
            aec->stats.last_error = ret;
            miipc_aec_unlock(aec);
            return ret;
        }
        pos += frame_samples;
        delivered_samples += frame_samples;
    }
    aec->stats.farend_samples += (uint64_t)delivered_samples;
    if (pos < samples) {
        aec->farend_pending_samples = samples - pos;
        memcpy(aec->farend_pending,
            farend + pos,
            sizeof(int16_t) * aec->farend_pending_samples);
    }
    aec->stats.farend_pending_samples =
        (uint64_t)aec->farend_pending_samples;
    miipc_aec_unlock(aec);
    return (int)samples;
}

int miipc_aec_process(miipc_aec_t* aec, const int16_t* nearend, int16_t* output, size_t samples)
{
    size_t pos = 0;
    size_t frame_samples;

    if (aec == NULL || aec->processor == NULL || nearend == NULL || output == NULL) {
        return -1;
    }

    frame_samples = aec->config.frame_samples;
    if (frame_samples == 0) {
        return -1;
    }

    miipc_aec_lock(aec);
    ++aec->stats.process_calls;
    while (pos + frame_samples <= samples) {
        int ret = mi_audio_processor_process(aec->processor,
            nearend + pos,
            NULL,
            aec->frame_out,
            frame_samples);
        if (ret != MI_AUDIO_PROCESSOR_OK) {
            ++aec->stats.process_errors;
            aec->stats.last_error = ret;
            miipc_aec_unlock(aec);
            return ret;
        }
        memcpy(output + pos, aec->frame_out, sizeof(int16_t) * frame_samples);
        pos += frame_samples;
    }
    aec->stats.processed_samples += (uint64_t)pos;
    miipc_aec_unlock(aec);

    if (pos < samples) {
        memcpy(output + pos, nearend + pos, sizeof(int16_t) * (samples - pos));
        miipc_aec_lock(aec);
        aec->stats.bypass_samples += (uint64_t)(samples - pos);
        miipc_aec_unlock(aec);
        pos = samples;
    }
    return (int)pos;
}

int miipc_aec_reset(miipc_aec_t* aec)
{
    int ret;

    if (aec == NULL || aec->processor == NULL) {
        return -1;
    }

    miipc_aec_lock(aec);
    ret = mi_audio_processor_reset(aec->processor);
    if (ret == MI_AUDIO_PROCESSOR_OK) {
        ++aec->stats.reset_count;
        aec->farend_pending_samples = 0;
        aec->stats.farend_pending_samples = 0;
    } else {
        aec->stats.last_error = ret;
    }
    miipc_aec_unlock(aec);
    return ret;
}

int miipc_aec_set_stream_delay_ms(miipc_aec_t* aec, int delay_ms)
{
    int ret;

    if (aec == NULL || aec->processor == NULL || delay_ms < 0) {
        return -1;
    }

    miipc_aec_lock(aec);
    ret = mi_audio_processor_set_stream_delay_ms(aec->processor, delay_ms);
    if (ret == MI_AUDIO_PROCESSOR_OK) {
        aec->config.stream_delay_ms = delay_ms;
        aec->stats.stream_delay_ms = delay_ms;
    } else {
        aec->stats.last_error = ret;
    }
    miipc_aec_unlock(aec);
    return ret;
}

int miipc_aec_get_stats(miipc_aec_t* aec, miipc_aec_stats_t* stats)
{
    if (aec == NULL || stats == NULL) {
        return -1;
    }

    miipc_aec_lock(aec);
    *stats = aec->stats;
    miipc_aec_unlock(aec);
    return 0;
}
