#include "libmp4_internal.h"

#include <stdlib.h>
#include <string.h>

uint16_t libmp4_rb16(const uint8_t* p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

uint32_t libmp4_rb32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

uint64_t libmp4_rb64(const uint8_t* p)
{
    uint64_t hi = libmp4_rb32(p);
    uint64_t lo = libmp4_rb32(p + 4);
    return (hi << 32) | lo;
}

static int libmp4_reader_seek(FILE* fp, uint64_t offset)
{
    if (fp == NULL || offset > 0x7fffffffu) {
        return LIBMP4_ERR_LIMIT;
    }
    return fseek(fp, (long)offset, SEEK_SET) == 0 ? LIBMP4_OK : LIBMP4_ERR_IO;
}

int libmp4_reader_read_at(FILE* fp, uint64_t offset, void* data, size_t bytes)
{
    if (bytes == 0) {
        return LIBMP4_OK;
    }
    if (fp == NULL || data == NULL) {
        return LIBMP4_ERR_ARG;
    }
    if (libmp4_reader_seek(fp, offset) != LIBMP4_OK) {
        return LIBMP4_ERR_IO;
    }
    return fread(data, 1, bytes, fp) == bytes ? LIBMP4_OK : LIBMP4_ERR_IO;
}

static int libmp4_reader_file_size(FILE* fp, uint64_t* out)
{
    long size;

    if (fp == NULL || out == NULL) {
        return LIBMP4_ERR_ARG;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        return LIBMP4_ERR_IO;
    }
    size = ftell(fp);
    if (size < 0) {
        return LIBMP4_ERR_IO;
    }
    *out = (uint64_t)size;
    return LIBMP4_OK;
}

static int libmp4_reader_box_read(libmp4_reader_t* reader,
                                  uint64_t offset,
                                  uint64_t parent_end,
                                  libmp4_box_t* box)
{
    uint8_t h[16];
    uint32_t size32;

    if (reader == NULL || box == NULL || offset + 8u > parent_end) {
        return LIBMP4_ERR_FORMAT;
    }
    if (libmp4_reader_read_at(reader->fp, offset, h, 8) != LIBMP4_OK) {
        return LIBMP4_ERR_IO;
    }
    memset(box, 0, sizeof(*box));
    box->start = offset;
    size32 = libmp4_rb32(h);
    memcpy(box->type, h + 4, 4);
    box->type[4] = '\0';
    box->header_size = 8;
    if (size32 == 1) {
        if (offset + 16u > parent_end ||
            libmp4_reader_read_at(reader->fp, offset + 8u, h + 8, 8) != LIBMP4_OK) {
            return LIBMP4_ERR_FORMAT;
        }
        box->size = libmp4_rb64(h + 8);
        box->header_size = 16;
    } else if (size32 == 0) {
        box->size = parent_end - offset;
    } else {
        box->size = size32;
    }
    if (box->size < box->header_size || offset + box->size > parent_end) {
        return LIBMP4_ERR_FORMAT;
    }
    box->data = offset + box->header_size;
    box->end = offset + box->size;
    return LIBMP4_OK;
}

int libmp4_type_is(const char type[5], const char fourcc[4])
{
    return memcmp(type, fourcc, 4) == 0;
}

static int libmp4_reader_check_table_count(const libmp4_reader_t* reader,
                                           uint32_t count,
                                           size_t element_size)
{
    if (reader == NULL || reader->max_samples == 0 || element_size == 0) {
        return LIBMP4_ERR_LIMIT;
    }
    if (count > reader->max_samples ||
        (count != 0 && (size_t)count > (size_t)-1 / element_size)) {
        return LIBMP4_ERR_LIMIT;
    }
    return LIBMP4_OK;
}

static int libmp4_reader_add_track(libmp4_reader_t* reader, libmp4_reader_track_t** out)
{
    libmp4_reader_track_t* next;
    libmp4_reader_track_t* track;
    size_t next_count;

    if (reader == NULL || out == NULL || reader->max_tracks == 0) {
        return LIBMP4_ERR_ARG;
    }
    *out = NULL;
    if (reader->track_count >= reader->max_tracks) {
        return LIBMP4_ERR_LIMIT;
    }
    next_count = (size_t)reader->track_count + 1u;
    if (next_count > (size_t)-1 / sizeof(reader->tracks[0])) {
        return LIBMP4_ERR_LIMIT;
    }
    next = (libmp4_reader_track_t*)realloc(reader->tracks,
                                            next_count * sizeof(reader->tracks[0]));
    if (next == NULL) {
        return LIBMP4_ERR_NOMEM;
    }
    reader->tracks = next;
    track = &reader->tracks[reader->track_count++];
    memset(track, 0, sizeof(*track));
    track->used = 1;
    track->track_id = (int)reader->track_count;
    track->bits_per_sample = 8;
    track->nal_length_size = 4;
    *out = track;
    return LIBMP4_OK;
}

static int libmp4_reader_parse_tkhd(libmp4_reader_t* reader,
                                    libmp4_reader_track_t* track,
                                    const libmp4_box_t* box)
{
    uint8_t h[4];
    uint8_t version;
    uint64_t track_id_offset;

    if (reader == NULL || track == NULL || box == NULL || box->data + 1u > box->end) {
        return LIBMP4_ERR_FORMAT;
    }
    if (libmp4_reader_read_at(reader->fp, box->data, h, 1) != LIBMP4_OK) {
        return LIBMP4_ERR_IO;
    }
    version = h[0];
    track_id_offset = version == 1 ? box->data + 20u : box->data + 12u;
    if (track_id_offset + 4u <= box->end &&
        libmp4_reader_read_at(reader->fp, track_id_offset, h, 4) == LIBMP4_OK) {
        track->track_id = (int)libmp4_rb32(h);
    }
    return LIBMP4_OK;
}

static int libmp4_reader_parse_mdhd(libmp4_reader_t* reader,
                                    libmp4_reader_track_t* track,
                                    const libmp4_box_t* box)
{
    uint8_t h[8];
    uint8_t version;
    uint64_t timescale_offset;
    uint64_t duration_offset;

    if (reader == NULL || track == NULL || box == NULL || box->data + 1u > box->end) {
        return LIBMP4_ERR_FORMAT;
    }
    if (libmp4_reader_read_at(reader->fp, box->data, h, 1) != LIBMP4_OK) {
        return LIBMP4_ERR_IO;
    }
    version = h[0];
    timescale_offset = version == 1 ? box->data + 20u : box->data + 12u;
    duration_offset = version == 1 ? box->data + 24u : box->data + 16u;
    if (timescale_offset + 4u > box->end) {
        return LIBMP4_ERR_FORMAT;
    }
    if (libmp4_reader_read_at(reader->fp, timescale_offset, h, 4) != LIBMP4_OK) {
        return LIBMP4_ERR_IO;
    }
    track->timescale = libmp4_rb32(h);
    if (version == 1 && duration_offset + 8u <= box->end) {
        if (libmp4_reader_read_at(reader->fp, duration_offset, h, 8) != LIBMP4_OK) {
            return LIBMP4_ERR_IO;
        }
        track->duration = libmp4_rb64(h);
    } else if (duration_offset + 4u <= box->end) {
        if (libmp4_reader_read_at(reader->fp, duration_offset, h, 4) != LIBMP4_OK) {
            return LIBMP4_ERR_IO;
        }
        track->duration = libmp4_rb32(h);
    }
    return LIBMP4_OK;
}

static int libmp4_reader_parse_hdlr(libmp4_reader_t* reader,
                                    libmp4_reader_track_t* track,
                                    const libmp4_box_t* box)
{
    uint8_t h[4];

    if (reader == NULL || track == NULL || box == NULL || box->data + 12u > box->end) {
        return LIBMP4_ERR_FORMAT;
    }
    if (libmp4_reader_read_at(reader->fp, box->data + 8u, h, sizeof(h)) != LIBMP4_OK) {
        return LIBMP4_ERR_IO;
    }
    if (memcmp(h, "vide", 4) == 0) {
        track->kind = LIBMP4_TRACK_VIDEO;
    } else if (memcmp(h, "soun", 4) == 0) {
        track->kind = LIBMP4_TRACK_AUDIO;
    }
    return LIBMP4_OK;
}

static int libmp4_reader_parse_video_entry(libmp4_reader_t* reader,
                                           libmp4_reader_track_t* track,
                                           const libmp4_box_t* entry)
{
    uint8_t h[78];
    uint64_t pos;
    int ret;

    if (entry->data + sizeof(h) > entry->end) {
        return LIBMP4_ERR_FORMAT;
    }
    if (libmp4_reader_read_at(reader->fp, entry->data, h, sizeof(h)) != LIBMP4_OK) {
        return LIBMP4_ERR_IO;
    }
    track->used = 1;
    track->kind = LIBMP4_TRACK_VIDEO;
    track->codec = libmp4_type_is(entry->type, "hvc1") ? LIBMP4_CODEC_H265 : LIBMP4_CODEC_H264;
    track->width = libmp4_rb16(h + 24);
    track->height = libmp4_rb16(h + 26);
    pos = entry->data + sizeof(h);
    while (pos + 8u <= entry->end) {
        libmp4_box_t child;
        ret = libmp4_reader_box_read(reader, pos, entry->end, &child);
        if (ret != LIBMP4_OK) {
            return ret;
        }
        if (track->codec == LIBMP4_CODEC_H264 && libmp4_type_is(child.type, "avcC")) {
            ret = libmp4_h264_parse_avcc(reader, track, &child);
            if (ret != LIBMP4_OK) {
                return ret;
            }
        } else if (track->codec == LIBMP4_CODEC_H265 && libmp4_type_is(child.type, "hvcC")) {
            ret = libmp4_h265_parse_hvcc(reader, track, &child);
            if (ret != LIBMP4_OK) {
                return ret;
            }
        }
        pos = child.end;
    }
    return LIBMP4_OK;
}

static int libmp4_reader_parse_stsd(libmp4_reader_t* reader,
                                    libmp4_reader_track_t* track,
                                    const libmp4_box_t* box)
{
    uint8_t h[8];
    uint32_t entry_count;
    uint32_t i;
    uint64_t pos;
    int ret;

    if (box->data + sizeof(h) > box->end) {
        return LIBMP4_ERR_FORMAT;
    }
    if (libmp4_reader_read_at(reader->fp, box->data, h, sizeof(h)) != LIBMP4_OK) {
        return LIBMP4_ERR_IO;
    }
    entry_count = libmp4_rb32(h + 4);
    pos = box->data + sizeof(h);
    for (i = 0; i < entry_count && pos + 8u <= box->end; ++i) {
        libmp4_box_t entry;
        ret = libmp4_reader_box_read(reader, pos, box->end, &entry);
        if (ret != LIBMP4_OK) {
            return ret;
        }
        if (libmp4_type_is(entry.type, "avc1") || libmp4_type_is(entry.type, "hvc1")) {
            ret = libmp4_reader_parse_video_entry(reader, track, &entry);
        } else if (libmp4_type_is(entry.type, "alaw") || libmp4_type_is(entry.type, "ulaw")) {
            ret = libmp4_g711_parse_audio_entry(reader, track, &entry);
        } else {
            ret = LIBMP4_OK;
        }
        if (ret != LIBMP4_OK) {
            return ret;
        }
        pos = entry.end;
    }
    return LIBMP4_OK;
}

static int libmp4_reader_parse_stts(libmp4_reader_t* reader,
                                    libmp4_reader_track_t* track,
                                    const libmp4_box_t* box)
{
    uint8_t h[8];
    uint32_t count;
    uint32_t i;
    uint64_t pos;

    if (box->data + sizeof(h) > box->end) {
        return LIBMP4_ERR_FORMAT;
    }
    if (libmp4_reader_read_at(reader->fp, box->data, h, sizeof(h)) != LIBMP4_OK) {
        return LIBMP4_ERR_IO;
    }
    count = libmp4_rb32(h + 4);
    if (track->stts != NULL) {
        return LIBMP4_ERR_FORMAT;
    }
    if (libmp4_reader_check_table_count(reader, count, sizeof(track->stts[0])) != LIBMP4_OK) {
        return LIBMP4_ERR_LIMIT;
    }
    if (count > 0 && box->data + 8u + (uint64_t)count * 8u > box->end) {
        return LIBMP4_ERR_FORMAT;
    }
    track->stts = (libmp4_reader_stts_t*)calloc(count ? count : 1u, sizeof(track->stts[0]));
    if (track->stts == NULL) {
        return LIBMP4_ERR_NOMEM;
    }
    track->stts_count = count;
    pos = box->data + 8u;
    for (i = 0; i < count; ++i) {
        if (libmp4_reader_read_at(reader->fp, pos, h, 8) != LIBMP4_OK) {
            return LIBMP4_ERR_IO;
        }
        track->stts[i].count = libmp4_rb32(h);
        track->stts[i].duration = libmp4_rb32(h + 4);
        pos += 8u;
    }
    return LIBMP4_OK;
}

static int libmp4_reader_parse_stsc(libmp4_reader_t* reader,
                                    libmp4_reader_track_t* track,
                                    const libmp4_box_t* box)
{
    uint8_t h[12];
    uint32_t count;
    uint32_t i;
    uint64_t pos;

    if (box->data + 8u > box->end ||
        libmp4_reader_read_at(reader->fp, box->data, h, 8) != LIBMP4_OK) {
        return LIBMP4_ERR_FORMAT;
    }
    count = libmp4_rb32(h + 4);
    if (track->stsc != NULL) {
        return LIBMP4_ERR_FORMAT;
    }
    if (libmp4_reader_check_table_count(reader, count, sizeof(track->stsc[0])) != LIBMP4_OK) {
        return LIBMP4_ERR_LIMIT;
    }
    if (count > 0 && box->data + 8u + (uint64_t)count * 12u > box->end) {
        return LIBMP4_ERR_FORMAT;
    }
    track->stsc = (libmp4_reader_stsc_t*)calloc(count ? count : 1u, sizeof(track->stsc[0]));
    if (track->stsc == NULL) {
        return LIBMP4_ERR_NOMEM;
    }
    track->stsc_count = count;
    pos = box->data + 8u;
    for (i = 0; i < count; ++i) {
        if (libmp4_reader_read_at(reader->fp, pos, h, sizeof(h)) != LIBMP4_OK) {
            return LIBMP4_ERR_IO;
        }
        track->stsc[i].first_chunk = libmp4_rb32(h);
        track->stsc[i].samples_per_chunk = libmp4_rb32(h + 4);
        track->stsc[i].sample_desc_index = libmp4_rb32(h + 8);
        pos += 12u;
    }
    return LIBMP4_OK;
}

static int libmp4_reader_parse_stsz(libmp4_reader_t* reader,
                                    libmp4_reader_track_t* track,
                                    const libmp4_box_t* box)
{
    uint8_t h[12];
    uint32_t sample_size;
    uint32_t sample_count;
    uint32_t i;
    uint64_t pos;

    if (box->data + sizeof(h) > box->end ||
        libmp4_reader_read_at(reader->fp, box->data, h, sizeof(h)) != LIBMP4_OK) {
        return LIBMP4_ERR_FORMAT;
    }
    sample_size = libmp4_rb32(h + 4);
    sample_count = libmp4_rb32(h + 8);
    if (track->samples != NULL) {
        return LIBMP4_ERR_FORMAT;
    }
    if (libmp4_reader_check_table_count(reader, sample_count, sizeof(track->samples[0])) != LIBMP4_OK) {
        return LIBMP4_ERR_LIMIT;
    }
    if (sample_size == 0 && box->data + 12u + (uint64_t)sample_count * 4u > box->end) {
        return LIBMP4_ERR_FORMAT;
    }
    track->samples = (libmp4_reader_sample_t*)calloc(sample_count ? sample_count : 1u,
                                                     sizeof(track->samples[0]));
    if (track->samples == NULL) {
        return LIBMP4_ERR_NOMEM;
    }
    track->sample_count = sample_count;
    pos = box->data + 12u;
    for (i = 0; i < sample_count; ++i) {
        if (sample_size != 0) {
            track->samples[i].size = sample_size;
        } else {
            if (libmp4_reader_read_at(reader->fp, pos, h, 4) != LIBMP4_OK) {
                return LIBMP4_ERR_IO;
            }
            track->samples[i].size = libmp4_rb32(h);
            pos += 4u;
        }
    }
    return LIBMP4_OK;
}

static int libmp4_reader_parse_stco(libmp4_reader_t* reader,
                                    libmp4_reader_track_t* track,
                                    const libmp4_box_t* box,
                                    int co64)
{
    uint8_t h[8];
    uint32_t count;
    uint32_t i;
    uint64_t pos;
    uint32_t entry_size = co64 ? 8u : 4u;

    if (box->data + 8u > box->end ||
        libmp4_reader_read_at(reader->fp, box->data, h, 8) != LIBMP4_OK) {
        return LIBMP4_ERR_FORMAT;
    }
    count = libmp4_rb32(h + 4);
    if (track->chunk_offsets != NULL) {
        return LIBMP4_ERR_FORMAT;
    }
    if (libmp4_reader_check_table_count(reader, count, sizeof(track->chunk_offsets[0])) != LIBMP4_OK) {
        return LIBMP4_ERR_LIMIT;
    }
    if (count > 0 && box->data + 8u + (uint64_t)count * entry_size > box->end) {
        return LIBMP4_ERR_FORMAT;
    }
    track->chunk_offsets = (uint64_t*)calloc(count ? count : 1u, sizeof(track->chunk_offsets[0]));
    if (track->chunk_offsets == NULL) {
        return LIBMP4_ERR_NOMEM;
    }
    track->chunk_count = count;
    pos = box->data + 8u;
    for (i = 0; i < count; ++i) {
        if (libmp4_reader_read_at(reader->fp, pos, h, entry_size) != LIBMP4_OK) {
            return LIBMP4_ERR_IO;
        }
        track->chunk_offsets[i] = co64 ? libmp4_rb64(h) : libmp4_rb32(h);
        pos += entry_size;
    }
    return LIBMP4_OK;
}

static int libmp4_reader_parse_stss(libmp4_reader_t* reader,
                                    libmp4_reader_track_t* track,
                                    const libmp4_box_t* box)
{
    uint8_t h[8];
    uint32_t count;
    uint32_t i;
    uint64_t pos;

    if (box->data + 8u > box->end ||
        libmp4_reader_read_at(reader->fp, box->data, h, 8) != LIBMP4_OK) {
        return LIBMP4_ERR_FORMAT;
    }
    count = libmp4_rb32(h + 4);
    if (track->sync_samples != NULL) {
        return LIBMP4_ERR_FORMAT;
    }
    if (libmp4_reader_check_table_count(reader, count, sizeof(track->sync_samples[0])) != LIBMP4_OK) {
        return LIBMP4_ERR_LIMIT;
    }
    if (count > 0 && box->data + 8u + (uint64_t)count * 4u > box->end) {
        return LIBMP4_ERR_FORMAT;
    }
    track->sync_samples = (uint32_t*)calloc(count ? count : 1u, sizeof(track->sync_samples[0]));
    if (track->sync_samples == NULL) {
        return LIBMP4_ERR_NOMEM;
    }
    track->sync_count = count;
    track->has_stss = 1;
    pos = box->data + 8u;
    for (i = 0; i < count; ++i) {
        if (libmp4_reader_read_at(reader->fp, pos, h, 4) != LIBMP4_OK) {
            return LIBMP4_ERR_IO;
        }
        track->sync_samples[i] = libmp4_rb32(h);
        pos += 4u;
    }
    return LIBMP4_OK;
}

static int libmp4_reader_parse_boxes(libmp4_reader_t* reader,
                                     uint64_t start,
                                     uint64_t end,
                                     libmp4_reader_track_t* track)
{
    uint64_t pos = start;
    int ret;

    while (pos + 8u <= end) {
        libmp4_box_t box;
        ret = libmp4_reader_box_read(reader, pos, end, &box);
        if (ret != LIBMP4_OK) {
            return ret;
        }
        if (libmp4_type_is(box.type, "moov") ||
            libmp4_type_is(box.type, "mdia") ||
            libmp4_type_is(box.type, "minf") ||
            libmp4_type_is(box.type, "stbl")) {
            ret = libmp4_reader_parse_boxes(reader, box.data, box.end, track);
        } else if (libmp4_type_is(box.type, "trak")) {
            libmp4_reader_track_t* next_track;

            ret = libmp4_reader_add_track(reader, &next_track);
            if (ret == LIBMP4_OK) {
                ret = libmp4_reader_parse_boxes(reader, box.data, box.end, next_track);
            }
        } else if (track != NULL && libmp4_type_is(box.type, "tkhd")) {
            ret = libmp4_reader_parse_tkhd(reader, track, &box);
        } else if (track != NULL && libmp4_type_is(box.type, "mdhd")) {
            ret = libmp4_reader_parse_mdhd(reader, track, &box);
        } else if (track != NULL && libmp4_type_is(box.type, "hdlr")) {
            ret = libmp4_reader_parse_hdlr(reader, track, &box);
        } else if (track != NULL && libmp4_type_is(box.type, "stsd")) {
            ret = libmp4_reader_parse_stsd(reader, track, &box);
        } else if (track != NULL && libmp4_type_is(box.type, "stts")) {
            ret = libmp4_reader_parse_stts(reader, track, &box);
        } else if (track != NULL && libmp4_type_is(box.type, "stsc")) {
            ret = libmp4_reader_parse_stsc(reader, track, &box);
        } else if (track != NULL && libmp4_type_is(box.type, "stsz")) {
            ret = libmp4_reader_parse_stsz(reader, track, &box);
        } else if (track != NULL && libmp4_type_is(box.type, "stco")) {
            ret = libmp4_reader_parse_stco(reader, track, &box, 0);
        } else if (track != NULL && libmp4_type_is(box.type, "co64")) {
            ret = libmp4_reader_parse_stco(reader, track, &box, 1);
        } else if (track != NULL && libmp4_type_is(box.type, "stss")) {
            ret = libmp4_reader_parse_stss(reader, track, &box);
        } else {
            ret = LIBMP4_OK;
        }
        if (ret != LIBMP4_OK) {
            return ret;
        }
        pos = box.end;
    }
    return LIBMP4_OK;
}

static int libmp4_reader_expand_durations(libmp4_reader_track_t* track)
{
    uint32_t i;
    uint32_t j;
    uint32_t sample = 0;
    uint32_t last_duration = 1;

    if (track == NULL || track->samples == NULL) {
        return LIBMP4_OK;
    }
    track->duration = 0;
    for (i = 0; i < track->stts_count && sample < track->sample_count; ++i) {
        last_duration = track->stts[i].duration != 0 ? track->stts[i].duration : 1u;
        for (j = 0; j < track->stts[i].count && sample < track->sample_count; ++j) {
            track->samples[sample].duration = last_duration;
            track->duration += last_duration;
            ++sample;
        }
    }
    while (sample < track->sample_count) {
        track->samples[sample].duration = last_duration;
        track->duration += last_duration;
        ++sample;
    }
    return LIBMP4_OK;
}

static int libmp4_reader_expand_offsets(libmp4_reader_track_t* track)
{
    uint32_t entry;
    uint32_t sample = 0;

    if (track == NULL || track->sample_count == 0) {
        return LIBMP4_OK;
    }
    if (track->samples == NULL || track->stsc_count == 0 || track->chunk_count == 0) {
        return LIBMP4_ERR_FORMAT;
    }
    for (entry = 0; entry < track->stsc_count && sample < track->sample_count; ++entry) {
        uint32_t first_chunk = track->stsc[entry].first_chunk;
        uint32_t next_first = entry + 1u < track->stsc_count ?
            track->stsc[entry + 1u].first_chunk : track->chunk_count + 1u;
        uint32_t samples_per_chunk = track->stsc[entry].samples_per_chunk;
        uint32_t chunk;

        if (first_chunk == 0 || samples_per_chunk == 0 || first_chunk > track->chunk_count + 1u) {
            return LIBMP4_ERR_FORMAT;
        }
        if (next_first > track->chunk_count + 1u) {
            next_first = track->chunk_count + 1u;
        }
        for (chunk = first_chunk; chunk < next_first && sample < track->sample_count; ++chunk) {
            uint64_t offset = track->chunk_offsets[chunk - 1u];
            uint32_t i;
            for (i = 0; i < samples_per_chunk && sample < track->sample_count; ++i) {
                track->samples[sample].offset = offset;
                offset += track->samples[sample].size;
                ++sample;
            }
        }
    }
    return sample == track->sample_count ? LIBMP4_OK : LIBMP4_ERR_FORMAT;
}

static int libmp4_reader_expand_sync(libmp4_reader_track_t* track)
{
    uint32_t i;

    if (track == NULL || track->samples == NULL) {
        return LIBMP4_OK;
    }
    if (track->kind == LIBMP4_TRACK_AUDIO || !track->has_stss) {
        for (i = 0; i < track->sample_count; ++i) {
            track->samples[i].keyframe = 1;
        }
        return LIBMP4_OK;
    }
    for (i = 0; i < track->sync_count; ++i) {
        uint32_t index = track->sync_samples[i];
        if (index > 0 && index <= track->sample_count) {
            track->samples[index - 1u].keyframe = 1;
        }
    }
    return LIBMP4_OK;
}

static void libmp4_reader_drop_tables(libmp4_reader_track_t* track)
{
    if (track == NULL) {
        return;
    }
    free(track->stts);
    free(track->stsc);
    free(track->chunk_offsets);
    free(track->sync_samples);
    track->stts = NULL;
    track->stsc = NULL;
    track->chunk_offsets = NULL;
    track->sync_samples = NULL;
    track->stts_count = 0;
    track->stsc_count = 0;
    track->chunk_count = 0;
    track->sync_count = 0;
}

static int libmp4_reader_finalize(libmp4_reader_t* reader)
{
    uint32_t i;
    int ret;

    if (reader == NULL) {
        return LIBMP4_ERR_ARG;
    }
    for (i = 0; i < reader->track_count; ++i) {
        libmp4_reader_track_t* track = &reader->tracks[i];
        if (track->codec == 0 || track->sample_count == 0) {
            libmp4_reader_drop_tables(track);
            continue;
        }
        if (track->timescale == 0) {
            track->timescale = track->sample_rate != 0 ? track->sample_rate : LIBMP4_DEFAULT_TIMESCALE;
        }
        ret = libmp4_reader_expand_durations(track);
        if (ret == LIBMP4_OK) {
            ret = libmp4_reader_expand_offsets(track);
        }
        if (ret == LIBMP4_OK) {
            ret = libmp4_reader_expand_sync(track);
        }
        libmp4_reader_drop_tables(track);
        if (ret != LIBMP4_OK) {
            return ret;
        }
    }
    return LIBMP4_OK;
}

static libmp4_reader_track_t* libmp4_reader_get_track(libmp4_reader_t* reader, int track_index)
{
    uint32_t i;
    int visible = 0;

    if (reader == NULL || track_index < 0) {
        return NULL;
    }
    for (i = 0; i < reader->track_count; ++i) {
        libmp4_reader_track_t* track = &reader->tracks[i];
        if (track->codec != 0 && track->sample_count > 0) {
            if (visible == track_index) {
                return track;
            }
            ++visible;
        }
    }
    return NULL;
}

static uint64_t libmp4_reader_track_duration_ms(const libmp4_reader_track_t* track)
{
    if (track == NULL || track->timescale == 0) {
        return 0;
    }
    return (track->duration * 1000u + (track->timescale / 2u)) / track->timescale;
}

static uint32_t libmp4_reader_duration_ms(const libmp4_reader_track_t* track, uint32_t duration)
{
    if (track == NULL || track->timescale == 0) {
        return 0;
    }
    return (uint32_t)(((uint64_t)duration * 1000u + (track->timescale / 2u)) / track->timescale);
}

static uint64_t libmp4_reader_sample_pts_ms(const libmp4_reader_track_t* track, uint32_t sample_index)
{
    uint64_t pts = 0;
    uint32_t i;

    if (track == NULL || track->timescale == 0) {
        return 0;
    }
    for (i = 0; i < sample_index && i < track->sample_count; ++i) {
        pts += track->samples[i].duration;
    }
    return (pts * 1000u + (track->timescale / 2u)) / track->timescale;
}

static void libmp4_reader_fill_sample_info(const libmp4_reader_track_t* track,
                                           int public_track,
                                           uint32_t sample_index,
                                           libmp4_sample_info_t* info)
{
    const libmp4_reader_sample_t* sample;

    if (track == NULL || info == NULL || sample_index >= track->sample_count) {
        return;
    }
    sample = &track->samples[sample_index];
    memset(info, 0, sizeof(*info));
    info->track = public_track;
    info->sample_index = sample_index;
    info->size = sample->size;
    info->duration_ms = libmp4_reader_duration_ms(track, sample->duration);
    info->pts_ms = libmp4_reader_sample_pts_ms(track, sample_index);
    info->flags = sample->keyframe ? LIBMP4_FLAG_KEYFRAME : 0;
}

libmp4_reader_t* libmp4_reader_open_with_config(const char* path,
                                                const libmp4_reader_config_t* cfg)
{
    libmp4_reader_t* reader;
    int ret;

    if (path == NULL || path[0] == '\0') {
        return NULL;
    }
    reader = (libmp4_reader_t*)calloc(1, sizeof(*reader));
    if (reader == NULL) {
        return NULL;
    }
    reader->max_tracks = cfg != NULL && cfg->max_tracks != 0 ?
                         cfg->max_tracks : LIBMP4_READER_DEFAULT_MAX_TRACKS;
    reader->max_samples = cfg != NULL && cfg->max_samples != 0 ?
                          cfg->max_samples : LIBMP4_READER_DEFAULT_MAX_SAMPLES;
    reader->fp = fopen(path, "rb");
    if (reader->fp == NULL) {
        free(reader);
        return NULL;
    }
    ret = libmp4_reader_file_size(reader->fp, &reader->file_size);
    if (ret == LIBMP4_OK) {
        ret = libmp4_reader_parse_boxes(reader, 0, reader->file_size, NULL);
    }
    if (ret == LIBMP4_OK) {
        ret = libmp4_reader_finalize(reader);
    }
    if (ret != LIBMP4_OK) {
        reader->error = ret;
        libmp4_reader_close(reader);
        return NULL;
    }
    return reader;
}

libmp4_reader_t* libmp4_reader_open(const char* path)
{
    return libmp4_reader_open_with_config(path, NULL);
}

int libmp4_reader_track_count(libmp4_reader_t* reader)
{
    uint32_t i;
    int count = 0;

    if (reader == NULL) {
        return LIBMP4_ERR_ARG;
    }
    for (i = 0; i < reader->track_count; ++i) {
        if (reader->tracks[i].codec != 0 && reader->tracks[i].sample_count > 0) {
            ++count;
        }
    }
    return count;
}

int libmp4_reader_get_track_info(libmp4_reader_t* reader,
                                 int track_index,
                                 libmp4_track_info_t* info)
{
    libmp4_reader_track_t* track = libmp4_reader_get_track(reader, track_index);

    if (track == NULL || info == NULL) {
        return LIBMP4_ERR_ARG;
    }
    memset(info, 0, sizeof(*info));
    info->track_id = track->track_id;
    info->codec = track->codec;
    info->width = track->width;
    info->height = track->height;
    info->timescale = track->timescale;
    info->sample_rate = track->sample_rate;
    info->channels = track->channels;
    info->bits_per_sample = track->bits_per_sample;
    info->sample_count = track->sample_count;
    info->duration_ms = libmp4_reader_track_duration_ms(track);
    return LIBMP4_OK;
}

int libmp4_reader_get_sample_info(libmp4_reader_t* reader,
                                  int track_index,
                                  uint32_t sample_index,
                                  libmp4_sample_info_t* info)
{
    libmp4_reader_track_t* track = libmp4_reader_get_track(reader, track_index);

    if (track == NULL || info == NULL || sample_index >= track->sample_count) {
        return LIBMP4_ERR_ARG;
    }
    libmp4_reader_fill_sample_info(track, track_index, sample_index, info);
    return LIBMP4_OK;
}

static size_t libmp4_reader_config_size(const libmp4_reader_track_t* track)
{
    size_t bytes = 0;

    if (track == NULL) {
        return 0;
    }
    if (track->codec == LIBMP4_CODEC_H265 && track->vps_len > 0) {
        bytes += 4u + track->vps_len;
    }
    if ((track->codec == LIBMP4_CODEC_H264 || track->codec == LIBMP4_CODEC_H265) &&
        track->sps_len > 0) {
        bytes += 4u + track->sps_len;
    }
    if ((track->codec == LIBMP4_CODEC_H264 || track->codec == LIBMP4_CODEC_H265) &&
        track->pps_len > 0) {
        bytes += 4u + track->pps_len;
    }
    return bytes;
}

static int libmp4_reader_write_start_code(uint8_t** out, size_t* remaining)
{
    static const uint8_t sc[4] = { 0, 0, 0, 1 };

    if (out == NULL || remaining == NULL || *out == NULL || *remaining < sizeof(sc)) {
        return LIBMP4_ERR_LIMIT;
    }
    memcpy(*out, sc, sizeof(sc));
    *out += sizeof(sc);
    *remaining -= sizeof(sc);
    return LIBMP4_OK;
}

static int libmp4_reader_copy_param_out(const uint8_t* param,
                                        uint16_t param_len,
                                        uint8_t** out,
                                        size_t* remaining)
{
    int ret;

    if (param == NULL || param_len == 0) {
        return LIBMP4_OK;
    }
    ret = libmp4_reader_write_start_code(out, remaining);
    if (ret != LIBMP4_OK) {
        return ret;
    }
    if (*remaining < param_len) {
        return LIBMP4_ERR_LIMIT;
    }
    memcpy(*out, param, param_len);
    *out += param_len;
    *remaining -= param_len;
    return LIBMP4_OK;
}

static int libmp4_reader_copy_config(const libmp4_reader_track_t* track,
                                     uint8_t** out,
                                     size_t* remaining)
{
    int ret;

    if (track == NULL) {
        return LIBMP4_ERR_ARG;
    }
    if (track->codec == LIBMP4_CODEC_H265) {
        ret = libmp4_reader_copy_param_out(track->vps, track->vps_len, out, remaining);
        if (ret != LIBMP4_OK) {
            return ret;
        }
    }
    ret = libmp4_reader_copy_param_out(track->sps, track->sps_len, out, remaining);
    if (ret != LIBMP4_OK) {
        return ret;
    }
    return libmp4_reader_copy_param_out(track->pps, track->pps_len, out, remaining);
}

static int libmp4_reader_read_nal_length(FILE* fp,
                                         uint64_t offset,
                                         uint8_t length_size,
                                         uint32_t* nal_len)
{
    uint8_t h[4] = { 0, 0, 0, 0 };
    uint32_t len = 0;
    uint8_t i;

    if (nal_len == NULL || length_size == 0 || length_size > 4) {
        return LIBMP4_ERR_FORMAT;
    }
    if (libmp4_reader_read_at(fp, offset, h, length_size) != LIBMP4_OK) {
        return LIBMP4_ERR_IO;
    }
    for (i = 0; i < length_size; ++i) {
        len = (len << 8) | h[i];
    }
    *nal_len = len;
    return LIBMP4_OK;
}

static int libmp4_reader_video_annexb_size(libmp4_reader_t* reader,
                                           const libmp4_reader_track_t* track,
                                           const libmp4_reader_sample_t* sample,
                                           uint32_t flags,
                                           size_t* needed)
{
    uint64_t pos;
    uint32_t remaining;
    size_t total = 0;
    uint8_t length_size;
    int ret;

    if (reader == NULL || track == NULL || sample == NULL || needed == NULL) {
        return LIBMP4_ERR_ARG;
    }
    if (flags & LIBMP4_READ_FLAG_PREPEND_CONFIG) {
        total += libmp4_reader_config_size(track);
    }
    length_size = track->nal_length_size != 0 ? track->nal_length_size : 4u;
    pos = sample->offset;
    remaining = sample->size;
    while (remaining > 0) {
        uint32_t nal_len;
        if (remaining < length_size) {
            return LIBMP4_ERR_FORMAT;
        }
        ret = libmp4_reader_read_nal_length(reader->fp, pos, length_size, &nal_len);
        if (ret != LIBMP4_OK) {
            return ret;
        }
        pos += length_size;
        remaining -= length_size;
        if (nal_len > remaining) {
            return LIBMP4_ERR_FORMAT;
        }
        total += 4u + nal_len;
        pos += nal_len;
        remaining -= nal_len;
    }
    *needed = total;
    return LIBMP4_OK;
}

static int libmp4_reader_copy_video_sample(libmp4_reader_t* reader,
                                           const libmp4_reader_track_t* track,
                                           const libmp4_reader_sample_t* sample,
                                           uint32_t flags,
                                           uint8_t* out,
                                           size_t out_cap)
{
    uint64_t pos;
    uint32_t remaining_sample;
    size_t remaining_out = out_cap;
    uint8_t length_size;
    int ret;

    if (reader == NULL || track == NULL || sample == NULL || out == NULL) {
        return LIBMP4_ERR_ARG;
    }
    if (flags & LIBMP4_READ_FLAG_PREPEND_CONFIG) {
        ret = libmp4_reader_copy_config(track, &out, &remaining_out);
        if (ret != LIBMP4_OK) {
            return ret;
        }
    }
    length_size = track->nal_length_size != 0 ? track->nal_length_size : 4u;
    pos = sample->offset;
    remaining_sample = sample->size;
    while (remaining_sample > 0) {
        uint32_t nal_len;
        if (remaining_sample < length_size) {
            return LIBMP4_ERR_FORMAT;
        }
        ret = libmp4_reader_read_nal_length(reader->fp, pos, length_size, &nal_len);
        if (ret != LIBMP4_OK) {
            return ret;
        }
        pos += length_size;
        remaining_sample -= length_size;
        if (nal_len > remaining_sample || remaining_out < 4u + nal_len) {
            return LIBMP4_ERR_FORMAT;
        }
        ret = libmp4_reader_write_start_code(&out, &remaining_out);
        if (ret != LIBMP4_OK) {
            return ret;
        }
        if (libmp4_reader_read_at(reader->fp, pos, out, nal_len) != LIBMP4_OK) {
            return LIBMP4_ERR_IO;
        }
        out += nal_len;
        remaining_out -= nal_len;
        pos += nal_len;
        remaining_sample -= nal_len;
    }
    return LIBMP4_OK;
}

int libmp4_reader_read_sample(libmp4_reader_t* reader,
                              int track_index,
                              uint32_t sample_index,
                              void* out,
                              size_t out_cap,
                              size_t* out_bytes,
                              libmp4_sample_info_t* info,
                              uint32_t flags)
{
    libmp4_reader_track_t* track = libmp4_reader_get_track(reader, track_index);
    libmp4_reader_sample_t* sample;
    size_t needed;
    int ret;

    if (track == NULL || sample_index >= track->sample_count || out_bytes == NULL) {
        return LIBMP4_ERR_ARG;
    }
    sample = &track->samples[sample_index];
    if (track->kind == LIBMP4_TRACK_VIDEO) {
        ret = libmp4_reader_video_annexb_size(reader, track, sample, flags, &needed);
        if (ret != LIBMP4_OK) {
            return ret;
        }
    } else {
        needed = sample->size;
    }
    *out_bytes = needed;
    if (info != NULL) {
        libmp4_reader_fill_sample_info(track, track_index, sample_index, info);
    }
    if (out == NULL || out_cap == 0) {
        return LIBMP4_OK;
    }
    if (out_cap < needed) {
        return LIBMP4_ERR_LIMIT;
    }
    if (track->kind == LIBMP4_TRACK_VIDEO) {
        return libmp4_reader_copy_video_sample(reader, track, sample, flags, (uint8_t*)out, out_cap);
    }
    return libmp4_reader_read_at(reader->fp, sample->offset, out, needed);
}

void libmp4_reader_close(libmp4_reader_t* reader)
{
    uint32_t i;

    if (reader == NULL) {
        return;
    }
    if (reader->fp != NULL) {
        fclose(reader->fp);
    }
    for (i = 0; i < reader->track_count; ++i) {
        free(reader->tracks[i].samples);
        libmp4_reader_drop_tables(&reader->tracks[i]);
    }
    free(reader->tracks);
    free(reader);
}
