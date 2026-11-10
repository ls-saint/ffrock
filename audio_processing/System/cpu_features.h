
#ifndef SYSTEM_WRAPPERS_INCLUDE_CPU_FEATURES_WRAPPER_H_
#define SYSTEM_WRAPPERS_INCLUDE_CPU_FEATURES_WRAPPER_H_

#include <stdint.h>

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

// List of features in x86.
typedef enum { kSSE2, kSSE3 } CPUFeature;

// List of features in ARM.
enum {
  kCPUFeatureARMv7 = (1 << 0),
  kCPUFeatureVFPv3 = (1 << 1),
  kCPUFeatureNEON = (1 << 2),
  kCPUFeatureLDREXSTREX = (1 << 3)
};

typedef int (*MiniBase_CPUInfo)(CPUFeature feature);

// Returns true if the CPU supports the feature.
extern MiniBase_CPUInfo MiniBase_GetCPUInfo;

// No CPU feature is available => straight C path.
extern MiniBase_CPUInfo MiniBase_GetCPUInfoNoASM;

// Return the features in an ARM device.
// It detects the features in the hardware platform, and returns supported
// values in the above enum definition as a bitmask.
extern uint64_t MiniBase_GetCPUFeaturesARM(void);

#if defined(__cplusplus) || defined(c_plusplus)
}  // extern "C"
#endif

#endif  // SYSTEM_WRAPPERS_INCLUDE_CPU_FEATURES_WRAPPER_H_
