#ifndef MIIPC_G711_H_
#define MIIPC_G711_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t miipc_g711a_encode_sample(int16_t pcm);
int16_t miipc_g711a_decode_sample(uint8_t alaw);
size_t miipc_g711a_encode(const int16_t* pcm, size_t samples, uint8_t* out);
size_t miipc_g711a_decode(const uint8_t* data, size_t bytes, int16_t* pcm);

uint8_t miipc_g711u_encode_sample(int16_t pcm);
int16_t miipc_g711u_decode_sample(uint8_t ulaw);
size_t miipc_g711u_encode(const int16_t* pcm, size_t samples, uint8_t* out);
size_t miipc_g711u_decode(const uint8_t* data, size_t bytes, int16_t* pcm);

#ifdef __cplusplus
}
#endif

#endif
