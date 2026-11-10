#include "include/mi_audio_processor.h"

#include <stdlib.h>
#include <string.h>

#include "aecm/echo_control_mobile.h"
#include "ns/noise_suppression.h"
#include "ns/noise_suppression_x.h"

#ifndef MI_AUDIO_PROCESSING_ENABLE_AECM
#define MI_AUDIO_PROCESSING_ENABLE_AECM 1
#endif

#ifndef MI_AUDIO_PROCESSING_ENABLE_NS
#define MI_AUDIO_PROCESSING_ENABLE_NS 1
#endif

#ifndef MI_AUDIO_PROCESSING_ENABLE_NSX
#define MI_AUDIO_PROCESSING_ENABLE_NSX 1
#endif

struct mi_audio_processor_t {
  mi_audio_processor_config_t config;
  void* aecm;
  NsHandle* ns;
  NsxHandle* nsx;
  int16_t* stage;
  float* ns_float_in;
  float* ns_float_out;
};

static int mi_audio_processor_is_supported_rate(int sample_rate_hz) {
  return sample_rate_hz == 8000 || sample_rate_hz == 16000;
}

static int16_t mi_audio_processor_float_to_s16(float value) {
  if (value > 32767.0f) {
    return 32767;
  }
  if (value < -32768.0f) {
    return -32768;
  }
  return (int16_t)(value >= 0.0f ? value + 0.5f : value - 0.5f);
}

void mi_audio_processor_config_init(mi_audio_processor_config_t* config) {
  if (config == NULL) {
    return;
  }
  memset(config, 0, sizeof(*config));
  config->sample_rate_hz = 16000;
  config->frame_samples = 160;
  config->enable_aecm = 1;
  config->ns_mode = MI_AUDIO_PROCESSOR_NS_FIXED;
  config->ns_policy = 1;
  config->aecm_echo_mode = 3;
  config->aecm_cng_mode = AecmTrue;
  config->stream_delay_ms = 40;
}

static int mi_audio_processor_normalize_config(
    mi_audio_processor_config_t* config) {
  if (config == NULL) {
    return MI_AUDIO_PROCESSOR_ERR_INVALID_ARG;
  }
  if (!mi_audio_processor_is_supported_rate(config->sample_rate_hz)) {
    return MI_AUDIO_PROCESSOR_ERR_UNSUPPORTED;
  }
  if (config->frame_samples == 0) {
    config->frame_samples = (size_t)(config->sample_rate_hz / 100);
  }
  if (config->frame_samples != (size_t)(config->sample_rate_hz / 100)) {
    return MI_AUDIO_PROCESSOR_ERR_INVALID_ARG;
  }
  if (config->ns_policy < 0 || config->ns_policy > 3) {
    config->ns_policy = 1;
  }
  if (config->aecm_echo_mode < 0 || config->aecm_echo_mode > 4) {
    config->aecm_echo_mode = 3;
  }
  if (config->stream_delay_ms < 0) {
    config->stream_delay_ms = 0;
  }
  return MI_AUDIO_PROCESSOR_OK;
}

int mi_audio_processor_create(const mi_audio_processor_config_t* config,
                              mi_audio_processor_t** processor) {
  mi_audio_processor_config_t local_config;
  mi_audio_processor_t* self = NULL;
  int ret = MI_AUDIO_PROCESSOR_OK;

  if (processor == NULL) {
    return MI_AUDIO_PROCESSOR_ERR_INVALID_ARG;
  }
  *processor = NULL;

  if (config != NULL) {
    local_config = *config;
  } else {
    mi_audio_processor_config_init(&local_config);
  }

  ret = mi_audio_processor_normalize_config(&local_config);
  if (ret != MI_AUDIO_PROCESSOR_OK) {
    return ret;
  }

  if (local_config.enable_aecm && !MI_AUDIO_PROCESSING_ENABLE_AECM) {
    return MI_AUDIO_PROCESSOR_ERR_UNSUPPORTED;
  }
  if (local_config.ns_mode == MI_AUDIO_PROCESSOR_NS_FLOAT &&
      !MI_AUDIO_PROCESSING_ENABLE_NS) {
    return MI_AUDIO_PROCESSOR_ERR_UNSUPPORTED;
  }
  if (local_config.ns_mode == MI_AUDIO_PROCESSOR_NS_FIXED &&
      !MI_AUDIO_PROCESSING_ENABLE_NSX) {
    return MI_AUDIO_PROCESSOR_ERR_UNSUPPORTED;
  }

  self = (mi_audio_processor_t*)calloc(1, sizeof(*self));
  if (self == NULL) {
    return MI_AUDIO_PROCESSOR_ERR_NO_MEMORY;
  }
  self->config = local_config;

  self->stage =
      (int16_t*)malloc(sizeof(int16_t) * self->config.frame_samples);
  if (self->stage == NULL) {
    mi_audio_processor_destroy(self);
    return MI_AUDIO_PROCESSOR_ERR_NO_MEMORY;
  }

#if MI_AUDIO_PROCESSING_ENABLE_AECM
  if (self->config.enable_aecm) {
    AecmConfig aecm_config;
    self->aecm = WebRtcAecm_Create();
    if (self->aecm == NULL) {
      mi_audio_processor_destroy(self);
      return MI_AUDIO_PROCESSOR_ERR_NO_MEMORY;
    }
    if (WebRtcAecm_Init(self->aecm, self->config.sample_rate_hz) != 0) {
      mi_audio_processor_destroy(self);
      return MI_AUDIO_PROCESSOR_ERR_PROCESS;
    }
    aecm_config.cngMode = (int16_t)self->config.aecm_cng_mode;
    aecm_config.echoMode = (int16_t)self->config.aecm_echo_mode;
    if (WebRtcAecm_set_config(self->aecm, aecm_config) != 0) {
      mi_audio_processor_destroy(self);
      return MI_AUDIO_PROCESSOR_ERR_PROCESS;
    }
  }
#endif

#if MI_AUDIO_PROCESSING_ENABLE_NS
  if (self->config.ns_mode == MI_AUDIO_PROCESSOR_NS_FLOAT) {
    self->ns = WebRtcNs_Create();
    self->ns_float_in =
        (float*)malloc(sizeof(float) * self->config.frame_samples);
    self->ns_float_out =
        (float*)malloc(sizeof(float) * self->config.frame_samples);
    if (self->ns == NULL || self->ns_float_in == NULL ||
        self->ns_float_out == NULL) {
      mi_audio_processor_destroy(self);
      return MI_AUDIO_PROCESSOR_ERR_NO_MEMORY;
    }
    if (WebRtcNs_Init(self->ns, (uint32_t)self->config.sample_rate_hz) != 0 ||
        WebRtcNs_set_policy(self->ns, self->config.ns_policy) != 0) {
      mi_audio_processor_destroy(self);
      return MI_AUDIO_PROCESSOR_ERR_PROCESS;
    }
  }
#endif

#if MI_AUDIO_PROCESSING_ENABLE_NSX
  if (self->config.ns_mode == MI_AUDIO_PROCESSOR_NS_FIXED) {
    self->nsx = WebRtcNsx_Create();
    if (self->nsx == NULL) {
      mi_audio_processor_destroy(self);
      return MI_AUDIO_PROCESSOR_ERR_NO_MEMORY;
    }
    if (WebRtcNsx_Init(self->nsx, (uint32_t)self->config.sample_rate_hz) != 0 ||
        WebRtcNsx_set_policy(self->nsx, self->config.ns_policy) != 0) {
      mi_audio_processor_destroy(self);
      return MI_AUDIO_PROCESSOR_ERR_PROCESS;
    }
  }
#endif

  *processor = self;
  return MI_AUDIO_PROCESSOR_OK;
}

void mi_audio_processor_destroy(mi_audio_processor_t* processor) {
  if (processor == NULL) {
    return;
  }
#if MI_AUDIO_PROCESSING_ENABLE_AECM
  WebRtcAecm_Free(processor->aecm);
#endif
#if MI_AUDIO_PROCESSING_ENABLE_NS
  WebRtcNs_Free(processor->ns);
#endif
#if MI_AUDIO_PROCESSING_ENABLE_NSX
  WebRtcNsx_Free(processor->nsx);
#endif
  free(processor->stage);
  free(processor->ns_float_in);
  free(processor->ns_float_out);
  free(processor);
}

int mi_audio_processor_set_stream_delay_ms(mi_audio_processor_t* processor,
                                           int delay_ms) {
  if (processor == NULL || delay_ms < 0) {
    return MI_AUDIO_PROCESSOR_ERR_INVALID_ARG;
  }
  processor->config.stream_delay_ms = delay_ms;
  return MI_AUDIO_PROCESSOR_OK;
}

int mi_audio_processor_buffer_farend(mi_audio_processor_t* processor,
                                     const int16_t* farend,
                                     size_t samples) {
  if (processor == NULL || farend == NULL ||
      samples != processor->config.frame_samples) {
    return MI_AUDIO_PROCESSOR_ERR_INVALID_ARG;
  }
  if (!processor->config.enable_aecm) {
    return MI_AUDIO_PROCESSOR_OK;
  }
#if MI_AUDIO_PROCESSING_ENABLE_AECM
  if (WebRtcAecm_BufferFarend(processor->aecm, farend, samples) != 0) {
    return MI_AUDIO_PROCESSOR_ERR_PROCESS;
  }
  return MI_AUDIO_PROCESSOR_OK;
#else
  return MI_AUDIO_PROCESSOR_ERR_UNSUPPORTED;
#endif
}

static int mi_audio_processor_apply_ns_float(mi_audio_processor_t* processor,
                                             const int16_t* input,
                                             int16_t* output) {
#if MI_AUDIO_PROCESSING_ENABLE_NS
  size_t i = 0;
  const float* ns_in_bands[1];
  float* ns_out_bands[1];
  for (i = 0; i < processor->config.frame_samples; ++i) {
    processor->ns_float_in[i] = (float)input[i];
  }
  ns_in_bands[0] = processor->ns_float_in;
  ns_out_bands[0] = processor->ns_float_out;
  WebRtcNs_Analyze(processor->ns, processor->ns_float_in);
  WebRtcNs_Process(processor->ns, ns_in_bands, 1, ns_out_bands);
  for (i = 0; i < processor->config.frame_samples; ++i) {
    output[i] = mi_audio_processor_float_to_s16(processor->ns_float_out[i]);
  }
  return MI_AUDIO_PROCESSOR_OK;
#else
  (void)processor;
  (void)input;
  (void)output;
  return MI_AUDIO_PROCESSOR_ERR_UNSUPPORTED;
#endif
}

static int mi_audio_processor_apply_ns_fixed(mi_audio_processor_t* processor,
                                             const int16_t* input,
                                             int16_t* output) {
#if MI_AUDIO_PROCESSING_ENABLE_NSX
  const int16_t* nsx_in_bands[1];
  int16_t* nsx_out_bands[1];
  nsx_in_bands[0] = input;
  nsx_out_bands[0] = output;
  WebRtcNsx_Process(processor->nsx, nsx_in_bands, 1, nsx_out_bands);
  return MI_AUDIO_PROCESSOR_OK;
#else
  (void)processor;
  (void)input;
  (void)output;
  return MI_AUDIO_PROCESSOR_ERR_UNSUPPORTED;
#endif
}

int mi_audio_processor_process(mi_audio_processor_t* processor,
                               const int16_t* nearend,
                               const int16_t* farend,
                               int16_t* output,
                               size_t samples) {
  const int16_t* current = nearend;
  int ret = MI_AUDIO_PROCESSOR_OK;

  if (processor == NULL || nearend == NULL || output == NULL ||
      samples != processor->config.frame_samples) {
    return MI_AUDIO_PROCESSOR_ERR_INVALID_ARG;
  }

  if (farend != NULL) {
    ret = mi_audio_processor_buffer_farend(processor, farend, samples);
    if (ret != MI_AUDIO_PROCESSOR_OK) {
      return ret;
    }
  }

  if (processor->config.enable_aecm) {
#if MI_AUDIO_PROCESSING_ENABLE_AECM
    if (WebRtcAecm_Process(processor->aecm, nearend, NULL, processor->stage,
                           samples,
                           (int16_t)processor->config.stream_delay_ms) != 0) {
      return MI_AUDIO_PROCESSOR_ERR_PROCESS;
    }
    current = processor->stage;
#else
    return MI_AUDIO_PROCESSOR_ERR_UNSUPPORTED;
#endif
  }

  if (processor->config.ns_mode == MI_AUDIO_PROCESSOR_NS_FLOAT) {
    return mi_audio_processor_apply_ns_float(processor, current, output);
  }
  if (processor->config.ns_mode == MI_AUDIO_PROCESSOR_NS_FIXED) {
    return mi_audio_processor_apply_ns_fixed(processor, current, output);
  }

  if (current != output) {
    memcpy(output, current, sizeof(int16_t) * samples);
  }
  return MI_AUDIO_PROCESSOR_OK;
}

int mi_audio_processor_reset(mi_audio_processor_t* processor) {
  mi_audio_processor_config_t config;
  mi_audio_processor_t* new_processor = NULL;
  int ret = MI_AUDIO_PROCESSOR_OK;

  if (processor == NULL) {
    return MI_AUDIO_PROCESSOR_ERR_INVALID_ARG;
  }

  config = processor->config;
  ret = mi_audio_processor_create(&config, &new_processor);
  if (ret != MI_AUDIO_PROCESSOR_OK) {
    return ret;
  }

  processor->config = new_processor->config;

#if MI_AUDIO_PROCESSING_ENABLE_AECM
  WebRtcAecm_Free(processor->aecm);
  processor->aecm = new_processor->aecm;
  new_processor->aecm = NULL;
#endif
#if MI_AUDIO_PROCESSING_ENABLE_NS
  WebRtcNs_Free(processor->ns);
  processor->ns = new_processor->ns;
  new_processor->ns = NULL;
#endif
#if MI_AUDIO_PROCESSING_ENABLE_NSX
  WebRtcNsx_Free(processor->nsx);
  processor->nsx = new_processor->nsx;
  new_processor->nsx = NULL;
#endif

  free(processor->stage);
  processor->stage = new_processor->stage;
  new_processor->stage = NULL;

  free(processor->ns_float_in);
  processor->ns_float_in = new_processor->ns_float_in;
  new_processor->ns_float_in = NULL;

  free(processor->ns_float_out);
  processor->ns_float_out = new_processor->ns_float_out;
  new_processor->ns_float_out = NULL;

  mi_audio_processor_destroy(new_processor);
  return MI_AUDIO_PROCESSOR_OK;
}
