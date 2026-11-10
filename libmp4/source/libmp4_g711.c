#include "libmp4_internal.h"

#include <stddef.h>

static uint32_t libmp4_g711_default_duration(const libmp4_track_t* t)
{
    if (t == NULL || t->sample_rate == 0) {
        return 160u;
    }
    return t->sample_rate / 50u;
}

static uint32_t libmp4_g711_duration_from_ms(const libmp4_track_t* t, uint32_t duration_ms)
{
    uint64_t duration;

    if (duration_ms == 0) {
        return libmp4_g711_default_duration(t);
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

uint32_t libmp4_g711_duration(const libmp4_track_t* t,
                              size_t bytes,
                              uint32_t duration_ms)
{
    uint64_t bytes_per_tick;
    uint64_t duration;

    if (duration_ms != 0) {
        return libmp4_g711_duration_from_ms(t, duration_ms);
    }
    bytes_per_tick = (uint64_t)t->channels * ((uint64_t)t->bits_per_sample / 8u);
    if (bytes_per_tick == 0) {
        return libmp4_g711_default_duration(t);
    }
    duration = (uint64_t)bytes / bytes_per_tick;
    if (duration == 0) {
        duration = 1;
    }
    if (duration > 0xffffffffu) {
        duration = 0xffffffffu;
    }
    return (uint32_t)duration;
}

int libmp4_g711_write_audio_stsd(libmp4_writer_t* w, const libmp4_track_t* t)
{
    uint64_t stsd = libmp4_box_start(w, "stsd");
    uint64_t sample;

    libmp4_write_fullbox(w, 0, 0);
    libmp4_write_u32(w, 1);
    sample = libmp4_box_start(w, t->codec == LIBMP4_CODEC_G711U ? "ulaw" : "alaw");
    libmp4_write_zero(w, 6);
    libmp4_write_u16(w, 1);
    libmp4_write_u16(w, 0);
    libmp4_write_u16(w, 0);
    libmp4_write_u32(w, 0);
    libmp4_write_u16(w, (uint16_t)t->channels);
    libmp4_write_u16(w, (uint16_t)t->bits_per_sample);
    libmp4_write_u16(w, 0);
    libmp4_write_u16(w, 0);
    libmp4_write_u32(w, t->sample_rate << 16);
    libmp4_box_end(w, sample);
    return libmp4_box_end(w, stsd);
}

int libmp4_g711_parse_audio_entry(libmp4_reader_t* reader,
                                  libmp4_reader_track_t* track,
                                  const libmp4_box_t* entry)
{
    uint8_t h[28];

    if (entry->data + sizeof(h) > entry->end) {
        return LIBMP4_ERR_FORMAT;
    }
    if (libmp4_reader_read_at(reader->fp, entry->data, h, sizeof(h)) != LIBMP4_OK) {
        return LIBMP4_ERR_IO;
    }
    track->used = 1;
    track->kind = LIBMP4_TRACK_AUDIO;
    track->codec = libmp4_type_is(entry->type, "ulaw") ? LIBMP4_CODEC_G711U : LIBMP4_CODEC_G711A;
    track->channels = libmp4_rb16(h + 16);
    track->bits_per_sample = libmp4_rb16(h + 18);
    track->sample_rate = libmp4_rb32(h + 24) >> 16;
    if (track->timescale == 0) {
        track->timescale = track->sample_rate;
    }
    return LIBMP4_OK;
}
