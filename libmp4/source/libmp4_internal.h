#ifndef LIBMP4_INTERNAL_H_
#define LIBMP4_INTERNAL_H_

#include "libmp4.h"

#include <stdio.h>

#define LIBMP4_SPS_MAX 1024u
#define LIBMP4_PPS_MAX 512u
#define LIBMP4_VPS_MAX 512u
#define LIBMP4_SAMPLE_GROW 32u

typedef enum libmp4_track_kind {
    LIBMP4_TRACK_VIDEO = 1,
    LIBMP4_TRACK_AUDIO = 2
} libmp4_track_kind_t;

typedef struct libmp4_sample {
    uint64_t offset;
    uint32_t size;
    uint32_t duration;
    uint8_t keyframe;
} libmp4_sample_t;

typedef struct libmp4_track {
    int used;
    int track_id;
    libmp4_track_kind_t kind;
    libmp4_codec_t codec;
    uint32_t timescale;
    uint32_t width;
    uint32_t height;
    uint32_t fps_num;
    uint32_t fps_den;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits_per_sample;
    uint32_t max_samples;
    libmp4_sample_t* samples;
    uint32_t sample_count;
    uint32_t sample_capacity;
    uint32_t keyframe_count;
    uint64_t duration;
    uint8_t vps[LIBMP4_VPS_MAX];
    uint16_t vps_len;
    uint8_t sps[LIBMP4_SPS_MAX];
    uint16_t sps_len;
    uint8_t pps[LIBMP4_PPS_MAX];
    uint16_t pps_len;
} libmp4_track_t;

typedef struct libmp4_io {
    FILE* fp;
    uint8_t* cache;
    uint32_t cap;
    uint32_t len;
    uint64_t pos;
    int error;
} libmp4_io_t;

struct libmp4_writer {
    libmp4_config_t cfg;
    libmp4_io_t io;
    libmp4_track_t* tracks;
    uint32_t max_tracks;
    uint32_t track_count;
    int legacy_h264_track;
    uint64_t mdat_start;
    int closed;
    int error;
};

typedef struct libmp4_video_scan {
    libmp4_track_t* track;
    uint32_t sample_size;
    int keyframe;
    int vcl;
} libmp4_video_scan_t;

typedef int (*libmp4_nal_cb)(void* user, const uint8_t* nal, size_t len);

typedef struct libmp4_reader_sample {
    uint64_t offset;
    uint32_t size;
    uint32_t duration;
    uint8_t keyframe;
} libmp4_reader_sample_t;

typedef struct libmp4_reader_stts {
    uint32_t count;
    uint32_t duration;
} libmp4_reader_stts_t;

typedef struct libmp4_reader_stsc {
    uint32_t first_chunk;
    uint32_t samples_per_chunk;
    uint32_t sample_desc_index;
} libmp4_reader_stsc_t;

typedef struct libmp4_reader_track {
    int used;
    int kind;
    int track_id;
    libmp4_codec_t codec;
    uint32_t timescale;
    uint64_t duration;
    uint32_t width;
    uint32_t height;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits_per_sample;
    uint8_t nal_length_size;
    libmp4_reader_sample_t* samples;
    uint32_t sample_count;
    libmp4_reader_stts_t* stts;
    uint32_t stts_count;
    libmp4_reader_stsc_t* stsc;
    uint32_t stsc_count;
    uint64_t* chunk_offsets;
    uint32_t chunk_count;
    uint32_t* sync_samples;
    uint32_t sync_count;
    int has_stss;
    uint8_t vps[LIBMP4_VPS_MAX];
    uint16_t vps_len;
    uint8_t sps[LIBMP4_SPS_MAX];
    uint16_t sps_len;
    uint8_t pps[LIBMP4_PPS_MAX];
    uint16_t pps_len;
} libmp4_reader_track_t;

struct libmp4_reader {
    FILE* fp;
    uint64_t file_size;
    libmp4_reader_track_t* tracks;
    uint32_t track_count;
    uint32_t max_tracks;
    uint32_t max_samples;
    int error;
};

typedef struct libmp4_box {
    uint64_t start;
    uint64_t size;
    uint64_t header_size;
    uint64_t data;
    uint64_t end;
    char type[5];
} libmp4_box_t;

uint16_t libmp4_rb16(const uint8_t* p);
uint32_t libmp4_rb32(const uint8_t* p);
uint64_t libmp4_rb64(const uint8_t* p);
int libmp4_type_is(const char type[5], const char fourcc[4]);

int libmp4_io_write(libmp4_io_t* io, const void* data, size_t bytes);
int libmp4_write_u8(libmp4_writer_t* w, uint8_t v);
int libmp4_write_u16(libmp4_writer_t* w, uint16_t v);
int libmp4_write_u32(libmp4_writer_t* w, uint32_t v);
int libmp4_write_data(libmp4_writer_t* w, const void* data, size_t bytes);
int libmp4_write_zero(libmp4_writer_t* w, size_t bytes);
uint64_t libmp4_box_start(libmp4_writer_t* w, const char type[4]);
int libmp4_box_end(libmp4_writer_t* w, uint64_t offset);
int libmp4_write_fullbox(libmp4_writer_t* w, uint8_t version, uint32_t flags);

int libmp4_reader_read_at(FILE* fp, uint64_t offset, void* data, size_t bytes);

int libmp4_h264_scan_nal(void* user, const uint8_t* nal, size_t len);
int libmp4_h264_should_skip(const uint8_t* nal, size_t len);
int libmp4_h264_write_avcc(libmp4_writer_t* w, const libmp4_track_t* t);
int libmp4_h264_parse_avcc(libmp4_reader_t* reader,
                           libmp4_reader_track_t* track,
                           const libmp4_box_t* box);

int libmp4_h265_scan_nal(void* user, const uint8_t* nal, size_t len);
int libmp4_h265_should_skip(const uint8_t* nal, size_t len);
int libmp4_h265_write_hvcc(libmp4_writer_t* w, const libmp4_track_t* t);
int libmp4_h265_parse_hvcc(libmp4_reader_t* reader,
                           libmp4_reader_track_t* track,
                           const libmp4_box_t* box);

uint32_t libmp4_g711_duration(const libmp4_track_t* t,
                              size_t bytes,
                              uint32_t duration_ms);
int libmp4_g711_write_audio_stsd(libmp4_writer_t* w, const libmp4_track_t* t);
int libmp4_g711_parse_audio_entry(libmp4_reader_t* reader,
                                  libmp4_reader_track_t* track,
                                  const libmp4_box_t* entry);

#endif /* LIBMP4_INTERNAL_H_ */
