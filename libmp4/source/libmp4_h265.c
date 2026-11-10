#include "libmp4_internal.h"
#include "libmp4_bitreader.h"

#include <stdlib.h>
#include <string.h>

typedef struct libmp4_h265_sps_info {
    uint8_t general_profile_space;
    uint8_t general_tier_flag;
    uint8_t general_profile_idc;
    uint32_t general_profile_compatibility_flags;
    uint64_t general_constraint_indicator_flags;
    uint8_t general_level_idc;
    uint8_t chroma_format;
    uint8_t bit_depth_luma_minus8;
    uint8_t bit_depth_chroma_minus8;
    uint8_t num_temporal_layers;
    uint8_t temporal_id_nested;
} libmp4_h265_sps_info_t;

static int libmp4_h265_read_profile_tier_level(libmp4_bitreader_t* bits,
                                               uint8_t max_sub_layers_minus1,
                                               libmp4_h265_sps_info_t* info)
{
    uint8_t sub_profile_present[7] = { 0 };
    uint8_t sub_level_present[7] = { 0 };
    uint32_t value;
    uint32_t compatibility;
    uint32_t constraint_high;
    uint32_t constraint_low;
    uint8_t i;
    int ret;

    if (bits == NULL || info == NULL || max_sub_layers_minus1 > 6u) {
        return LIBMP4_ERR_FORMAT;
    }
    ret = libmp4_bitreader_read(bits, 2, &value);
    if (ret != LIBMP4_OK) {
        return ret;
    }
    info->general_profile_space = (uint8_t)value;
    ret = libmp4_bitreader_read(bits, 1, &value);
    if (ret != LIBMP4_OK) {
        return ret;
    }
    info->general_tier_flag = (uint8_t)value;
    ret = libmp4_bitreader_read(bits, 5, &value);
    if (ret != LIBMP4_OK) {
        return ret;
    }
    info->general_profile_idc = (uint8_t)value;
    ret = libmp4_bitreader_read(bits, 32, &compatibility);
    if (ret != LIBMP4_OK) {
        return ret;
    }
    info->general_profile_compatibility_flags = compatibility;
    ret = libmp4_bitreader_read(bits, 32, &constraint_high);
    if (ret != LIBMP4_OK) {
        return ret;
    }
    ret = libmp4_bitreader_read(bits, 16, &constraint_low);
    if (ret != LIBMP4_OK) {
        return ret;
    }
    info->general_constraint_indicator_flags =
        ((uint64_t)constraint_high << 16) | (uint64_t)constraint_low;
    ret = libmp4_bitreader_read(bits, 8, &value);
    if (ret != LIBMP4_OK) {
        return ret;
    }
    info->general_level_idc = (uint8_t)value;

    for (i = 0; i < max_sub_layers_minus1; ++i) {
        ret = libmp4_bitreader_read(bits, 1, &value);
        if (ret != LIBMP4_OK) {
            return ret;
        }
        sub_profile_present[i] = (uint8_t)value;
        ret = libmp4_bitreader_read(bits, 1, &value);
        if (ret != LIBMP4_OK) {
            return ret;
        }
        sub_level_present[i] = (uint8_t)value;
    }
    if (max_sub_layers_minus1 > 0u) {
        for (i = max_sub_layers_minus1; i < 8u; ++i) {
            ret = libmp4_bitreader_skip(bits, 2);
            if (ret != LIBMP4_OK) {
                return ret;
            }
        }
        for (i = 0; i < max_sub_layers_minus1; ++i) {
            if (sub_profile_present[i]) {
                ret = libmp4_bitreader_skip(bits, 88);
                if (ret != LIBMP4_OK) {
                    return ret;
                }
            }
            if (sub_level_present[i]) {
                ret = libmp4_bitreader_skip(bits, 8);
                if (ret != LIBMP4_OK) {
                    return ret;
                }
            }
        }
    }
    return LIBMP4_OK;
}

static int libmp4_h265_parse_sps_info(const uint8_t* sps,
                                      size_t sps_len,
                                      libmp4_h265_sps_info_t* info)
{
    libmp4_bitreader_t bits;
    uint32_t value;
    uint8_t max_sub_layers_minus1;
    uint8_t i;
    int ret;

    if (sps == NULL || info == NULL || sps_len < 3u ||
        ((sps[0] >> 1) & 0x3fu) != 33u) {
        return LIBMP4_ERR_FORMAT;
    }
    memset(info, 0, sizeof(*info));
    info->chroma_format = 1;
    info->num_temporal_layers = 1;
    info->temporal_id_nested = 1;
    libmp4_bitreader_init(&bits, sps + 2u, sps_len - 2u);

    ret = libmp4_bitreader_read(&bits, 4, &value); /* sps_video_parameter_set_id */
    if (ret != LIBMP4_OK) {
        return ret;
    }
    ret = libmp4_bitreader_read(&bits, 3, &value); /* sps_max_sub_layers_minus1 */
    if (ret != LIBMP4_OK || value > 6u) {
        return LIBMP4_ERR_FORMAT;
    }
    max_sub_layers_minus1 = (uint8_t)value;
    ret = libmp4_bitreader_read(&bits, 1, &value); /* sps_temporal_id_nesting_flag */
    if (ret != LIBMP4_OK) {
        return ret;
    }
    info->temporal_id_nested = (uint8_t)value;
    info->num_temporal_layers = (uint8_t)(max_sub_layers_minus1 + 1u);
    ret = libmp4_h265_read_profile_tier_level(&bits, max_sub_layers_minus1, info);
    if (ret != LIBMP4_OK) {
        return ret;
    }

    ret = libmp4_bitreader_read_ue(&bits, &value); /* sps_seq_parameter_set_id */
    if (ret != LIBMP4_OK) {
        return ret;
    }
    ret = libmp4_bitreader_read_ue(&bits, &value); /* chroma_format_idc */
    if (ret != LIBMP4_OK || value > 3u) {
        return LIBMP4_ERR_FORMAT;
    }
    info->chroma_format = (uint8_t)value;
    if (value == 3u) {
        ret = libmp4_bitreader_skip(&bits, 1); /* separate_colour_plane_flag */
        if (ret != LIBMP4_OK) {
            return ret;
        }
    }
    ret = libmp4_bitreader_read_ue(&bits, &value); /* pic_width_in_luma_samples */
    if (ret != LIBMP4_OK) {
        return ret;
    }
    ret = libmp4_bitreader_read_ue(&bits, &value); /* pic_height_in_luma_samples */
    if (ret != LIBMP4_OK) {
        return ret;
    }
    ret = libmp4_bitreader_read(&bits, 1, &value); /* conformance_window_flag */
    if (ret != LIBMP4_OK) {
        return ret;
    }
    if (value != 0) {
        for (i = 0; i < 4u; ++i) {
            ret = libmp4_bitreader_read_ue(&bits, &value);
            if (ret != LIBMP4_OK) {
                return ret;
            }
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

int libmp4_h265_scan_nal(void* user, const uint8_t* nal, size_t len)
{
    libmp4_video_scan_t* scan = (libmp4_video_scan_t*)user;
    uint8_t type;

    if (scan == NULL || scan->track == NULL || len < 2) {
        return LIBMP4_OK;
    }
    type = (uint8_t)((nal[0] >> 1) & 0x3fu);
    if (type == 32) {
        if (len > LIBMP4_VPS_MAX) {
            return LIBMP4_ERR_LIMIT;
        }
        memcpy(scan->track->vps, nal, len);
        scan->track->vps_len = (uint16_t)len;
        return LIBMP4_OK;
    }
    if (type == 33) {
        if (len > LIBMP4_SPS_MAX) {
            return LIBMP4_ERR_LIMIT;
        }
        memcpy(scan->track->sps, nal, len);
        scan->track->sps_len = (uint16_t)len;
        return LIBMP4_OK;
    }
    if (type == 34) {
        if (len > LIBMP4_PPS_MAX) {
            return LIBMP4_ERR_LIMIT;
        }
        memcpy(scan->track->pps, nal, len);
        scan->track->pps_len = (uint16_t)len;
        return LIBMP4_OK;
    }
    if (type == 35) {
        return LIBMP4_OK;
    }
    if (type <= 31) {
        scan->vcl = 1;
    }
    if (type >= 16 && type <= 21) {
        scan->keyframe = 1;
    }
    if (len > 0xffffffffu - 4u - scan->sample_size) {
        return LIBMP4_ERR_LIMIT;
    }
    scan->sample_size += (uint32_t)len + 4u;
    return LIBMP4_OK;
}

int libmp4_h265_should_skip(const uint8_t* nal, size_t len)
{
    uint8_t type;

    if (len < 2) {
        return 1;
    }
    type = (uint8_t)((nal[0] >> 1) & 0x3fu);
    return type == 32 || type == 33 || type == 34 || type == 35;
}

static int libmp4_h265_write_hvcc_array(libmp4_writer_t* w,
                                        uint8_t nal_type,
                                        const uint8_t* data,
                                        uint16_t len)
{
    libmp4_write_u8(w, (uint8_t)(0x80u | (nal_type & 0x3fu)));
    libmp4_write_u16(w, 1);
    libmp4_write_u16(w, len);
    return libmp4_write_data(w, data, len);
}

int libmp4_h265_write_hvcc(libmp4_writer_t* w, const libmp4_track_t* t)
{
    libmp4_h265_sps_info_t info;
    uint64_t box;
    int ret = libmp4_h265_parse_sps_info(t != NULL ? t->sps : NULL,
                                         t != NULL ? t->sps_len : 0,
                                         &info);

    if (w == NULL || t == NULL || t->vps_len == 0 || t->sps_len == 0 ||
        t->pps_len == 0 || ret != LIBMP4_OK) {
        if (w != NULL) {
            w->error = ret != LIBMP4_OK ? ret : LIBMP4_ERR_FORMAT;
        }
        return ret != LIBMP4_OK ? ret : LIBMP4_ERR_FORMAT;
    }
    box = libmp4_box_start(w, "hvcC");

    libmp4_write_u8(w, 1);
    libmp4_write_u8(w, (uint8_t)((info.general_profile_space << 6) |
                                 (info.general_tier_flag << 5) |
                                 info.general_profile_idc));
    libmp4_write_u32(w, info.general_profile_compatibility_flags);
    libmp4_write_u32(w, (uint32_t)(info.general_constraint_indicator_flags >> 16));
    libmp4_write_u16(w, (uint16_t)info.general_constraint_indicator_flags);
    libmp4_write_u8(w, info.general_level_idc);
    libmp4_write_u16(w, 0xf000u);
    libmp4_write_u8(w, 0xfcu);
    libmp4_write_u8(w, (uint8_t)(0xfcu | info.chroma_format));
    libmp4_write_u8(w, (uint8_t)(0xf8u | info.bit_depth_luma_minus8));
    libmp4_write_u8(w, (uint8_t)(0xf8u | info.bit_depth_chroma_minus8));
    libmp4_write_u16(w, 0);
    libmp4_write_u8(w, (uint8_t)((info.num_temporal_layers << 3) |
                                 (info.temporal_id_nested << 2) | 3u));
    libmp4_write_u8(w, 3);
    libmp4_h265_write_hvcc_array(w, 32, t->vps, t->vps_len);
    libmp4_h265_write_hvcc_array(w, 33, t->sps, t->sps_len);
    libmp4_h265_write_hvcc_array(w, 34, t->pps, t->pps_len);
    return libmp4_box_end(w, box);
}

static int libmp4_h265_copy_param(uint8_t* dst,
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

int libmp4_h265_parse_hvcc(libmp4_reader_t* reader,
                           libmp4_reader_track_t* track,
                           const libmp4_box_t* box)
{
    uint8_t* data;
    uint64_t payload_size;
    size_t pos;
    uint8_t arrays;
    uint8_t i;
    int ret = LIBMP4_OK;

    payload_size = box->end - box->data;
    if (payload_size < 23u || payload_size > 65536u) {
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
    track->nal_length_size = (uint8_t)((data[21] & 0x03u) + 1u);
    arrays = data[22];
    pos = 23;
    for (i = 0; i < arrays && pos + 3u <= payload_size; ++i) {
        uint8_t nal_type = (uint8_t)(data[pos++] & 0x3fu);
        uint16_t nal_count = libmp4_rb16(data + pos);
        uint16_t n;
        pos += 2u;
        for (n = 0; n < nal_count && pos + 2u <= payload_size; ++n) {
            uint32_t len = libmp4_rb16(data + pos);
            pos += 2u;
            if (pos + len > payload_size) {
                ret = LIBMP4_ERR_FORMAT;
                break;
            }
            if (n == 0 && nal_type == 32) {
                ret = libmp4_h265_copy_param(track->vps,
                                             &track->vps_len,
                                             LIBMP4_VPS_MAX,
                                             data + pos,
                                             len);
            } else if (n == 0 && nal_type == 33) {
                ret = libmp4_h265_copy_param(track->sps,
                                             &track->sps_len,
                                             LIBMP4_SPS_MAX,
                                             data + pos,
                                             len);
            } else if (n == 0 && nal_type == 34) {
                ret = libmp4_h265_copy_param(track->pps,
                                             &track->pps_len,
                                             LIBMP4_PPS_MAX,
                                             data + pos,
                                             len);
            }
            if (ret != LIBMP4_OK) {
                break;
            }
            pos += len;
        }
        if (ret != LIBMP4_OK) {
            break;
        }
    }
    free(data);
    return ret;
}
