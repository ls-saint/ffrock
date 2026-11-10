#include "libmp4.h"

const char* libmp4_version(void)
{
    return "libmp4-0.4";
}

const char* libmp4_error_text(int error_code)
{
    switch (error_code) {
    case LIBMP4_OK:
        return "ok";
    case LIBMP4_ERR_ARG:
        return "invalid argument";
    case LIBMP4_ERR_IO:
        return "io error";
    case LIBMP4_ERR_NOMEM:
        return "out of memory";
    case LIBMP4_ERR_FORMAT:
        return "invalid or incomplete mp4 data";
    case LIBMP4_ERR_LIMIT:
        return "configured limit exceeded";
    default:
        return "unknown error";
    }
}
