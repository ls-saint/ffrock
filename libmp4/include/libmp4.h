#ifndef LIBMP4_H_
#define LIBMP4_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIBMP4_WRITE_CACHE_MAX 8192u
#define LIBMP4_WRITE_CACHE_DEFAULT 8192u
#define LIBMP4_DEFAULT_TIMESCALE 1000u
#define LIBMP4_DEFAULT_MAX_SAMPLES 256u
#define LIBMP4_DEFAULT_MAX_TRACKS 4u
#define LIBMP4_READER_DEFAULT_MAX_TRACKS 4u
#define LIBMP4_READER_DEFAULT_MAX_SAMPLES 2048u

#define LIBMP4_FLAG_KEYFRAME 0x01u
#define LIBMP4_READ_FLAG_PREPEND_CONFIG 0x01u

typedef struct libmp4_writer libmp4_writer_t;
typedef struct libmp4_reader libmp4_reader_t;

typedef enum libmp4_codec {
    LIBMP4_CODEC_H264 = 1,
    LIBMP4_CODEC_H265 = 2,
    LIBMP4_CODEC_G711A = 3,
    LIBMP4_CODEC_G711U = 4
} libmp4_codec_t;

typedef struct libmp4_config {
    const char* path;
    /* Legacy single-H264-track fields. Leave width/height as 0 for explicit multi-track mode. */
    uint32_t width;
    uint32_t height;
    uint32_t fps_num;
    uint32_t fps_den;
    uint32_t timescale;
    uint32_t write_cache_size;
    uint32_t max_samples; /* default per-track sample cap */
    /* 0 means unlimited. Use only when the product wants a file-size cap. */
    uint64_t max_file_bytes;
    uint32_t max_tracks;
} libmp4_config_t;

typedef struct libmp4_reader_config {
    /* 0 selects the bounded defaults above. max_tracks is per file; max_samples is per track. */
    uint32_t max_tracks;
    uint32_t max_samples;
} libmp4_reader_config_t;

typedef struct libmp4_track_info {
    int track_id;
    libmp4_codec_t codec;
    uint32_t width;
    uint32_t height;
    uint32_t timescale;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits_per_sample;
    uint32_t sample_count;
    uint64_t duration_ms;
} libmp4_track_info_t;

typedef struct libmp4_sample_info {
    int track;
    uint32_t sample_index;
    uint32_t size;
    uint32_t duration_ms;
    uint64_t pts_ms;
    uint32_t flags;
} libmp4_sample_info_t;

enum {
    LIBMP4_OK = 0,
    LIBMP4_ERR_ARG = -1,
    LIBMP4_ERR_IO = -2,
    LIBMP4_ERR_NOMEM = -3,
    LIBMP4_ERR_FORMAT = -4,
    LIBMP4_ERR_LIMIT = -5
};

const char* libmp4_version(void);

libmp4_writer_t* libmp4_open(const libmp4_config_t* cfg);

/* Track add APIs return a track handle >= 0, or a negative LIBMP4_ERR_* code. */
int libmp4_add_h264_track(libmp4_writer_t* writer,
                          uint32_t width,
                          uint32_t height,
                          uint32_t fps_num,
                          uint32_t fps_den,
                          uint32_t max_samples);

int libmp4_add_h265_track(libmp4_writer_t* writer,
                          uint32_t width,
                          uint32_t height,
                          uint32_t fps_num,
                          uint32_t fps_den,
                          uint32_t max_samples);

int libmp4_add_g711a_track(libmp4_writer_t* writer,
                           uint32_t sample_rate,
                           uint32_t channels,
                           uint32_t max_samples);

int libmp4_add_g711u_track(libmp4_writer_t* writer,
                           uint32_t sample_rate,
                           uint32_t channels,
                           uint32_t max_samples);

/* Pass the track handle returned by libmp4_add_*_track(). */
int libmp4_write_h264_annexb_track(libmp4_writer_t* writer,
                                   int track,
                                   const void* data,
                                   size_t bytes,
                                   uint32_t duration_ms,
                                   uint32_t flags);

int libmp4_write_h265_annexb_track(libmp4_writer_t* writer,
                                   int track,
                                   const void* data,
                                   size_t bytes,
                                   uint32_t duration_ms,
                                   uint32_t flags);

int libmp4_write_g711_track(libmp4_writer_t* writer,
                            int track,
                            const void* data,
                            size_t bytes,
                            uint32_t duration_ms);

int libmp4_write_h264_annexb(libmp4_writer_t* writer,
                             const void* data,
                             size_t bytes,
                             uint32_t duration_ms,
                             uint32_t flags);

int libmp4_flush(libmp4_writer_t* writer);

int libmp4_close(libmp4_writer_t* writer);

void libmp4_abort(libmp4_writer_t* writer);

libmp4_reader_t* libmp4_reader_open(const char* path);

/*
 * Opens an MP4 with bounded reader allocations. The legacy open function uses
 * LIBMP4_READER_DEFAULT_MAX_TRACKS and LIBMP4_READER_DEFAULT_MAX_SAMPLES.
 */
libmp4_reader_t* libmp4_reader_open_with_config(const char* path,
                                                const libmp4_reader_config_t* cfg);

int libmp4_reader_track_count(libmp4_reader_t* reader);

int libmp4_reader_get_track_info(libmp4_reader_t* reader,
                                 int track,
                                 libmp4_track_info_t* info);

int libmp4_reader_get_sample_info(libmp4_reader_t* reader,
                                  int track,
                                  uint32_t sample_index,
                                  libmp4_sample_info_t* info);

/*
 * Reads one sample.
 * H.264/H.265 samples are returned as Annex-B. G.711 samples are raw bytes.
 * Use out=NULL or out_cap=0 to query the required out_bytes.
 */
int libmp4_reader_read_sample(libmp4_reader_t* reader,
                              int track,
                              uint32_t sample_index,
                              void* out,
                              size_t out_cap,
                              size_t* out_bytes,
                              libmp4_sample_info_t* info,
                              uint32_t flags);

void libmp4_reader_close(libmp4_reader_t* reader);

const char* libmp4_error_text(int error_code);

#ifdef __cplusplus
}
#endif

#endif /* LIBMP4_H_ */
