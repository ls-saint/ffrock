#include "miipc_g711.h"

#include <stddef.h>

static int miipc_g711_search_segment(int value, const int* table, int count)
{
    int i;

    for (i = 0; i < count; ++i) {
        if (value <= table[i]) {
            return i;
        }
    }
    return count;
}

uint8_t miipc_g711a_encode_sample(int16_t sample)
{
    static const int seg_end[8] = {
        0x001F, 0x003F, 0x007F, 0x00FF,
        0x01FF, 0x03FF, 0x07FF, 0x0FFF
    };
    int pcm = (int)sample;
    int mask;
    int seg;
    uint8_t aval;

    if (pcm >= 0) {
        mask = 0xD5;
    } else {
        mask = 0x55;
        pcm = -pcm - 1;
        if (pcm < 0) {
            pcm = 0;
        }
    }

    if (pcm > 0x7FFF) {
        pcm = 0x7FFF;
    }
    pcm >>= 3;

    seg = miipc_g711_search_segment(pcm, seg_end, 8);
    if (seg >= 8) {
        return (uint8_t)(0x7F ^ mask);
    }

    aval = (uint8_t)(seg << 4);
    if (seg < 2) {
        aval |= (uint8_t)((pcm >> 1) & 0x0F);
    } else {
        aval |= (uint8_t)((pcm >> seg) & 0x0F);
    }
    return (uint8_t)(aval ^ mask);
}

int16_t miipc_g711a_decode_sample(uint8_t alaw)
{
    int value;
    int segment;

    alaw ^= 0x55;
    value = (alaw & 0x0F) << 4;
    segment = (alaw & 0x70) >> 4;
    switch (segment) {
    case 0:
        value += 8;
        break;
    case 1:
        value += 0x108;
        break;
    default:
        value += 0x108;
        value <<= segment - 1;
        break;
    }

    return (int16_t)((alaw & 0x80) ? value : -value);
}

size_t miipc_g711a_encode(const int16_t* pcm, size_t samples, uint8_t* out)
{
    size_t i;

    if (pcm == NULL || out == NULL) {
        return 0;
    }
    for (i = 0; i < samples; ++i) {
        out[i] = miipc_g711a_encode_sample(pcm[i]);
    }
    return samples;
}

size_t miipc_g711a_decode(const uint8_t* data, size_t bytes, int16_t* pcm)
{
    size_t i;

    if (data == NULL || pcm == NULL) {
        return 0;
    }
    for (i = 0; i < bytes; ++i) {
        pcm[i] = miipc_g711a_decode_sample(data[i]);
    }
    return bytes;
}

uint8_t miipc_g711u_encode_sample(int16_t sample)
{
    static const int seg_end[8] = {
        0x00FF, 0x01FF, 0x03FF, 0x07FF,
        0x0FFF, 0x1FFF, 0x3FFF, 0x7FFF
    };
    const int bias = 0x84;
    const int clip = 32635;
    int pcm = (int)sample;
    int mask;
    int seg;
    uint8_t uval;

    if (pcm < 0) {
        pcm = -pcm;
        mask = 0x7F;
    } else {
        mask = 0xFF;
    }

    if (pcm > clip) {
        pcm = clip;
    }
    pcm += bias;

    seg = miipc_g711_search_segment(pcm, seg_end, 8);
    if (seg >= 8) {
        return (uint8_t)(0x7F ^ mask);
    }

    uval = (uint8_t)((seg << 4) | ((pcm >> (seg + 3)) & 0x0F));
    return (uint8_t)(uval ^ mask);
}

int16_t miipc_g711u_decode_sample(uint8_t ulaw)
{
    const int bias = 0x84;
    int value;

    ulaw = (uint8_t)~ulaw;
    value = ((ulaw & 0x0F) << 3) + bias;
    value <<= (ulaw & 0x70) >> 4;

    if (ulaw & 0x80) {
        value = bias - value;
    } else {
        value = value - bias;
    }
    return (int16_t)value;
}

size_t miipc_g711u_encode(const int16_t* pcm, size_t samples, uint8_t* out)
{
    size_t i;

    if (pcm == NULL || out == NULL) {
        return 0;
    }
    for (i = 0; i < samples; ++i) {
        out[i] = miipc_g711u_encode_sample(pcm[i]);
    }
    return samples;
}

size_t miipc_g711u_decode(const uint8_t* data, size_t bytes, int16_t* pcm)
{
    size_t i;

    if (data == NULL || pcm == NULL) {
        return 0;
    }
    for (i = 0; i < bytes; ++i) {
        pcm[i] = miipc_g711u_decode_sample(data[i]);
    }
    return bytes;
}
