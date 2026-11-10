#ifndef LIBMP4_BITREADER_H_
#define LIBMP4_BITREADER_H_

#include <stddef.h>
#include <stdint.h>

/* Reads an H.26x RBSP while removing Annex-B emulation-prevention bytes. */
typedef struct libmp4_bitreader {
    const uint8_t* data;
    size_t bytes;
    size_t offset;
    uint8_t cache;
    uint8_t bits_left;
    uint8_t zero_bytes;
} libmp4_bitreader_t;

static void libmp4_bitreader_init(libmp4_bitreader_t* reader,
                                  const uint8_t* data,
                                  size_t bytes)
{
    reader->data = data;
    reader->bytes = bytes;
    reader->offset = 0;
    reader->cache = 0;
    reader->bits_left = 0;
    reader->zero_bytes = 0;
}

static int libmp4_bitreader_next_byte(libmp4_bitreader_t* reader,
                                      uint8_t* value)
{
    if (reader == NULL || value == NULL) {
        return LIBMP4_ERR_ARG;
    }
    while (reader->offset < reader->bytes) {
        uint8_t byte = reader->data[reader->offset++];

        if (reader->zero_bytes >= 2u && byte == 0x03u) {
            reader->zero_bytes = 0;
            continue;
        }
        if (byte == 0) {
            if (reader->zero_bytes < 0xffu) {
                ++reader->zero_bytes;
            }
        } else {
            reader->zero_bytes = 0;
        }
        *value = byte;
        return LIBMP4_OK;
    }
    return LIBMP4_ERR_FORMAT;
}

static int libmp4_bitreader_read(libmp4_bitreader_t* reader,
                                 uint8_t bits,
                                 uint32_t* value)
{
    uint32_t result = 0;

    if (reader == NULL || value == NULL || bits > 32u) {
        return LIBMP4_ERR_ARG;
    }
    while (bits > 0) {
        uint8_t take;
        uint8_t shift;
        uint32_t part;
        int ret;

        if (reader->bits_left == 0) {
            ret = libmp4_bitreader_next_byte(reader, &reader->cache);
            if (ret != LIBMP4_OK) {
                return ret;
            }
            reader->bits_left = 8;
        }
        take = bits < reader->bits_left ? bits : reader->bits_left;
        shift = (uint8_t)(reader->bits_left - take);
        part = ((uint32_t)reader->cache >> shift) & ((1u << take) - 1u);
        result = (result << take) | part;
        reader->bits_left = (uint8_t)(reader->bits_left - take);
        bits = (uint8_t)(bits - take);
    }
    *value = result;
    return LIBMP4_OK;
}

static int libmp4_bitreader_skip(libmp4_bitreader_t* reader, uint32_t bits)
{
    while (bits > 0) {
        uint32_t ignored;
        uint8_t count = bits > 32u ? 32u : (uint8_t)bits;
        int ret = libmp4_bitreader_read(reader, count, &ignored);

        if (ret != LIBMP4_OK) {
            return ret;
        }
        bits -= count;
    }
    return LIBMP4_OK;
}

static int libmp4_bitreader_read_ue(libmp4_bitreader_t* reader,
                                    uint32_t* value)
{
    uint32_t bit;
    uint32_t suffix = 0;
    uint32_t leading_zero_bits = 0;
    int ret;

    if (reader == NULL || value == NULL) {
        return LIBMP4_ERR_ARG;
    }
    do {
        ret = libmp4_bitreader_read(reader, 1, &bit);
        if (ret != LIBMP4_OK) {
            return ret;
        }
        if (bit == 0) {
            if (leading_zero_bits >= 30u) {
                return LIBMP4_ERR_FORMAT;
            }
            ++leading_zero_bits;
        }
    } while (bit == 0);

    if (leading_zero_bits > 0) {
        ret = libmp4_bitreader_read(reader, (uint8_t)leading_zero_bits, &suffix);
        if (ret != LIBMP4_OK) {
            return ret;
        }
    }
    *value = ((1u << leading_zero_bits) - 1u) + suffix;
    return LIBMP4_OK;
}

#endif /* LIBMP4_BITREADER_H_ */
