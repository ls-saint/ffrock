#ifndef MI_SIG_H_
#define MI_SIG_H_

#include <stddef.h>

#if defined(_WIN32)
#if defined(MI_SIG_BUILD)
#define MI_SIG_API __declspec(dllexport)
#else
#define MI_SIG_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define MI_SIG_API __attribute__((visibility("default")))
#else
#define MI_SIG_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MI_SIG_SHA1_HEX_SIZE 41u

enum MiSigResult {
    MI_SIG_OK = 0,
    MI_SIG_ERR_INVALID_ARGUMENT = -1,
    MI_SIG_ERR_UNSUPPORTED_ALGORITHM = -2,
    MI_SIG_ERR_BUFFER_TOO_SMALL = -3,
    MI_SIG_ERR_CRYPTO = -4
};

/*
 * Calculate the HMAC over a null-terminated UTF-8 message and return a
 * 40-character lowercase hexadecimal digest. The returned pointer is
 * thread-local and remains valid until the next call on the same thread.
 * Returns NULL on failure.
 */
MI_SIG_API const char* ComputeHmac(const char* algorithm, const char* sig);

/* Buffer-based variant. output_size must be at least MI_SIG_SHA1_HEX_SIZE. */
MI_SIG_API int ComputeHmacToBuffer(const char* algorithm,
                                   const char* sig,
                                   char* output,
                                   size_t output_size);

#ifdef __cplusplus
}
#endif

#endif /* MI_SIG_H_ */
