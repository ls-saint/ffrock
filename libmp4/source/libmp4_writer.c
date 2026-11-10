#include "libmp4_internal.h"

#include <stdlib.h>
#include <string.h>

static uint32_t libmp4_nonzero_u32(uint32_t value, uint32_t fallback)
{
    return value != 0 ? value : fallback;
}

static uint64_t libmp4_tell(libmp4_io_t* io)
{
    return io != NULL ? io->pos : 0;
}

static int libmp4_io_flush(libmp4_io_t* io)
{
    if (io == NULL || io->fp == NULL) {
        return LIBMP4_ERR_ARG;
    }
    if (io->error != LIBMP4_OK) {
        return io->error;
    }
    if (io->len > 0) {
        if (fwrite(io->cache, 1, io->len, io->fp) != io->len) {
            io->error = LIBMP4_ERR_IO;
            return io->error;
        }
        io->len = 0;
    }
    if (fflush(io->fp) != 0) {
        io->error = LIBMP4_ERR_IO;
        return io->error;
    }
    return LIBMP4_OK;
}

static int libmp4_io_seek(libmp4_io_t* io, uint64_t offset)
{
    if (io == NULL || io->fp == NULL) {
        return LIBMP4_ERR_ARG;
    }
    if (libmp4_io_flush(io) != LIBMP4_OK) {
        return io->error;
    }
    if (offset > 0x7fffffffu) {
        io->error = LIBMP4_ERR_LIMIT;
        return io->error;
    }
    if (fseek(io->fp, (long)offset, SEEK_SET) != 0) {
        io->error = LIBMP4_ERR_IO;
        return io->error;
    }
    io->pos = offset;
    return LIBMP4_OK;
}

int libmp4_io_write(libmp4_io_t* io, const void* data, size_t bytes)
{
    const uint8_t* p = (const uint8_t*)data;

    if (io == NULL || io->fp == NULL || (data == NULL && bytes != 0)) {
        return LIBMP4_ERR_ARG;
    }
    if (io->error != LIBMP4_OK) {
        return io->error;
    }
    while (bytes > 0) {
        size_t space = (size_t)io->cap - io->len;
        if (bytes >= io->cap) {
            if (libmp4_io_flush(io) != LIBMP4_OK) {
                return io->error;
            }
            if (fwrite(p, 1, bytes, io->fp) != bytes) {
                io->error = LIBMP4_ERR_IO;
                return io->error;
            }
            io->pos += (uint64_t)bytes;
            return LIBMP4_OK;
        }
        if (space == 0) {
            if (libmp4_io_flush(io) != LIBMP4_OK) {
                return io->error;
            }
            space = io->cap;
        }
        if (space > bytes) {
            space = bytes;
        }
        memcpy(io->cache + io->len, p, space);
        io->len += (uint32_t)space;
        io->pos += (uint64_t)space;
        p += space;
        bytes -= space;
    }
    return LIBMP4_OK;
}

int libmp4_write_u8(libmp4_writer_t* w, uint8_t v)
{
    return libmp4_io_write(&w->io, &v, 1);
}

int libmp4_write_u16(libmp4_writer_t* w, uint16_t v)
{
    uint8_t b[2];
    b[0] = (uint8_t)(v >> 8);
    b[1] = (uint8_t)v;
    return libmp4_io_write(&w->io, b, sizeof(b));
}

static int libmp4_write_u24(libmp4_writer_t* w, uint32_t v)
{
    uint8_t b[3];
    b[0] = (uint8_t)(v >> 16);
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)v;
    return libmp4_io_write(&w->io, b, sizeof(b));
}

int libmp4_write_u32(libmp4_writer_t* w, uint32_t v)
{
    uint8_t b[4];
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)v;
    return libmp4_io_write(&w->io, b, sizeof(b));
}

int libmp4_write_data(libmp4_writer_t* w, const void* data, size_t bytes)
{
    return libmp4_io_write(&w->io, data, bytes);
}

static int libmp4_write_fourcc(libmp4_writer_t* w, const char type[4])
{
    return libmp4_write_data(w, type, 4);
}

int libmp4_write_zero(libmp4_writer_t* w, size_t bytes)
{
    static const uint8_t zeros[32] = { 0 };
    while (bytes > 0) {
        size_t n = bytes > sizeof(zeros) ? sizeof(zeros) : bytes;
        int ret = libmp4_write_data(w, zeros, n);
        if (ret != LIBMP4_OK) {
            return ret;
        }
        bytes -= n;
    }
    return LIBMP4_OK;
}

uint64_t libmp4_box_start(libmp4_writer_t* w, const char type[4])
{
    uint64_t offset = libmp4_tell(&w->io);
    libmp4_write_u32(w, 0);
    libmp4_write_fourcc(w, type);
    return offset;
}

int libmp4_box_end(libmp4_writer_t* w, uint64_t offset)
{
    uint64_t end = libmp4_tell(&w->io);
    uint64_t size = end - offset;

    if (w->error != LIBMP4_OK) {
        return w->error;
    }
    if (w->io.error != LIBMP4_OK) {
        w->error = w->io.error;
        return w->error;
    }
    if (size > 0xffffffffu) {
        w->error = LIBMP4_ERR_LIMIT;
        return w->error;
    }
    if (libmp4_io_seek(&w->io, offset) != LIBMP4_OK) {
        w->error = w->io.error;
        return w->error;
    }
    libmp4_write_u32(w, (uint32_t)size);
    if (libmp4_io_seek(&w->io, end) != LIBMP4_OK) {
        w->error = w->io.error;
        return w->error;
    }
    return LIBMP4_OK;
}

int libmp4_write_fullbox(libmp4_writer_t* w, uint8_t version, uint32_t flags)
{
    int ret = libmp4_write_u8(w, version);
    if (ret == LIBMP4_OK) {
        ret = libmp4_write_u24(w, flags);
    }
    return ret;
}

static libmp4_track_t* libmp4_get_track(libmp4_writer_t* w, int track)
{
    if (w == NULL || track < 0 || (uint32_t)track >= w->track_count) {
        return NULL;
    }
    if (!w->tracks[track].used) {
        return NULL;
    }
    return &w->tracks[track];
}

static uint32_t libmp4_track_default_duration(const libmp4_track_t* t)
{
    uint64_t num;
    uint64_t duration;

    if (t == NULL) {
        return 1;
    }
    if (t->kind == LIBMP4_TRACK_AUDIO) {
        return t->sample_rate > 0 ? t->sample_rate / 50u : 160u;
    }
    if (t->fps_num == 0) {
        return 1;
    }
    num = (uint64_t)t->timescale * (uint64_t)t->fps_den;
    duration = (num + (t->fps_num / 2u)) / t->fps_num;
    if (duration == 0) {
        duration = 1;
    }
    if (duration > 0xffffffffu) {
        duration = 0xffffffffu;
    }
    return (uint32_t)duration;
}

static uint32_t libmp4_track_duration_from_ms(const libmp4_track_t* t, uint32_t duration_ms)
{
    uint64_t duration;

    if (duration_ms == 0) {
        return libmp4_track_default_duration(t);
    }
    duration = ((uint64_t)duration_ms * (uint64_t)t->timescale + 500u) / 1000u;
    if (duration == 0) {
        duration = 1;
    }
    if (duration > 0xffffffffu) {
        duration = 0xffffffffu;
    }
    return (uint32_t)duration;
}

static int libmp4_ensure_sample(libmp4_track_t* t)
{
    uint32_t next_capacity;
    void* p;

    if (t == NULL) {
        return LIBMP4_ERR_ARG;
    }
    if (t->sample_count >= t->max_samples) {
        return LIBMP4_ERR_LIMIT;
    }
    if (t->sample_count < t->sample_capacity) {
        return LIBMP4_OK;
    }
    next_capacity = t->sample_capacity + LIBMP4_SAMPLE_GROW;
    if (next_capacity > t->max_samples) {
        next_capacity = t->max_samples;
    }
    p = realloc(t->samples, (size_t)next_capacity * sizeof(t->samples[0]));
    if (p == NULL) {
        return LIBMP4_ERR_NOMEM;
    }
    t->samples = (libmp4_sample_t*)p;
    t->sample_capacity = next_capacity;
    return LIBMP4_OK;
}

static uint64_t libmp4_estimate_tail(const libmp4_writer_t* w)
{
    uint64_t bytes = 4096u;
    uint32_t i;

    if (w == NULL) {
        return bytes;
    }
    for (i = 0; i < w->track_count; ++i) {
        const libmp4_track_t* t = &w->tracks[i];
        if (!t->used) {
            continue;
        }
        bytes += (uint64_t)(t->sample_count + 1u) * 20u;
        bytes += (uint64_t)(t->keyframe_count + 1u) * 4u;
        bytes += t->vps_len + t->sps_len + t->pps_len;
    }
    return bytes;
}

static int libmp4_check_file_limit(libmp4_writer_t* w, uint32_t sample_size)
{
    uint64_t predicted;

    if (w == NULL || w->cfg.max_file_bytes == 0) {
        return LIBMP4_OK;
    }
    predicted = libmp4_tell(&w->io) + sample_size + libmp4_estimate_tail(w);
    if (predicted > w->cfg.max_file_bytes) {
        return LIBMP4_ERR_LIMIT;
    }
    return LIBMP4_OK;
}

static int libmp4_add_sample(libmp4_writer_t* w,
                             libmp4_track_t* t,
                             uint64_t offset,
                             uint32_t size,
                             uint32_t duration,
                             int keyframe)
{
    libmp4_sample_t* sample;
    int ret;

    ret = libmp4_ensure_sample(t);
    if (ret != LIBMP4_OK) {
        w->error = ret;
        return ret;
    }
    sample = &t->samples[t->sample_count++];
    sample->offset = offset;
    sample->size = size;
    sample->duration = duration;
    sample->keyframe = keyframe ? 1u : 0u;
    if (sample->keyframe) {
        ++t->keyframe_count;
    }
    t->duration += duration;
    return LIBMP4_OK;
}

static const uint8_t* libmp4_find_start_code(const uint8_t* p, const uint8_t* end, int* code_len)
{
    while (p + 3 <= end) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1) {
            if (code_len != NULL) {
                *code_len = 3;
            }
            return p;
        }
        if (p + 4 <= end && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1) {
            if (code_len != NULL) {
                *code_len = 4;
            }
            return p;
        }
        ++p;
    }
    return NULL;
}

static void libmp4_trim_nal(const uint8_t** nal, size_t* len)
{
    while (*len > 0 && (*nal)[*len - 1u] == 0) {
        --(*len);
    }
}

static int libmp4_for_each_annexb_nal(const void* data,
                                      size_t bytes,
                                      libmp4_nal_cb cb,
                                      void* user)
{
    const uint8_t* begin = (const uint8_t*)data;
    const uint8_t* end = begin + bytes;
    const uint8_t* sc;
    int code_len = 0;

    if (data == NULL || bytes == 0 || cb == NULL) {
        return LIBMP4_ERR_ARG;
    }
    sc = libmp4_find_start_code(begin, end, &code_len);
    if (sc == NULL) {
        const uint8_t* nal = begin;
        size_t len = bytes;
        libmp4_trim_nal(&nal, &len);
        return len > 0 ? cb(user, nal, len) : LIBMP4_OK;
    }
    while (sc != NULL) {
        const uint8_t* nal = sc + code_len;
        const uint8_t* next;
        int next_code_len = 0;
        size_t len;

        next = libmp4_find_start_code(nal, end, &next_code_len);
        len = (size_t)((next != NULL ? next : end) - nal);
        libmp4_trim_nal(&nal, &len);
        if (len > 0) {
            int ret = cb(user, nal, len);
            if (ret != LIBMP4_OK) {
                return ret;
            }
        }
        sc = next;
        code_len = next_code_len;
    }
    return LIBMP4_OK;
}

typedef struct libmp4_video_write {
    libmp4_writer_t* writer;
    libmp4_codec_t codec;
    uint32_t written;
} libmp4_video_write_t;

static int libmp4_write_video_nal(void* user, const uint8_t* nal, size_t len)
{
    libmp4_video_write_t* ctx = (libmp4_video_write_t*)user;
    int skip;
    int ret;

    if (ctx == NULL || ctx->writer == NULL || len == 0) {
        return LIBMP4_OK;
    }
    skip = ctx->codec == LIBMP4_CODEC_H264 ?
           libmp4_h264_should_skip(nal, len) :
           libmp4_h265_should_skip(nal, len);
    if (skip) {
        return LIBMP4_OK;
    }
    if (len > 0xffffffffu) {
        return LIBMP4_ERR_LIMIT;
    }
    ret = libmp4_write_u32(ctx->writer, (uint32_t)len);
    if (ret == LIBMP4_OK) {
        ret = libmp4_write_data(ctx->writer, nal, len);
    }
    if (ret == LIBMP4_OK) {
        ctx->written += (uint32_t)len + 4u;
    }
    return ret;
}

static int libmp4_write_video_annexb(libmp4_writer_t* w,
                                     int track,
                                     libmp4_codec_t codec,
                                     const void* data,
                                     size_t bytes,
                                     uint32_t duration_ms,
                                     uint32_t flags)
{
    libmp4_track_t* t;
    libmp4_video_scan_t scan;
    libmp4_video_write_t write_ctx;
    uint32_t duration;
    uint64_t offset;
    int ret;

    t = libmp4_get_track(w, track);
    if (t == NULL || data == NULL || bytes == 0 || w->closed ||
        t->kind != LIBMP4_TRACK_VIDEO || t->codec != codec) {
        return LIBMP4_ERR_ARG;
    }
    memset(&scan, 0, sizeof(scan));
    scan.track = t;
    ret = libmp4_for_each_annexb_nal(data, bytes,
                                     codec == LIBMP4_CODEC_H264 ?
                                     libmp4_h264_scan_nal : libmp4_h265_scan_nal,
                                     &scan);
    if (ret != LIBMP4_OK) {
        w->error = ret;
        return ret;
    }
    if (!scan.vcl || scan.sample_size == 0) {
        return LIBMP4_OK;
    }
    ret = libmp4_check_file_limit(w, scan.sample_size);
    if (ret != LIBMP4_OK) {
        w->error = ret;
        return ret;
    }
    duration = libmp4_track_duration_from_ms(t, duration_ms);
    offset = libmp4_tell(&w->io);
    memset(&write_ctx, 0, sizeof(write_ctx));
    write_ctx.writer = w;
    write_ctx.codec = codec;
    ret = libmp4_for_each_annexb_nal(data, bytes, libmp4_write_video_nal, &write_ctx);
    if (ret != LIBMP4_OK || write_ctx.written != scan.sample_size) {
        w->error = ret != LIBMP4_OK ? ret : LIBMP4_ERR_IO;
        return w->error;
    }
    return libmp4_add_sample(w, t, offset, write_ctx.written, duration,
                             scan.keyframe || (flags & LIBMP4_FLAG_KEYFRAME));
}

static uint64_t libmp4_track_movie_duration(const libmp4_writer_t* w,
                                            const libmp4_track_t* t)
{
    uint32_t movie_ts = libmp4_nonzero_u32(w->cfg.timescale, LIBMP4_DEFAULT_TIMESCALE);
    if (t == NULL || t->timescale == 0) {
        return 0;
    }
    return (t->duration * movie_ts + (t->timescale / 2u)) / t->timescale;
}

static uint64_t libmp4_movie_duration(const libmp4_writer_t* w)
{
    uint64_t duration = 0;
    uint32_t i;

    for (i = 0; i < w->track_count; ++i) {
        const libmp4_track_t* t = &w->tracks[i];
        uint64_t d;
        if (!t->used || t->sample_count == 0) {
            continue;
        }
        d = libmp4_track_movie_duration(w, t);
        if (d > duration) {
            duration = d;
        }
    }
    return duration;
}

static int libmp4_write_ftyp(libmp4_writer_t* w)
{
    uint64_t box = libmp4_box_start(w, "ftyp");
    libmp4_write_fourcc(w, "isom");
    libmp4_write_u32(w, 0x00000200u);
    libmp4_write_fourcc(w, "isom");
    libmp4_write_fourcc(w, "iso2");
    libmp4_write_fourcc(w, "avc1");
    libmp4_write_fourcc(w, "hvc1");
    libmp4_write_fourcc(w, "mp41");
    return libmp4_box_end(w, box);
}

static int libmp4_write_mvhd(libmp4_writer_t* w)
{
    static const uint32_t matrix[9] = {
        0x00010000u, 0, 0,
        0, 0x00010000u, 0,
        0, 0, 0x40000000u
    };
    uint64_t box = libmp4_box_start(w, "mvhd");
    uint32_t movie_ts = libmp4_nonzero_u32(w->cfg.timescale, LIBMP4_DEFAULT_TIMESCALE);
    int i;

    libmp4_write_fullbox(w, 0, 0);
    libmp4_write_u32(w, 0);
    libmp4_write_u32(w, 0);
    libmp4_write_u32(w, movie_ts);
    libmp4_write_u32(w, (uint32_t)libmp4_movie_duration(w));
    libmp4_write_u32(w, 0x00010000u);
    libmp4_write_u16(w, 0x0100u);
    libmp4_write_u16(w, 0);
    libmp4_write_zero(w, 8);
    for (i = 0; i < 9; ++i) {
        libmp4_write_u32(w, matrix[i]);
    }
    libmp4_write_zero(w, 24);
    libmp4_write_u32(w, w->track_count + 1u);
    return libmp4_box_end(w, box);
}

static int libmp4_write_tkhd(libmp4_writer_t* w, const libmp4_track_t* t)
{
    static const uint32_t matrix[9] = {
        0x00010000u, 0, 0,
        0, 0x00010000u, 0,
        0, 0, 0x40000000u
    };
    uint64_t box = libmp4_box_start(w, "tkhd");
    int i;

    libmp4_write_fullbox(w, 0, 0x000007u);
    libmp4_write_u32(w, 0);
    libmp4_write_u32(w, 0);
    libmp4_write_u32(w, (uint32_t)t->track_id);
    libmp4_write_u32(w, 0);
    libmp4_write_u32(w, (uint32_t)libmp4_track_movie_duration(w, t));
    libmp4_write_zero(w, 8);
    libmp4_write_u16(w, 0);
    libmp4_write_u16(w, 0);
    libmp4_write_u16(w, t->kind == LIBMP4_TRACK_AUDIO ? 0x0100u : 0);
    libmp4_write_u16(w, 0);
    for (i = 0; i < 9; ++i) {
        libmp4_write_u32(w, matrix[i]);
    }
    libmp4_write_u32(w, t->kind == LIBMP4_TRACK_VIDEO ? (t->width << 16) : 0);
    libmp4_write_u32(w, t->kind == LIBMP4_TRACK_VIDEO ? (t->height << 16) : 0);
    return libmp4_box_end(w, box);
}

static int libmp4_write_mdhd(libmp4_writer_t* w, const libmp4_track_t* t)
{
    uint64_t box = libmp4_box_start(w, "mdhd");

    libmp4_write_fullbox(w, 0, 0);
    libmp4_write_u32(w, 0);
    libmp4_write_u32(w, 0);
    libmp4_write_u32(w, t->timescale);
    libmp4_write_u32(w, (uint32_t)t->duration);
    libmp4_write_u16(w, 0x55c4u);
    libmp4_write_u16(w, 0);
    return libmp4_box_end(w, box);
}

static int libmp4_write_hdlr(libmp4_writer_t* w, const libmp4_track_t* t)
{
    const char video_name[] = "VideoHandler";
    const char audio_name[] = "SoundHandler";
    uint64_t box = libmp4_box_start(w, "hdlr");

    libmp4_write_fullbox(w, 0, 0);
    libmp4_write_u32(w, 0);
    libmp4_write_fourcc(w, t->kind == LIBMP4_TRACK_VIDEO ? "vide" : "soun");
    libmp4_write_zero(w, 12);
    if (t->kind == LIBMP4_TRACK_VIDEO) {
        libmp4_write_data(w, video_name, sizeof(video_name));
    } else {
        libmp4_write_data(w, audio_name, sizeof(audio_name));
    }
    return libmp4_box_end(w, box);
}

static int libmp4_write_vmhd(libmp4_writer_t* w)
{
    uint64_t box = libmp4_box_start(w, "vmhd");
    libmp4_write_fullbox(w, 0, 1);
    libmp4_write_zero(w, 8);
    return libmp4_box_end(w, box);
}

static int libmp4_write_smhd(libmp4_writer_t* w)
{
    uint64_t box = libmp4_box_start(w, "smhd");
    libmp4_write_fullbox(w, 0, 0);
    libmp4_write_u16(w, 0);
    libmp4_write_u16(w, 0);
    return libmp4_box_end(w, box);
}

static int libmp4_write_dinf(libmp4_writer_t* w)
{
    uint64_t dinf = libmp4_box_start(w, "dinf");
    uint64_t dref = libmp4_box_start(w, "dref");
    uint64_t url;

    libmp4_write_fullbox(w, 0, 0);
    libmp4_write_u32(w, 1);
    url = libmp4_box_start(w, "url ");
    libmp4_write_fullbox(w, 0, 1);
    libmp4_box_end(w, url);
    libmp4_box_end(w, dref);
    return libmp4_box_end(w, dinf);
}

static int libmp4_write_video_stsd(libmp4_writer_t* w, const libmp4_track_t* t)
{
    uint64_t stsd = libmp4_box_start(w, "stsd");
    uint64_t sample;

    libmp4_write_fullbox(w, 0, 0);
    libmp4_write_u32(w, 1);
    sample = libmp4_box_start(w, t->codec == LIBMP4_CODEC_H265 ? "hvc1" : "avc1");
    libmp4_write_zero(w, 6);
    libmp4_write_u16(w, 1);
    libmp4_write_zero(w, 16);
    libmp4_write_u16(w, (uint16_t)t->width);
    libmp4_write_u16(w, (uint16_t)t->height);
    libmp4_write_u32(w, 0x00480000u);
    libmp4_write_u32(w, 0x00480000u);
    libmp4_write_u32(w, 0);
    libmp4_write_u16(w, 1);
    libmp4_write_zero(w, 32);
    libmp4_write_u16(w, 0x0018u);
    libmp4_write_u16(w, 0xffffu);
    if (t->codec == LIBMP4_CODEC_H265) {
        libmp4_h265_write_hvcc(w, t);
    } else {
        libmp4_h264_write_avcc(w, t);
    }
    libmp4_box_end(w, sample);
    return libmp4_box_end(w, stsd);
}

static uint32_t libmp4_count_stts_entries(const libmp4_track_t* t)
{
    uint32_t count = 0;
    uint32_t i;
    uint32_t last = 0;

    for (i = 0; i < t->sample_count; ++i) {
        if (i == 0 || t->samples[i].duration != last) {
            ++count;
            last = t->samples[i].duration;
        }
    }
    return count;
}

static int libmp4_write_stts(libmp4_writer_t* w, const libmp4_track_t* t)
{
    uint64_t box = libmp4_box_start(w, "stts");
    uint32_t i = 0;

    libmp4_write_fullbox(w, 0, 0);
    libmp4_write_u32(w, libmp4_count_stts_entries(t));
    while (i < t->sample_count) {
        uint32_t duration = t->samples[i].duration;
        uint32_t count = 1;
        ++i;
        while (i < t->sample_count && t->samples[i].duration == duration) {
            ++count;
            ++i;
        }
        libmp4_write_u32(w, count);
        libmp4_write_u32(w, duration);
    }
    return libmp4_box_end(w, box);
}

static int libmp4_write_stsc(libmp4_writer_t* w)
{
    uint64_t box = libmp4_box_start(w, "stsc");
    libmp4_write_fullbox(w, 0, 0);
    libmp4_write_u32(w, 1);
    libmp4_write_u32(w, 1);
    libmp4_write_u32(w, 1);
    libmp4_write_u32(w, 1);
    return libmp4_box_end(w, box);
}

static int libmp4_write_stsz(libmp4_writer_t* w, const libmp4_track_t* t)
{
    uint64_t box = libmp4_box_start(w, "stsz");
    uint32_t i;

    libmp4_write_fullbox(w, 0, 0);
    libmp4_write_u32(w, 0);
    libmp4_write_u32(w, t->sample_count);
    for (i = 0; i < t->sample_count; ++i) {
        libmp4_write_u32(w, t->samples[i].size);
    }
    return libmp4_box_end(w, box);
}

static int libmp4_write_stco(libmp4_writer_t* w, const libmp4_track_t* t)
{
    uint64_t box = libmp4_box_start(w, "stco");
    uint32_t i;

    libmp4_write_fullbox(w, 0, 0);
    libmp4_write_u32(w, t->sample_count);
    for (i = 0; i < t->sample_count; ++i) {
        if (t->samples[i].offset > 0xffffffffu) {
            w->error = LIBMP4_ERR_LIMIT;
            return w->error;
        }
        libmp4_write_u32(w, (uint32_t)t->samples[i].offset);
    }
    return libmp4_box_end(w, box);
}

static int libmp4_write_stss(libmp4_writer_t* w, const libmp4_track_t* t)
{
    uint64_t box;
    uint32_t i;

    if (t->kind != LIBMP4_TRACK_VIDEO ||
        t->keyframe_count == 0 ||
        t->keyframe_count == t->sample_count) {
        return LIBMP4_OK;
    }
    box = libmp4_box_start(w, "stss");
    libmp4_write_fullbox(w, 0, 0);
    libmp4_write_u32(w, t->keyframe_count);
    for (i = 0; i < t->sample_count; ++i) {
        if (t->samples[i].keyframe) {
            libmp4_write_u32(w, i + 1u);
        }
    }
    return libmp4_box_end(w, box);
}

static int libmp4_write_stbl(libmp4_writer_t* w, const libmp4_track_t* t)
{
    uint64_t box = libmp4_box_start(w, "stbl");

    if (t->kind == LIBMP4_TRACK_VIDEO) {
        libmp4_write_video_stsd(w, t);
    } else {
        libmp4_g711_write_audio_stsd(w, t);
    }
    libmp4_write_stts(w, t);
    libmp4_write_stsc(w);
    libmp4_write_stsz(w, t);
    libmp4_write_stco(w, t);
    libmp4_write_stss(w, t);
    return libmp4_box_end(w, box);
}

static int libmp4_write_minf(libmp4_writer_t* w, const libmp4_track_t* t)
{
    uint64_t box = libmp4_box_start(w, "minf");

    if (t->kind == LIBMP4_TRACK_VIDEO) {
        libmp4_write_vmhd(w);
    } else {
        libmp4_write_smhd(w);
    }
    libmp4_write_dinf(w);
    libmp4_write_stbl(w, t);
    return libmp4_box_end(w, box);
}

static int libmp4_write_mdia(libmp4_writer_t* w, const libmp4_track_t* t)
{
    uint64_t box = libmp4_box_start(w, "mdia");

    libmp4_write_mdhd(w, t);
    libmp4_write_hdlr(w, t);
    libmp4_write_minf(w, t);
    return libmp4_box_end(w, box);
}

static int libmp4_write_trak(libmp4_writer_t* w, const libmp4_track_t* t)
{
    uint64_t box = libmp4_box_start(w, "trak");

    libmp4_write_tkhd(w, t);
    libmp4_write_mdia(w, t);
    return libmp4_box_end(w, box);
}

static int libmp4_write_moov(libmp4_writer_t* w)
{
    uint64_t box = libmp4_box_start(w, "moov");
    uint32_t i;

    libmp4_write_mvhd(w);
    for (i = 0; i < w->track_count; ++i) {
        if (w->tracks[i].used && w->tracks[i].sample_count > 0) {
            libmp4_write_trak(w, &w->tracks[i]);
        }
    }
    return libmp4_box_end(w, box);
}

static int libmp4_patch_mdat(libmp4_writer_t* w)
{
    uint64_t end = libmp4_tell(&w->io);
    uint64_t size = end - w->mdat_start;

    if (size > 0xffffffffu) {
        return LIBMP4_ERR_LIMIT;
    }
    if (libmp4_io_seek(&w->io, w->mdat_start) != LIBMP4_OK) {
        return w->io.error;
    }
    libmp4_write_u32(w, (uint32_t)size);
    return libmp4_io_seek(&w->io, end);
}

static int libmp4_add_track(libmp4_writer_t* w,
                            libmp4_codec_t codec,
                            libmp4_track_kind_t kind,
                            uint32_t width,
                            uint32_t height,
                            uint32_t fps_num,
                            uint32_t fps_den,
                            uint32_t sample_rate,
                            uint32_t channels,
                            uint32_t max_samples)
{
    libmp4_track_t* t;

    if (w == NULL) {
        return LIBMP4_ERR_ARG;
    }
    if (w->closed || w->track_count >= w->max_tracks) {
        return LIBMP4_ERR_LIMIT;
    }
    if (kind == LIBMP4_TRACK_VIDEO &&
        (width == 0 || height == 0 || width > 0xffffu || height > 0xffffu)) {
        return LIBMP4_ERR_ARG;
    }
    if (kind == LIBMP4_TRACK_AUDIO &&
        (sample_rate == 0 || sample_rate > 0xffffu ||
         channels == 0 || channels > 0xffffu)) {
        return LIBMP4_ERR_ARG;
    }
    t = &w->tracks[w->track_count];
    memset(t, 0, sizeof(*t));
    t->used = 1;
    t->track_id = (int)w->track_count + 1;
    t->kind = kind;
    t->codec = codec;
    t->width = width;
    t->height = height;
    t->fps_num = libmp4_nonzero_u32(fps_num, 5);
    t->fps_den = libmp4_nonzero_u32(fps_den, 1);
    t->sample_rate = sample_rate;
    t->channels = libmp4_nonzero_u32(channels, 1);
    t->bits_per_sample = 8;
    t->max_samples = libmp4_nonzero_u32(max_samples, w->cfg.max_samples);
    if (t->max_samples == 0) {
        t->max_samples = LIBMP4_DEFAULT_MAX_SAMPLES;
    }
    if (kind == LIBMP4_TRACK_AUDIO) {
        t->timescale = sample_rate;
    } else {
        t->timescale = libmp4_nonzero_u32(w->cfg.timescale, LIBMP4_DEFAULT_TIMESCALE);
    }
    return (int)w->track_count++;
}

libmp4_writer_t* libmp4_open(const libmp4_config_t* cfg)
{
    libmp4_writer_t* w;
    uint32_t cache_size;

    if (cfg == NULL || cfg->path == NULL || cfg->path[0] == '\0') {
        return NULL;
    }
    w = (libmp4_writer_t*)calloc(1, sizeof(*w));
    if (w == NULL) {
        return NULL;
    }
    w->cfg = *cfg;
    w->cfg.timescale = libmp4_nonzero_u32(w->cfg.timescale, LIBMP4_DEFAULT_TIMESCALE);
    w->cfg.max_samples = libmp4_nonzero_u32(w->cfg.max_samples, LIBMP4_DEFAULT_MAX_SAMPLES);
    w->max_tracks = libmp4_nonzero_u32(w->cfg.max_tracks, LIBMP4_DEFAULT_MAX_TRACKS);
    w->legacy_h264_track = -1;
    cache_size = libmp4_nonzero_u32(w->cfg.write_cache_size, LIBMP4_WRITE_CACHE_DEFAULT);
    if (cache_size > LIBMP4_WRITE_CACHE_MAX) {
        cache_size = LIBMP4_WRITE_CACHE_MAX;
    }
    if (cache_size < 1024u) {
        cache_size = 1024u;
    }
    w->cfg.write_cache_size = cache_size;
    w->tracks = (libmp4_track_t*)calloc(w->max_tracks, sizeof(w->tracks[0]));
    w->io.cache = (uint8_t*)malloc(cache_size);
    if (w->tracks == NULL || w->io.cache == NULL) {
        libmp4_abort(w);
        return NULL;
    }
    w->io.cap = cache_size;
    w->io.error = LIBMP4_OK;
    w->io.fp = fopen(cfg->path, "wb+");
    if (w->io.fp == NULL) {
        libmp4_abort(w);
        return NULL;
    }
    setvbuf(w->io.fp, NULL, _IONBF, 0);
    w->error = LIBMP4_OK;
    if (libmp4_write_ftyp(w) != LIBMP4_OK) {
        libmp4_abort(w);
        return NULL;
    }
    w->mdat_start = libmp4_tell(&w->io);
    libmp4_write_u32(w, 0);
    libmp4_write_fourcc(w, "mdat");
    if (cfg->width != 0 && cfg->height != 0) {
        int track = libmp4_add_h264_track(w, cfg->width, cfg->height,
                                          cfg->fps_num, cfg->fps_den,
                                          cfg->max_samples);
        if (track < 0) {
            libmp4_abort(w);
            return NULL;
        }
        w->legacy_h264_track = track;
    }
    return w;
}

int libmp4_add_h264_track(libmp4_writer_t* writer,
                          uint32_t width,
                          uint32_t height,
                          uint32_t fps_num,
                          uint32_t fps_den,
                          uint32_t max_samples)
{
    return libmp4_add_track(writer, LIBMP4_CODEC_H264, LIBMP4_TRACK_VIDEO,
                            width, height, fps_num, fps_den, 0, 0, max_samples);
}

int libmp4_add_h265_track(libmp4_writer_t* writer,
                          uint32_t width,
                          uint32_t height,
                          uint32_t fps_num,
                          uint32_t fps_den,
                          uint32_t max_samples)
{
    return libmp4_add_track(writer, LIBMP4_CODEC_H265, LIBMP4_TRACK_VIDEO,
                            width, height, fps_num, fps_den, 0, 0, max_samples);
}

int libmp4_add_g711a_track(libmp4_writer_t* writer,
                           uint32_t sample_rate,
                           uint32_t channels,
                           uint32_t max_samples)
{
    return libmp4_add_track(writer, LIBMP4_CODEC_G711A, LIBMP4_TRACK_AUDIO,
                            0, 0, 0, 0, sample_rate, channels, max_samples);
}

int libmp4_add_g711u_track(libmp4_writer_t* writer,
                           uint32_t sample_rate,
                           uint32_t channels,
                           uint32_t max_samples)
{
    return libmp4_add_track(writer, LIBMP4_CODEC_G711U, LIBMP4_TRACK_AUDIO,
                            0, 0, 0, 0, sample_rate, channels, max_samples);
}

int libmp4_write_h264_annexb_track(libmp4_writer_t* writer,
                                   int track,
                                   const void* data,
                                   size_t bytes,
                                   uint32_t duration_ms,
                                   uint32_t flags)
{
    return libmp4_write_video_annexb(writer, track, LIBMP4_CODEC_H264,
                                     data, bytes, duration_ms, flags);
}

int libmp4_write_h265_annexb_track(libmp4_writer_t* writer,
                                   int track,
                                   const void* data,
                                   size_t bytes,
                                   uint32_t duration_ms,
                                   uint32_t flags)
{
    return libmp4_write_video_annexb(writer, track, LIBMP4_CODEC_H265,
                                     data, bytes, duration_ms, flags);
}

int libmp4_write_g711_track(libmp4_writer_t* writer,
                            int track,
                            const void* data,
                            size_t bytes,
                            uint32_t duration_ms)
{
    libmp4_track_t* t;
    uint32_t duration;
    uint64_t offset;
    int ret;

    t = libmp4_get_track(writer, track);
    if (t == NULL || data == NULL || bytes == 0 || writer->closed ||
        t->kind != LIBMP4_TRACK_AUDIO ||
        (t->codec != LIBMP4_CODEC_G711A && t->codec != LIBMP4_CODEC_G711U) ||
        bytes > 0xffffffffu) {
        return LIBMP4_ERR_ARG;
    }
    ret = libmp4_check_file_limit(writer, (uint32_t)bytes);
    if (ret != LIBMP4_OK) {
        writer->error = ret;
        return ret;
    }
    duration = libmp4_g711_duration(t, bytes, duration_ms);
    offset = libmp4_tell(&writer->io);
    ret = libmp4_write_data(writer, data, bytes);
    if (ret != LIBMP4_OK) {
        writer->error = ret;
        return ret;
    }
    return libmp4_add_sample(writer, t, offset, (uint32_t)bytes, duration, 1);
}

int libmp4_write_h264_annexb(libmp4_writer_t* writer,
                             const void* data,
                             size_t bytes,
                             uint32_t duration_ms,
                             uint32_t flags)
{
    if (writer == NULL || writer->legacy_h264_track < 0) {
        return LIBMP4_ERR_ARG;
    }
    return libmp4_write_h264_annexb_track(writer, writer->legacy_h264_track,
                                          data, bytes, duration_ms, flags);
}

int libmp4_flush(libmp4_writer_t* writer)
{
    if (writer == NULL) {
        return LIBMP4_ERR_ARG;
    }
    return libmp4_io_flush(&writer->io);
}

static int libmp4_validate_for_close(libmp4_writer_t* w)
{
    uint32_t i;
    uint32_t samples = 0;

    for (i = 0; i < w->track_count; ++i) {
        libmp4_track_t* t = &w->tracks[i];
        if (!t->used || t->sample_count == 0) {
            continue;
        }
        samples += t->sample_count;
        if (t->codec == LIBMP4_CODEC_H264 && (t->sps_len == 0 || t->pps_len == 0)) {
            return LIBMP4_ERR_FORMAT;
        }
        if (t->codec == LIBMP4_CODEC_H265 &&
            (t->vps_len == 0 || t->sps_len == 0 || t->pps_len == 0)) {
            return LIBMP4_ERR_FORMAT;
        }
    }
    return samples > 0 ? LIBMP4_OK : LIBMP4_ERR_FORMAT;
}

int libmp4_close(libmp4_writer_t* writer)
{
    int ret;

    if (writer == NULL) {
        return LIBMP4_ERR_ARG;
    }
    if (writer->closed) {
        ret = writer->error;
        libmp4_abort(writer);
        return ret;
    }
    writer->closed = 1;
    ret = writer->error;
    if (ret == LIBMP4_OK) {
        ret = libmp4_validate_for_close(writer);
    }
    if (ret == LIBMP4_OK) {
        ret = libmp4_patch_mdat(writer);
    }
    if (ret == LIBMP4_OK) {
        ret = libmp4_write_moov(writer);
    }
    if (ret == LIBMP4_OK) {
        ret = libmp4_io_flush(&writer->io);
    }
    if (writer->io.fp != NULL && fclose(writer->io.fp) != 0 && ret == LIBMP4_OK) {
        ret = LIBMP4_ERR_IO;
    }
    writer->io.fp = NULL;
    libmp4_abort(writer);
    return ret;
}

void libmp4_abort(libmp4_writer_t* writer)
{
    uint32_t i;

    if (writer == NULL) {
        return;
    }
    if (writer->io.fp != NULL) {
        fclose(writer->io.fp);
        writer->io.fp = NULL;
    }
    if (writer->tracks != NULL) {
        for (i = 0; i < writer->max_tracks; ++i) {
            free(writer->tracks[i].samples);
        }
    }
    free(writer->tracks);
    free(writer->io.cache);
    free(writer);
}
