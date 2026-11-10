#ifndef MI_AUDIO_PROCESSING_CHECKS_H_
#define MI_AUDIO_PROCESSING_CHECKS_H_

#include <assert.h>

#include "signal_processing/include/compile_assert_c.h"

#define MINIBASE_DCHECK(condition) assert(condition)
#define MINIBASE_DCHECK_EQ(a, b) assert((a) == (b))
#define MINIBASE_DCHECK_NE(a, b) assert((a) != (b))
#define MINIBASE_DCHECK_GT(a, b) assert((a) > (b))
#define MINIBASE_DCHECK_GE(a, b) assert((a) >= (b))
#define MINIBASE_DCHECK_LT(a, b) assert((a) < (b))
#define MINIBASE_DCHECK_LE(a, b) assert((a) <= (b))

#endif
