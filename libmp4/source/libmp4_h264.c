#include "libmp4_internal.h"
#include "libmp4_bitreader.h"

#include <stdlib.h>
#include <string.h>

typedef struct libmp4_h264_sps_info {
    uint8_t chroma_format_idc;
    uint8_t bit_depth_luma_minus8;
    uint8_t bit_depth_chroma_minus8;
} libmp4_h264_sps_info_t;

static int libmp4_h264_profile_has_extension(uint8_t profile)
{
    switch (profile) {
    case 44:
    case 83:
    case 86:
    case 100:
    case 110:
    case 118:
    case 122:
    case 128:
    case 134:
    case 135:
    case 138:
    case 139:
    case 144:
    case 244:
        return 1;
    default:
        return 0;
    }
}

static int libmp4_h264_parse_sps_info(const uint8_t* sps,
                                      size_t sps_len,
                                      libmp4_h264_sps_info_t* info)
{
    libmp4_bitreader_t bits;
    uint8_t profile;
    uint32_t value;
    int ret;

    if (sps == NULL || info == NULL || sps_len < 4u || (sps[0] & 0x1fu) != 7u) {
        return LIBMP4_ERR_FORMAT;
    }
    profile = sps[1];
    info->chroma_format_idc = 1;
    info->bit_depth_luma_minus8 = 0;
    info->bit_depth_chroma_minus8 = 0;
    if (!libmp4_h264_profile_has_extension(profile)) {
        return LIBMP4_OK;
    }

    libmp4_bitreader_init(&bits, sps + 4u, sps_len - 4u);
    ret = libmp4_bitreader_read_ue(&bits, &value); /* seq_parameter_set_id */
    if (ret != LIBMP4_OK) {
        return ret;
    }
    ret = libmp4_bitreader_read_ue(&bits, &value); /* chroma_format_idc */
    if (ret != LIBMP4_OK || value > 3u) {
        return LIBMP4_ERR_FORMAT;
    }
    info->chroma_format_idc = (uint8_t)value;
    if (value == 3u) {
        ret = libmp4_bitreader_skip(&bits, 1); /* separate_colour_plane_flag */
        if (ret != LIBMP4_OK) {
            return ret;
        }
    }
    ret = libmp4_bitreader_read_ue(&bits, &value); /* bit_depth_luma_minus8 */
    if (ret != LIBMP4_OK || value > 7u) {
        return LIBMP4_ERR_FORMAT;
    }
    info->bit_depth_luma_minus8 = (uint8_t)value;
    ret = libmp4_bitreader_read_ue(&bits, &value); /* bit_depth_chroma_minus8 */
    if (ret != LIBMP4_OK || value > 7u) {
        return LIBMP4_ERR_FORMAT;
    }
    info->bit_depth_chroma_minus8 = (uint8_t)value;
    return LIBMP4_OK;
}

int libmp4_h264_scan_nal(void* user, const uint8_t* nal, size_t len)
{
    libmp4_video_scan_t* scan = (libmp4_video_scan_t*)user;
    uint8_t type;

    if (scan == NULL || scan->track == NULL || len == 0) {
        return LIBMP4_OK;
    }
    type = (uint8_t)(nal[0] & 0x1fu);
    if (type == 7) {
        if (len > LIBMP4_SPS_MAX) {
            return LIBMP4_ERR_LIMIT;
        }
        memcpy(scan->track->sps, nal, len);
        scan->track->sps_len = (uint16_t)len;
        return LIBMP4_OK;
    }
    if (type == 8) {
        if (len > LIBMP4_PPS_MAX) {
            return LIBMP4_ERR_LIMIT;
        }
        memcpy(scan->track->pps, nal, len);
        scan->track->pps_len = (uint16_t)len;
        return LIBMP4_OK;
    }
    if (type == 9) {
        return LIBMP4_OK;
    }
    if (type == 1 || type == 5) {
        scan->vcl = 1;
    }
    if (type == 5) {
        scan->keyframe = 1;
    }
    if (len > 0xffffffffu - 4u - scan->sample_size) {
        return LIBMP4_ERR_LIMIT;
    }
    scan->sample_size += (uint32_t)len + 4u;
    return LIBMP4_OK;
}

int libmp4_h264_should_skip(const uint8_t* nal, size_t len)
{
    uint8_t type;

    if (len < 1) {
        return 1;
    }
    type = (uint8_t)(nal[0] & 0x1fu);
    return type == 7 || type == 8 || type == 9;
}

int libmp4_h264_write_avcc(libmp4_writer_t* w, const libmp4_track_t* t)
{
    libmp4_h264_sps_info_t info;
    uint8_t profile;
    uint8_t compatibility;
    uint8_t level;
    uint64_t box;
    int ret;

    if (w == NULL || t == NULL || t->sps_len < 4u || t->pps_len == 0) {
        if (w != NULL) {
            w->error = LIBMP4_ERR_FORMAT;
        }
        return LIBMP4_ERR_FORMAT;
    }
    profile = t->sps[1];
    compatibility = t->sps[2];
    level = t->sps[3];
    ret = libmp4_h264_parse_sps_info(t->sps, t->sps_len, &info);
    if (ret != LIBMP4_OK) {
        w->error = ret;
        return ret;
    }
    box = libmp4_box_start(w, "avcC");

    libmp4_write_u8(w, 1);
    libmp4_write_u8(w, profile);
    libmp4_write_u8(w, compatibility);
    libmp4_write_u8(w, level);
    libmp4_write_u8(w, 0xffu);
    libmp4_write_u8(w, 0xe1u);
    libmp4_write_u16(w, t->sps_len);
    libmp4_write_data(w, t->sps, t->sps_len);
    libmp4_write_u8(w, 1);
    libmp4_write_u16(w, t->pps_len);
    libmp4_write_data(w, t->pps, t->pps_len);
    if (libmp4_h264_profile_has_extension(profile)) {
        libmp4_write_u8(w, (uint8_t)(0xfcu | info.chroma_format_idc));
        libmp4_write_u8(w, (uint8_t)(0xf8u | info.bit_depth_luma_minus8));
        libmp4_write_u8(w, (uint8_t)(0xf8u | info.bit_depth_chroma_minus8));
        libmp4_write_u8(w, 0); /* numOfSequenceParameterSetExt */
    }
    return libmp4_box_end(w, box);
}

static int libmp4_h264_copy_param(uint8_t* dst,
                                  uint16_t* dst_len,
                                  uint32_t dst_cap,
                                  const uint8_t* src,
                                  uint32_t len)
{
    if (dst == NULL || dst_len == NULL || src == NULL || len == 0 || len > dst_cap) {
        return LIBMP4_ERR_LIMIT;
    }
    memcpy(dst, src, len);
    *dst_len = (uint16_t)len;
    return LIBMP4_OK;
}

int libmp4_h264_parse_avcc(libmp4_reader_t* reader,
                           libmp4_reader_track_t* track,
                           const libmp4_box_t* box)
{
    uint8_t* data;
    uint64_t payload_size;
    size_t pos;
    uint8_t count;
    uint8_t i;
    int ret = LIBMP4_OK;

    payload_size = box->end - box->data;
    if (payload_size < 7u || payload_size > 65536u) {
        return LIBMP4_ERR_FORMAT;
    }
    data = (uint8_t*)malloc((size_t)payload_size);
    if (data == NULL) {
        return LIBMP4_ERR_NOMEM;
    }
    if (libmp4_reader_read_at(reader->fp, box->data, data, (size_t)payload_size) != LIBMP4_OK) {
        free(data);
        return LIBMP4_ERR_IO;
    }
    track->nal_length_size = (uint8_t)((data[4] & 0x03u) + 1u);
    pos = 5;
    count = (uint8_t)(data[pos++] & 0x1fu);
    for (i = 0; i < count && pos + 2u <= payload_size; ++i) {
        uint32_t len = libmp4_rb16(data + pos);
        pos += 2u;
        if (pos + len > payload_size) {
            ret = LIBMP4_ERR_FORMAT;
            break;
        }
        if (i == 0) {
            ret = libmp4_h264_copy_param(track->sps,
                                         &track->sps_len,
                                         LIBMP4_SPS_MAX,
                                         data + pos,
                                         len);
            if (ret != LIBMP4_OK) {
                break;
            }
        }
        pos += len;
    }
    if (ret == LIBMP4_OK && pos < payload_size) {
        count = data[pos++];
        for (i = 0; i < count && pos + 2u <= payload_size; ++i) {
            uint32_t len = libmp4_rb16(data + pos);
            pos += 2u;
            if (pos + len > payload_size) {
                ret = LIBMP4_ERR_FORMAT;
                break;
            }
            if (i == 0) {
                ret = libmp4_h264_copy_param(track->pps,
                                             &track->pps_len,
                                             LIBMP4_PPS_MAX,
                                             data + pos,
                                             len);
                if (ret != LIBMP4_OK) {
                    break;
                }
            }
            pos += len;
        }
    }
    free(data);
    return ret;
}
