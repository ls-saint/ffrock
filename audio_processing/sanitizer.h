#ifndef MI_AUDIO_PROCESSING_SANITIZER_H_
#define MI_AUDIO_PROCESSING_SANITIZER_H_

#include <stddef.h>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define BASE_HAS_ASAN 1
#endif
#if __has_feature(memory_sanitizer)
#define BASE_HAS_MSAN 1
#endif
#endif

#ifndef BASE_HAS_ASAN
#define BASE_HAS_ASAN 0
#endif

#ifndef BASE_HAS_MSAN
#define BASE_HAS_MSAN 0
#endif

#if BASE_HAS_ASAN
#include <sanitizer/asan_interface.h>
#endif

#if BASE_HAS_MSAN
#include <sanitizer/msan_interface.h>
#endif

#if defined(__has_attribute)
#if __has_attribute(no_sanitize)
#define BASE_NO_SANITIZE(what) __attribute__((no_sanitize(what)))
#endif
#endif

#ifndef BASE_NO_SANITIZE
#define BASE_NO_SANITIZE(what)
#endif

static inline void MiniBase_AsanPoison(const volatile void* ptr,
                                       size_t element_size,
                                       size_t num_elements) {
#if BASE_HAS_ASAN
  ASAN_POISON_MEMORY_REGION(ptr, element_size * num_elements);
#else
  (void)ptr;
  (void)element_size;
  (void)num_elements;
#endif
}

static inline void MiniBase_AsanUnpoison(const volatile void* ptr,
                                         size_t element_size,
                                         size_t num_elements) {
#if BASE_HAS_ASAN
  ASAN_UNPOISON_MEMORY_REGION(ptr, element_size * num_elements);
#else
  (void)ptr;
  (void)element_size;
  (void)num_elements;
#endif
}

static inline void MiniBase_MsanMarkUninitialized(const volatile void* ptr,
                                                  size_t element_size,
                                                  size_t num_elements) {
#if BASE_HAS_MSAN
  __msan_poison(ptr, element_size * num_elements);
#else
  (void)ptr;
  (void)element_size;
  (void)num_elements;
#endif
}

static inline void MiniBase_MsanCheckInitialized(const volatile void* ptr,
                                                 size_t element_size,
                                                 size_t num_elements) {
#if BASE_HAS_MSAN
  __msan_check_mem_is_initialized(ptr, element_size * num_elements);
#else
  (void)ptr;
  (void)element_size;
  (void)num_elements;
#endif
}

#endif
