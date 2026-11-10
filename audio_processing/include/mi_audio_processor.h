#ifndef MI_AUDIO_PROCESSOR_H_
#define MI_AUDIO_PROCESSOR_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum mi_audio_processor_ns_mode_t {
  MI_AUDIO_PROCESSOR_NS_NONE = 0,
  MI_AUDIO_PROCESSOR_NS_FLOAT = 1,
  MI_AUDIO_PROCESSOR_NS_FIXED = 2
} mi_audio_processor_ns_mode_t;

typedef struct mi_audio_processor_config_t {
  int sample_rate_hz;
  size_t frame_samples;
  int enable_aecm;
  mi_audio_processor_ns_mode_t ns_mode;
  int ns_policy;
  int aecm_echo_mode;
  int aecm_cng_mode;
  int stream_delay_ms;
} mi_audio_processor_config_t;

typedef struct mi_audio_processor_t mi_audio_processor_t;

enum {
  MI_AUDIO_PROCESSOR_OK = 0,
  MI_AUDIO_PROCESSOR_ERR_INVALID_ARG = -1,
  MI_AUDIO_PROCESSOR_ERR_NO_MEMORY = -2,
  MI_AUDIO_PROCESSOR_ERR_UNSUPPORTED = -3,
  MI_AUDIO_PROCESSOR_ERR_PROCESS = -4
};

void mi_audio_processor_config_init(mi_audio_processor_config_t* config);

int mi_audio_processor_create(const mi_audio_processor_config_t* config,
                              mi_audio_processor_t** processor);

void mi_audio_processor_destroy(mi_audio_processor_t* processor);

int mi_audio_processor_set_stream_delay_ms(mi_audio_processor_t* processor,
                                           int delay_ms);

int mi_audio_processor_buffer_farend(mi_audio_processor_t* processor,
                                     const int16_t* farend,
                                     size_t samples);

int mi_audio_processor_process(mi_audio_processor_t* processor,
                               const int16_t* nearend,
                               const int16_t* farend,
                               int16_t* output,
                               size_t samples);

int mi_audio_processor_reset(mi_audio_processor_t* processor);

#ifdef __cplusplus
}
#endif

#endif
