#ifndef MI_AUDIO_PROCESSING_COMPILE_ASSERT_C_H_
#define MI_AUDIO_PROCESSING_COMPILE_ASSERT_C_H_

#define MINIBASE_COMPILE_ASSERT(expression) \
  switch (0) {                              \
    case 0:                                 \
    case expression:;                       \
  }

#endif
